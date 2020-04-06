#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include "dbg_guv.h"
#include "textio.h"
#include "queue.h"

char const *const DBG_GUV_SUCC = "success";
char const *const DBG_GUV_NULL_ARG = "received NULL argument";
char const *const DBG_GUV_NULL_CB = "received NULL callback";
char const *const DBG_GUV_NULL_CONN_INFO = "received NULL fpga_connection_info";

//////////////////////////////
//Static functions/variables//
//////////////////////////////

//Thread that manages outgoing network data. Empties fpga->egress queue
//arg is a pointer to an fpga_connection_info struct
static void* fpga_egress_thread(void *arg) {
    fpga_connection_info *info = (fpga_connection_info *)arg;
    #ifdef DEBUG_ON
    fprintf(stderr, "Entered net_tx (%p)\n", info);
    #endif
    
    if (info == NULL) {
        #ifdef DEBUG_ON
        fprintf(stderr, "NULL argument to fpga_egress_thread!\n");
        #endif
        pthread_exit(NULL);
    }
    
    queue *q = &info->egress;
    
    char cmd[4];
    while (dequeue_n(q, cmd, 4) == 0) {
        int len = write(info->sfd, cmd, 4);
        if (len <= 0) break;
    }
    
    #ifdef DEBUG_ON
    fprintf(stderr, "Exited net_tx (%p)\n", info);
    #endif
    pthread_exit(NULL);
}

static void init_dbg_guv(dbg_guv *d, fpga_connection_info *f, int addr) {
    init_linebuf(&d->logs, DBG_GUV_SCROLLBACK);
    pthread_mutex_init(&d->logs_mutex, NULL);
    
    d->need_redraw = 1; //Need to draw when first added in
    d->values_unknown = 1;
    d->parent = f;
    d->addr = addr;
} 

static void deinit_dbg_guv(dbg_guv *d) {
    if (!d) return; //I guess we'll do this?
    free_linebuf_logs(&d->logs);
    deinit_linebuf(&d->logs);
    
    #ifndef DISABLE_WEIRD_LOCK
    pthread_mutex_lock(&d->logs_mutex);
    pthread_mutex_unlock(&d->logs_mutex);
    #endif
    pthread_mutex_destroy(&d->logs_mutex);
}

//A helper function to properly allocate, construct, and initialize an 
//fpga_connection_info struct.
static fpga_connection_info *construct_fpga_connection() {
    fpga_connection_info *ret = malloc(sizeof(fpga_connection_info));
    if (!ret) return NULL;
    
    int i;
    for (i = 0; i < MAX_GUVS_PER_FPGA; i++) {
        init_dbg_guv(ret->guvs + i, ret, i);
    }
    
    ret->buf_pos = 0;
    
    init_queue(&ret->egress, 1, 1);
    
    return ret;
}

//Struct for arguments to thread that opens a new connection
typedef struct _open_fpga_conn_args {
    fpga_connection_info *f;
    new_fpga_cb *cb;
    char *node;
    char *serv;
    void *user_data;
} open_fpga_conn_args;

