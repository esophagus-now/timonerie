#ifndef DBG_GUV_H
#define DBG_GUV_H 1

#include <pthread.h>
#include <event2/event.h>
#include "queue.h"
#include "textio.h"
#include "twm.h"
#include "dbg_cmd.h"

struct _dbg_cmd; //This is a more serious circular reference... fix?

struct _dbg_guv; //Fix circular reference

//If the user types in a command which is not one of the builtins, the 
//entire command is instead sent to the focused dbg_guv window. The dbg_cmd
//struct is used in case this timonier wants to
//use one of the builtin functions (and for returning errors)
typedef int guv_got_line_fn(struct _dbg_guv *owner, char const *str, struct _dbg_cmd *dest);

//A timonier may choose to occupy some of the lines inside of a dbg_guv 
//window, and smart timoniers may use fewer lines if the window is smaller
typedef int lines_req_fn(struct _dbg_guv *owner, int w, int h);

typedef struct _guv_operations {
    guv_got_line_fn *got_line;
    lines_req_fn *lines_req;
    draw_operations draw_ops;
} guv_operations;

//To fix circular definition of dbg_guv and fpga_connection_info
struct _fpga_connection_info;

#define DBG_GUV_SCROLLBACK 1000
//This struct contains all the state associated with displaying dbg_guv
// information.
typedef struct _dbg_guv {
    //Stores messages received from FGPAs
    linebuf logs;
    pthread_mutex_t logs_mutex; //The logs can be accessed from more than one thread
    int log_pos;
    char *name;
    int need_redraw;
    
    //Before you get your first command receipt, we don't know what state 
    //the guvs are in
    int values_unknown;
    
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
    
    //Additional information from command receipt
    unsigned inj_failed;
    unsigned dout_not_rdy_cnt;
    
    //The user can select one of several modes for operating the dbg_guv.
    //This is done by passing a set of function pointers into the dbg_guv
    //struct that will get triggered at various times
    guv_operations ops;
    void *manager;
    
    //Address information for this dbg_guv
    struct _fpga_connection_info *parent;
    int addr; 
    
    //Error information
    char const *error_str;
} dbg_guv;

#define MAX_GUVS_PER_FPGA 32
#define FCI_BUF_SIZE 512
typedef struct _fpga_connection_info {    
    //For each dbg_guv, keep a local mirror of its control regs. These 
    //structs also contain the log buffer
    dbg_guv guvs[MAX_GUVS_PER_FPGA];
    
    //Network connection info
    struct sockaddr *addr;
    int addr_len;
    int sfd;
    struct event *ev; //Event for reading input
    
    //General-purpose buffer, but I only use it for network ingress data
    char buf[FCI_BUF_SIZE];
    int buf_pos;
    
    //Other threads can enqueue dbg_guv command messages on egress queue
    queue egress;
    //This is the thread ID for the thread that empties egress and sends 
    //commands to the FPGA
    pthread_t net_tx;
    int net_tx_started;
    
    //Error information
    char const* error_str;
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

//An "iterator" to a dbg_guv or an fpga_connection_info. If f is NULL, this
//iterator is considered invalid. If addr is -1, this iterator is considered
//to be pointing to the fpga_connection_info. 
typedef struct _dbg_guv_it {
    fpga_connection_info *f;
    int addr;
} dbg_guv_it;

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

//TODO: runtime sizes for the stream
int read_fpga_connection(fpga_connection_info *f, int fd, int addr_w);

//Duplicates string in name and saves it into d. If name was previously set, it
//will be freed
void dbg_guv_set_name(dbg_guv *d, char *name);

//Returns number of bytes added into buf, or -1 on error.
int draw_fn_dbg_guv(void *item, int x, int y, int w, int h, char *buf);

//Returns how many bytes are needed (can be an upper bound) to draw dbg_guv
//given the size
int draw_sz_dbg_guv(void *item, int w, int h);

//Tells us that we should redraw, probably because we moved to another
//area of the screen
void trigger_redraw_dbg_guv(void *item);

//Simply scrolls the dbg_guv; positive for up, negative for down. A special
//check in this function, along with a more robust check in draw_linebuf, 
//make sure that you won't read out of bounds.
void dbg_guv_scroll(dbg_guv *d, int amount);

extern draw_operations const dbg_guv_draw_ops;

///////////////////////////////////////////////////////
//Error strings, whose pointers double as error codes//
///////////////////////////////////////////////////////
extern char const *const DBG_GUV_SUCC; //= "success";
extern char const *const DBG_GUV_NULL_ARG; //= "received NULL argument";
extern char const *const DBG_GUV_NULL_CB; //= "received NULL callback";
extern char const *const DBG_GUV_NULL_CONN_INFO; //= "received NULL fpga_connection_info";
extern char const *const DBG_GUV_CNX_CLOSED; //= "external host closed connection";

#endif
