#ifndef DBG_GUV_H
#define DBG_GUV_H 1

#include <pthread.h>
#include "queue.h"
#include "textio.h"

//To fix circular definitions
struct _fpga_connection_info;

//This struct contains all the state associated with displaying dbg_guv
// information.
typedef struct _dbg_guv {    
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
    unsigned dut_reset;
    
    //Address information for this dbg_guv
    struct _fpga_connection_info *parent;
    int addr; 
} dbg_guv;

#define MAX_GUVS_PER_FPGA 32
#define LOG_IN_MEM_SZ 4096
typedef struct _fpga_connection_info {
	//Place to save logs
    msg_win logs[MAX_GUVS_PER_FPGA];
    pthread_mutex_t logs_mutex[MAX_GUVS_PER_FPGA];
    
    //For each dbg_guv, keep a local mirror of its control regs
    dbg_guv guvs[MAX_GUVS_PER_FPGA];
    
    //Network connection info
    struct sockaddr *addr;
    int addr_len;
    int sfd;
    int sfd_state;
    
    //(This may disappear) network management threads can enqueue onto
    //ingress queue
    queue ingress;    
    //Other threads can enqueue dbg_guv command messages on egress queue
    queue egress;
} fpga_connection_info;

//At the moment, does not try to connect to the FPGA or start any threads
fpga_connection_info *new_fpga_connection(char *node, char *serv);

//Cleans up an FPGA connection. Note that it will block as it waits for 
//threads to close; do NOT call while holdign a mutex!
void del_fpga_connection(fpga_connection_info *f);

//Returns string which was displaced by new log. This can be NULL. NOTE: 
//do NOT call this function if you are holding a mutex! By the way, does NOT
//make an internal copy of the string; you must copy it yourself if this is
//desired
char *append_log(fpga_connection_info *f, int addr, char *log);

//Duplicates string in name and saves it into d. If name was previously set, it
//will be freed
void dbg_guv_set_name(dbg_guv *d, char *name);

//Returns number of bytes added into buf. Not really safe, should probably try
//to improve this...
int draw_dbg_guv(dbg_guv *g, char *buf);

#endif