//Thread that opens a new connection and calls a callback with either 1) the
//completed fpga_connection_info struct or 2) an error message. By the way,
//this thread takes care of freeing the args it was given, since it is not
//intended to be used with pthread_join. I hope it doesn't block forever...
static void* open_fpga_conn_thread(void *arg) {
    new_fpga_cb_info ret;
    int err_occurred = 0;
    int sfd = -1;
    
    open_fpga_conn_args *args = (open_fpga_conn_args*)arg;
    if (args == NULL) {
        ret.f = NULL;
        ret.error_str = DBG_GUV_NULL_ARG;
        err_occurred = 1;
        goto err_nothing;
    }
    
    fpga_connection_info *f = args->f;
    if (f == NULL) {
        ret.f = NULL;
        ret.error_str = DBG_GUV_NULL_CONN_INFO;
        err_occurred = 1;
        goto err_nothing;
    } else {
        ret.f = f;
    }
    
    new_fpga_cb *cb = args->cb;
    if (cb == NULL) {
        ret.f = NULL;
        ret.error_str = DBG_GUV_NULL_CB;
        err_occurred = 1;
        goto err_delete_f;
    }
    
    if (args != NULL) ret.user_data = args->user_data;
    
    //Get address info
    struct addrinfo *res = NULL;
    struct addrinfo hint = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = 0 //Not sure if this protocol field needs to be set
    };
    
    int rc = getaddrinfo(args->node, args->serv, &hint, &res);
    if (rc < 0) {
        //cursor_pos(1, term_rows-1);
        //sprintf(line, "Could not resolve [%s]: %s" ERASE_TO_END "%n", args->node, gai_strerror(rc), &len);
        //write(STDOUT_FILENO, line, len);
        ret.f = NULL;
        ret.error_str = gai_strerror(rc);
        err_occurred = 1;
        goto err_delete_f;
    }
    
    //Connect the socket
    sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
        //cursor_pos(1, term_rows-1);
        //sprintf(line, "Could not open socket: %s" ERASE_TO_END "%n", strerror(errno), &len);
        //write(STDOUT_FILENO, line, len);
        ret.f = NULL;
        ret.error_str = strerror(errno);
        err_occurred = 1;
        goto err_freeaddrinfo;
    }
    f->sfd = sfd;
    
    rc = connect(sfd, res->ai_addr, res->ai_addrlen);
    if (rc < 0) {
        //cursor_pos(1, term_rows-1);
        //sprintf(line, "Could not connect socket: %s" ERASE_TO_END "%n", strerror(errno), &len);
        //write(STDOUT_FILENO, line, len);
        ret.f = NULL;
        ret.error_str = strerror(errno);
        err_occurred = 1;
        goto err_close_socket;
    }
    
    freeaddrinfo(res);
    //Don't forget to mark as NULL so cleanup code can deal with it properly
    res = NULL;
    
    //Start the net_tx thread
    pthread_create(&f->net_tx, NULL, fpga_egress_thread, f);
    f->net_tx_started = 1;
    
    //Clean up and exit
    if (!err_occurred) ret.error_str = DBG_GUV_SUCC;
err_close_socket:
    if (err_occurred) close(sfd);
err_freeaddrinfo:
    if (res != NULL) freeaddrinfo(res);
err_delete_f:
    if (err_occurred) del_fpga_connection(f);
err_nothing:
    free(args);
    //Let the callback know what happened, if we have one
    if(cb != NULL) cb(ret);
    pthread_exit(NULL);
}

///////////////////////////////////////////
//Implementations of prototypes in header//
///////////////////////////////////////////

//(copy comments from header once I settle on them)
int new_fpga_connection(new_fpga_cb *cb, char *node, char *serv, void *user_data) {
    if (!cb) {
        return -1;
    }
    
    fpga_connection_info *f = construct_fpga_connection();
    if (!f) {
        return -1;
    }
    
    open_fpga_conn_args *args = malloc(sizeof(open_fpga_conn_args));
    if (!args) {
        del_fpga_connection(f);
        return -1;
    }
    
    args->f = f;
    args->cb = cb;
    args->node = node;
    args->serv = serv;
    args->user_data = user_data;
    
    //Spin up thread that opens connection
    pthread_t tid;
    pthread_create(&tid, NULL, open_fpga_conn_thread, args);
    
    //Note: we don't pthread_join. That thread (should) end itself at some
    //point
    return 0;
}

void del_fpga_connection(fpga_connection_info *f) {
    //Gracefully quit early if f is NULL
    if (f == NULL) return;
    
    deinit_queue(&f->egress);
    
    //We gave peace a chance, but really, make sure net_tx stops
    if (f->net_tx_started) {
        pthread_cancel(f->net_tx);
        pthread_join(f->net_tx, NULL);
    }
    f->net_tx_started = 0;
    
    //Try locking and unlocking all mutexes to wait until last person 
    //relinquishes it
    //Note that it's up to the caller to make sure no one is going to try 
    //locking these mutexes later
    int i;
    for (i = 0; i < MAX_GUVS_PER_FPGA; i++) {        
        //Slow as hell... there must be a better way...
        //Also, I've gotten into trouble here. The append_log function
        //specifically does not malloc or free or copy anything. However, 
        //this fpga_connection_info cleanup assumes that non-NULL strings
        //should be freed. This is inconsistent. I should fix this 
        deinit_dbg_guv(&f->guvs[i]);
    }
    
    free(f);
}

