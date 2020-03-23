#ifndef DBG_GUV_H
#define DBG_GUV_H 1

#include <pthread.h>
#include "queue.h"

//This is essentially a circular buffer, but there is only one writer which
//just constantly overwrites old data. Also, the reader can read whatever they
//want, usually at some fixed offset from pos
#define SCROLLBACK 1000
typedef struct _linebuf {
    char *lines[SCROLLBACK];
    int pos;
} linebuf;

#define MAX_GUVS_PER_FPGA 32
#define LOG_IN_MEM_SZ 4096
typedef struct _fpga_connection_info {
    //Logs for each governor. This uses a ping-pong approach; when the second 
    //buffer fills up, the first one is written to disk. By doing this 
    //carefully, we always ensure a minimum amount of scrollback. And if we 
    //really wanted to be tricky, we could reload things from disk when needed
    linebuf logs[MAX_GUVS_PER_FPGA];
    pthread_mutex_t logs_mutex[MAX_GUVS_PER_FPGA];
    
    struct sockaddr *addr;
    int addr_len;
    int sfd;
    int sfd_state;
    
    queue ingress;    
    queue egress;
} fpga_connection_info;

//This struct contains all the state associated with displaying dbg_guv
// information.
typedef struct _dbg_guv {
    //Display information
    char *name;
    int x, y; 
    int w, h; //Minimum: 6 by 6?
    int buf_offset; //Where to start reading from linebuffer
    int need_redraw;
    
    //Mirror registers in hardware
    unsigned keep_pausing;
    unsigned keep_logging;
    unsigned log_cnt;
    unsigned keep_dropping;
    unsigned drop_cnt;
    unsigned inj_TDATA;
    unsigned inj_TVALID;
    unsigned inj_TKEEP;
    unsigned inj_TLAST;
    unsigned inj_TDEST;
    unsigned inj_TID;
    
    //Address information for this dbg_guv
    fpga_connection_info *parent;
    int addr; 
} dbg_guv;

//At the moment, does not try to connect to the FPGA or start any threads
fpga_connection_info *new_fpga_connection(char *node, char *serv);

void del_fpga_connection(fpga_connection_info *f);

//Returns string which was displaced by new log. This can be NULL. NOTE: 
//do NOT call this function if you are holding a mutex! By the way, does NOT
//make an internal copy of the string; you must copy it yourself if this is
//desired
char *append_log(fpga_connection_info *f, int addr, char *log);

dbg_guv* new_dbg_guv(char *name);

void del_dbg_guv(dbg_guv *d);

//Duplicates string in name and saves it into d. If name was previously set, it
//will be freed
void dbg_guv_set_name(dbg_guv *d, char *name);

//Returns number of bytes added into buf. Not really safe, should probably try
//to improve this...
int draw_dbg_guv(dbg_guv *g, char *buf);

#endif
