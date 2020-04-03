#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dbg_guv.h"
#include "textio.h"

//Little helper functions
static inline msg_win *get_msg_win(dbg_guv *g) {
	fpga_connection_info *f = g->parent;
	return &f->logs[g->addr];
}

static inline pthread_mutex_t *get_msg_win_mutex(dbg_guv *g) {
	fpga_connection_info *f = g->parent;
	return &f->logs_mutex[g->addr];
}

fpga_connection_info *new_fpga_connection(char *node, char *serv) {
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
        
        ret->guvs[i].parent = ret;
        ret->guvs[i].addr = i;
    }
    
    pthread_mutex_init(&ret->ingress.mutex, NULL);
    pthread_mutex_init(&ret->egress.mutex, NULL);
    
    //TODO: open up socket and spin up network management threads
    
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
        //Also, I've gotten into trouble here. The append_log function
        //specifically does not malloc or free or copy anything. However, 
        //this fpga_connection_info cleanup assumes that non-NULL strings
        //should be freed. This is inconsistent. I should fix this 
        int j;
        for (j = 0; j < f->logs[i].l.nlines; j++) {
            if (f->logs[i].l.lines[j] != NULL) free(f->logs[i].l.lines[j]);
        }
        
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
    
    *buf++ = g->keep_pausing ? 'P' : '-';
    *buf++ = g->keep_logging ? 'L' : (g->log_cnt > 0 ? 'l' : '-');
    *buf++ = g->keep_dropping ? 'D' : (g->drop_cnt > 0 ? 'd' : '-');
    num_written += 3;
    
    return num_written;
}