//Returns string which was displaced by new log. This can be NULL. NOTE: 
//do NOT call this function if you are holding a mutex! By the way, does NOT
//make an internal copy of the string; you must copy it yourself if this is
//desired
char *append_log(fpga_connection_info *f, int addr, char *log) {
    if (addr < 0 || addr > MAX_GUVS_PER_FPGA) {
        return NULL;
    }
    pthread_mutex_lock(&f->guvs[addr].logs_mutex);
    linebuf *l = &f->guvs[addr].logs;
    char *ret = linebuf_append(l, log);
    pthread_mutex_unlock(&f->guvs[addr].logs_mutex);
    
    f->guvs[addr].need_redraw = 1;
    
    return ret;
}

//Duplicates string in name and saves it into d. If name was previously set, it
//will be freed
void dbg_guv_set_name(dbg_guv *d, char *name) {
    if (d->name != NULL) free(d->name);
    d->name = strdup(name);
}

//Returns number of bytes added into buf, or -1 on error.
int draw_fn_dbg_guv(void *item, int x, int y, int w, int h, char *buf) {
    dbg_guv *d = (dbg_guv*) item;
    if (!d) return -1;
    
    if (d->need_redraw == 0) return 0; //Nothing to draw!
    
    //Save old value of buf so we can calculate number of characters added
    char *buf_saved = buf;
    
    //Draw title bar
    //First, turn on inverted video mode
    *buf++ = '\e'; *buf++ = '['; *buf++ = '7'; *buf++ = 'm';
    
    char status[6];
    status[0] = '|';
    status[1] = d->keep_pausing ? 'P' : '-';
    status[2] = d->keep_logging ? 'L' : (d->log_cnt != 0 ? 'l' : '-');
    status[3] = d->keep_dropping ? 'D' : (d->log_cnt != 0 ? 'd' : '-');
    status[4] = d->inj_TVALID ? 'V' : '-';
    status[5] = '\0';
    static char const *const unknown = "|????";
    
    int incr = cursor_pos_cmd(buf, x, y);
    buf += incr;
    
    sprintf(buf, "%-*.*s%s%n", w - 5, w - 5, d->name, (d->values_unknown ? unknown : status), &incr);
    buf += incr;
    //Turn off inverted video
    *buf++ = '\e'; *buf++ = '['; *buf++ = '2'; *buf++ = '7'; *buf++ = 'm';
    
    //Now simply draw the linebuf
    pthread_mutex_lock(&d->logs_mutex);
    incr = draw_linebuf(&d->logs, d->log_pos, x, y + 1, w, h - 1, buf);
    pthread_mutex_unlock(&d->logs_mutex);
    
    if (incr < 0) {
        //Propagate error, not that it really matters...
        d->error_str = d->logs.error_str;
        return -1;
    }
    buf += incr;
    
    d->need_redraw = 0;
    
    return buf - buf_saved;
}

//Returns how many bytes are needed (can be an upper bound) to draw dbg_guv
//given the size
int draw_sz_dbg_guv(void *item, int w, int h) {
    dbg_guv *d = (dbg_guv*) item;
    if (!d) return -1;
    
    if (d->need_redraw == 0) return 0; //Nothing to draw!
    
    int total_sz = 0;
    total_sz += 10; //Bytes needed to initially position the cursor
    total_sz += 4; //Bytes needed to invert colours for title bar
    total_sz += w; //Bytes needed for title bar
    total_sz += 5; //Bytes needed to turn off inverted colours
    
    int log_sz = 10 + w; //Bytes needed to move the cursor to a line, plus the length of a line
    
    total_sz += (h-1) * log_sz; //We draw h-1 lines from the linebuf, since the first line is a title bar
    
    return total_sz;
}

//Tells us that we should redraw, probably because we moved to another
//area of the screen
void trigger_redraw_dbg_guv(void *item) {
    dbg_guv *d = (dbg_guv*) item;
    if (!d) return;
    
    d->need_redraw = 1;
}
