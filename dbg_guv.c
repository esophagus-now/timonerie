#include "dbg_guv.h"
#include "textio.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fpga_connection_info *new_fpga_connection(char *node, char *serv) {
    fpga_connection_info *ret = malloc(sizeof(fpga_connection_info));
    memset(&ret->logs, 0, sizeof(ret->logs));
    int i;
    for (i = 0; i < MAX_GUVS_PER_FPGA; i++) {
        pthread_mutex_init(&ret->logs_mutex[i], NULL);
        ret->guvs[i] = new_dbg_guv(NULL);
    }
    
    pthread_mutex_init(&ret->ingress.mutex, NULL);
    pthread_mutex_init(&ret->egress.mutex, NULL);
    return ret;
}


void del_fpga_connection(fpga_connection_info *f) {
    //Gracefully quit early if f is NULL
    if (f == NULL) return;
    
#ifndef DISABLE_WEIRD_LOCK
    pthread_mutex_lock(&f->egress.mutex);
    pthread_mutex_unlock(&f->egress.mutex);
    pthread_mutex_lock(&f->ingress.mutex);
    pthread_mutex_unlock(&f->ingress.mutex);
#endif
    pthread_mutex_destroy(&f->egress.mutex);
    pthread_mutex_destroy(&f->ingress.mutex);
    
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
        int j;
        for (j = 0; j < SCROLLBACK; j++) {
            if (f->logs[i].lines[j] != NULL) free(f->logs[i].lines[j]);
        }
        
        if (f->guvs[i] != NULL) free(f->guvs[i]);
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

//Thread that manages outgoing network data. Empties fpga->egress queue
//arg is a pointer to an fpga_connection_info struct
void* fpga_egress_thread(void *arg) {
    return NULL;
    
}

//Returns string which was displaced by new log. This can be NULL. NOTE: 
//do NOT call this function if you are holding a mutex!
char *append_log(fpga_connection_info *f, int addr, char *log) {
    if (addr < 0 || addr > MAX_GUVS_PER_FPGA) {
        return NULL;
    }
    pthread_mutex_lock(&f->logs_mutex[addr]);
    linebuf *l = f->logs + addr;
    char *ret = l->lines[l->pos];
    l->lines[l->pos++] = log;
    if (l->pos == SCROLLBACK) l->pos = 0;
    pthread_mutex_unlock(&f->logs_mutex[addr]);
    
    return ret;
}

dbg_guv* new_dbg_guv(char *name) {
    dbg_guv *ret = malloc(sizeof(dbg_guv));
    memset(ret, 0, sizeof(dbg_guv));
    
    if (name != NULL) {
        ret->name = strdup(name);
    }
    ret->w = 6;
    ret->h = 6;
    
    return ret;
}

void del_dbg_guv(dbg_guv *d) {
    if (d != NULL) {
        if (d->name != NULL) free(d->name);
        free(d);
    }
}

//Duplicates string in name and saves it into d. If name was previously set, it
//will be freed
void dbg_guv_set_name(dbg_guv *d, char *name) {
    if (d->name != NULL) free(d->name);
    d->name = strdup(name);
}

//Returns number of bytes added into buf. Not really safe, should probably try
//to improve this... returns -1 on error
int draw_dbg_guv(dbg_guv *g, char *buf) {
    //Check if we need a redraw
    if (g->need_redraw == 0) return 0;
    if (g->w < 12 || g->h < 6) {
        return -1;
    }
    char *buf_saved = buf;
    //Draw top row
    //Move to top-left
    int incr = cursor_pos_cmd(buf, g->x, g->y);
    buf += incr;
    int len;
    sprintf(buf, "+%.*s-%n",
        g->w - 6,
        g->name,
        &len
    );
    buf += len;
    int i;
    for (i = len; i < g->w - 4; i++) *buf++ = '-';
    sprintf(buf, "%c%c%c+",
        g->keep_pausing ? 'P' : '-',
        g->keep_logging ? 'L' : (g->log_cnt > 0 ? 'l' : '-'),
        g->keep_dropping ? 'D' : (g->drop_cnt > 0 ? 'd' : '-')
    );
    buf += 4;
    
    //This method is "more efficient", but not well supported
    //sprintf(buf, "+-\x1b[%db+%n", g->w - 3, &incr);
    //buf += incr;
    
    //Draw logs and box edges
    //First get a handle to the line buffer for this dbg_guv
    linebuf *l = &(g->parent->logs[g->addr]);
    //Grab mutex for this buffer while we read it
    pthread_mutex_lock(&(g->parent->logs_mutex[g->addr]));
    for (i = g->h-2-1; i >= 0; i--) {
        //Move the cursor
        incr = cursor_pos_cmd(buf, g->x, g->y + 1 + (g->h-2-1 - i)); //I hope there's no OBOE
        buf += incr;
        
        //Compute index into line buffer's scrollback 
        int ind = l->pos - 1 - g->buf_offset - i;
        //wrap into the right range (it's circular buffer)
        ind = (ind + SCROLLBACK) % SCROLLBACK;
        
        //Construct the string that we will print
        sprintf(buf, "|%-*.*s|%n", g->w-2, g->w-2, l->lines[ind], &incr);
        buf += incr;        
    }
    pthread_mutex_unlock(&(g->parent->logs_mutex[g->addr]));
    
    
    //Draw bottom row
    //Move to bottom-left
    incr = cursor_pos_cmd(buf, g->x, g->y + g->h - 1);
    buf += incr;
    *buf++ = '+';
    for (i = 1; i < g->w - 1; i++) *buf++ = '-';
    *buf++ = '+';
    //sprintf(buf, "+-\x1b[%db+%n", g->w - 3, &incr);
    //buf += incr;
    
    g->need_redraw = 0;
    return buf - buf_saved;
}
