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

char const *const DBG_GUV_SUCC = "success";
char const *const DBG_GUV_NULL_ARG = "received NULL argument";
char const *const DBG_GUV_NULL_CB = "received NULL callback";
char const *const DBG_GUV_NULL_CONN_INFO = "received NULL fpga_connection_info";

//////////////////////////////
//Static functions/variables//
//////////////////////////////

//Little helper functions. I probably should have used container_of, but
//whatever
static inline msg_win *get_msg_win(dbg_guv *g) {
	fpga_connection_info *f = g->parent;
	return &f->logs[g->addr];
}

static inline pthread_mutex_t *get_msg_win_mutex(dbg_guv *g) {
	fpga_connection_info *f = g->parent;
	return &f->logs_mutex[g->addr];
}

//Thread that manages outgoing network data. Empties fpga->egress queue
//arg is a pointer to an fpga_connection_info struct
static void* fpga_egress_thread(void *arg) {
    fpga_connection_info *info = (fpga_connection_info *)v;
	#ifdef DEBUG_ON
	fprintf(stderr, "Entered net_tx (%p)\n", info);
	#endif
	
    if (info == NULL) {
		#ifdef DEBUG_ON
		fprintf(stderr, "NULL argument to fpga_egress_thread!\n", info);
		#endif
		pthread_exit(NULL);
	}
	
	queue *q = info->egress;
    
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

//A helper function to properly allocate, construct, and initialize an 
//fpga_connection_info struct.
static fpga_connection_info *construct_fpga_connection() {
	//TODO: sanity check inputs
	
    fpga_connection_info *ret = malloc(sizeof(fpga_connection_info));
    if (!ret) return NULL;
    
    int i;
    for (i = 0; i < MAX_GUVS_PER_FPGA; i++) {
		int rc = init_msg_win(&(ret->logs[i]), NULL);
		if (rc < 0) {
			fprintf(stderr, "Could not allocate a msg_win while constructing fpga_connection_info: %s\n", ret->logs[i].error_str);
			//Clean up everything we allocated so far
			int j;
			for (j = i - 1; j >= 0; j--) {
				pthread_mutex_destroy(&ret->logs_mutex[j]);
			}
			free(ret);
			return NULL;
		}
		
        pthread_mutex_init(&ret->logs_mutex[i], NULL);
        
        ret->guvs[i].values_unknown = 1;
        ret->guvs[i].parent = ret;
        ret->guvs[i].addr = i;
    }
    
    pthread_mutex_init(&ret->ingress.mutex, NULL);
    pthread_mutex_init(&ret->egress.mutex, NULL);
    
    return ret;
}

//Struct for arguments to thread that opens a new connection
typedef struct _open_fpga_conn_args {
	fpga_connection_info *info;
	new_fpga_cb *cb;
	char *node;
	char *serv;
	void *user_data;
} open_fpga_conn_args;

//Thread that opens a new connection and calls a callback with either 1) the
//completed fpga_connection_info struct or 2) an error message. By the way,
//this thread takes care of freeing the args it was given, since it is not
//intended to be used with pthread_join. I hope it doesn't block forever...
static void open_fpga_conn_thread(void *v) {
	new_fpga_cb_info ret;
	int err_occurred = 0;
	int sfd = -1;
	
	open_fpga_conn_args *args = *(open_fpga_conn_args*)v;
	if (args == NULL) {
		ret.f = NULL;
		ret.error_str = DBG_GUV_NULL_ARG;
		err_occurred = 1;
		goto err_nothing;
	}
	
	new_fpga_cb *cb = args->cb;
	if (cb == NULL) {
		ret.f = NULL;
		ret.error_str = DBG_GUV_NULL_CB;
		err_occurred = 1;
		goto err_nothing;
	}
	
	fpga_connection_info *f = args->f;
	if (f == NULL) {
		ret.f = NULL;
		ret.error_str = DBG_GUV_NULL_CONN_INFO;
		err_occurred = 1;
		goto err_nothing;
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
        goto err_nothing;
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
    args->sfd = sfd;
    
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
	f->egress.num_producers++;
	f->egress.num_consumers++;
    pthread_create(&args->tx_thread, NULL, net_tx, args);
    f->net_tx_started = 1;
    args->net_tx_started = 1;
	
	//Clean up and exit
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
	
	args->info = f;
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
    
    pthread_mutex_lock(&f->egress.mutex);
    f->egress.num_consumers = -1;
    f->egress.num_producers = -1;
    pthread_mutex_unlock(&f->egress.mutex);
    
    //Not sure if this is needed, but make sure we allow threads on this
    //queue to exit gracefully
    pthread_cond_broadcast(&f->egress.can_cons);
    pthread_cond_broadcast(&f->egress.can_prod);
    sched_yield();
    
    //We gave peace a chance, but really, make sure net_tx stops
    pthread_cancel(&f->net_tx)
    
    //We're done with the egress queue
    pthread_mutex_destroy(&f->egress.mutex);
    
    //Try locking and unlocking all mutexes to wait until last person 
    //relinquishes it
    //Note that it's up to the caller to make sure no one is going to try 
    //locking these mutexes later
    int i;
    for (i = 0; i < MAX_GUVS_PER_FPGA; i++) {
		#ifndef DISABLE_WEIRD_LOCK
        pthread_mutex_lock(&f->logs_mutex[i]);
        pthread_mutex_unlock(&f->logs_mutex[i]);
		#endif
        pthread_mutex_destroy(&f->logs_mutex[i]);
        
        //Slow as hell... there must be a better way...
        //Also, I've gotten into trouble here. The append_log function
        //specifically does not malloc or free or copy anything. However, 
        //this fpga_connection_info cleanup assumes that non-NULL strings
        //should be freed. This is inconsistent. I should fix this 
        int j;
        for (j = 0; j < f->logs[i].l.nlines; j++) {
            if (f->logs[i].l.lines[j] != NULL) free(f->logs[i].l.lines[j]);
        }
        
        //Free up the stuff from the message window
        deinit_msg_win(&f->logs[i]);
    }
    
    free(f);
}

//Thread that manages incoming network data. Stores into fpga->ingress queue
//arg is a pointer to an fpga_connection_info struct
void* fpga_ingress_thread(void *arg) {
    return NULL;
}

//Thread that dispatches data in ingress queue to appropriate linebuffer
//arg is a pointer to an fpga_connection_info struct
void* fpga_log_thread(void *arg) {
    return NULL;
    
}

//Returns string which was displaced by new log. This can be NULL. NOTE: 
//do NOT call this function if you are holding a mutex!
char *append_log(fpga_connection_info *f, int addr, char *log) {
    if (addr < 0 || addr > MAX_GUVS_PER_FPGA) {
        return NULL;
    }
    pthread_mutex_lock(&f->logs_mutex[addr]);
    msg_win *m = f->logs + addr;
    char *ret = msg_win_append(m, log);
    pthread_mutex_unlock(&f->logs_mutex[addr]);
    
    return ret;
}

//Duplicates string in name and saves it into d. If name was previously set, it
//will be freed
void dbg_guv_set_name(dbg_guv *d, char *name) {
	msg_win *m = get_msg_win(d);
    msg_win_set_name(m, name);
}

//Returns number of bytes added into buf. Not really safe since there is no
//way (currently) to avoid writing too much into buf. Should probably try
//to improve this... returns -1 on error
int draw_dbg_guv(dbg_guv *g, char *buf) {
	//Sanity check inputs
	if (g == NULL || buf == NULL) return -1;
	
	int num_written = 0;
	
	//First, draw the message window in the usual way
	//Because draw_msg_win reads from a log, we have to lock the appropriate
	//mutex:
	pthread_mutex_t *mtx = get_msg_win_mutex(g);
	pthread_mutex_lock(mtx);
	//Actually draw the message window
	msg_win *m = get_msg_win(g);
	int incr = draw_msg_win(m, buf);
	pthread_mutex_unlock(mtx);
	//Cheeky hack: if draw_msg_win returned 0 (or negative) then there is 
	//nothing for us to draw and we return early
	if (incr <= 0) {
		return incr;
	}
	
	buf += incr;
	num_written += incr;
	
	//Now we simply overwrite three characters on the message window to 
	//indicate our status. It's not the most efficient, but it's not really
	//that bad
    incr = cursor_pos_cmd(buf, m->x + m->w-1 -3, m->y);
    num_written += incr;
    buf += incr;
    
    if (g->values_unknown) {
		*buf++ = '?';
		*buf++ = '?';
		*buf++ = '?';
		num_written += 3;
	} else {
		*buf++ = g->keep_pausing ? 'P' : '-';
		*buf++ = g->keep_logging ? 'L' : (g->log_cnt > 0 ? 'l' : '-');
		*buf++ = g->keep_dropping ? 'D' : (g->drop_cnt > 0 ? 'd' : '-');
		num_written += 3;
    }
    
    return num_written;
}
