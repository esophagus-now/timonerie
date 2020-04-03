#ifndef DBG_GUV_H
#define DBG_GUV_H 1

#include <pthread.h>
#include "queue.h"
#include "textio.h"

//To fix circular definition of dbg_guv and fpga_connection_info
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
    
    //Before you get your first command receipt, we don't know what state 
    //the guvs are in
    int values_unknown;
    
    //Address information for this dbg_guv
    struct _fpga_connection_info *parent;
    int addr; 
} dbg_guv;

#define MAX_GUVS_PER_FPGA 32
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
     
    //Other threads can enqueue dbg_guv command messages on egress queue
    queue egress;
    //This is the thread ID for the thread that empties egress and sends 
    //commands to the FPGA
    pthread_t net_tx;
    int net_tx_started;
} fpga_connection_info;

typedef struct _new_fpga_cb_info {
    //If a connection was succesfully opened, a pointer to the allocated
    //fpga_connection_info is stored here.
    fpga_connection_info *f;
    
    //Otherwise, ret is NULL and here is some error information
    char const *error_str;
    
    //User can put whatever they want here
    void *user_data;
} new_fpga_cb_info;

typedef void new_fpga_cb(new_fpga_cb_info info);

//Idea: this function should take a callback as an argument. This callback
//will be called from inside a new thread once the socket is connected (or
//some kind of error occurs)
//Then, in the main thread, we simply maintain a list of active file 
//descriptors and run an event loop using poll(). Of course, if we were a
//little smarter about having, say, four threads working together, then it
//might have better performance. But that's out of scope for what we need
//here.
//Aaaaanyway, the main thread uses the callbacks to maintain that list of
//fds to poll on. Also, it is this thread that needs the smarts to deal with
//command receipts
int new_fpga_connection(new_fpga_cb *cb, char *node, char *serv, void *user_data);

//Cleans up an FPGA connection. Note that it will block as it waits for 
//threads to close; do NOT call while holding a mutex!
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


///////////////////////////////////////////////////////
//Error strings, whose pointers double as error codes//
///////////////////////////////////////////////////////
extern char const *const DBG_GUV_SUCC; //= "success";
extern char const *const DBG_GUV_NULL_ARG; //= "received NULL argument";
extern char const *const DBG_GUV_NULL_CB; //= "received NULL callback";
extern char const *const DBG_GUV_NULL_CONN_INFO; //= "received NULL fpga_connection_info";

#endif
