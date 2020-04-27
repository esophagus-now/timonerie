#ifndef DBG_GUV_H
#define DBG_GUV_H 1

#include <event2/event.h>
#include "queue.h"
#include "textio.h"
#include "twm.h"
#include "dbg_cmd.h"

struct _dbg_cmd; //This is a more serious circular reference... fix?

struct _dbg_guv; //Fix circular reference

//When a new manager is applied to a debug, its init callback is triggered.
//This is optional. Return 0 on success, -1 on error.
typedef int init_mgr_fn(struct _dbg_guv *owner);

//If the user types in a command which is not one of the builtins, the 
//entire command is instead sent to the focused dbg_guv window. The dbg_cmd
//struct is used in case this timonier wants to
//use one of the builtin functions (and for returning errors)
typedef int got_line_fn(struct _dbg_guv *owner, char const *str, struct _dbg_cmd *dest);

//A timonier may choose to occupy some of the lines inside of a dbg_guv 
//window, and smart timoniers may use fewer lines if the window is smaller
typedef int lines_req_fn(struct _dbg_guv *owner, int w, int h);

//A manager can supply this callback if it wishes to be notified about 
//command receipts
typedef int cmd_receipt_fn(struct _dbg_guv *owner, unsigned const *receipt);

//A manager can supply this callback if it wishes to be notified about 
//logs from a dbg_guv
typedef int log_fn(struct _dbg_guv *owner, unsigned const *log);

//Sometimes a manager needs to be triggered periodically. The fast_update
//function, if provided, will be triggered once per iteration of the event
//loop. The slow callback is triggered every 50 ms (incidentally, this is
//performed right before the display is checked for redrawing)
typedef int fast_update_fn(struct _dbg_guv *owner);
typedef int slow_update_fn(struct _dbg_guv *owner);


//Also allow a timonier the chance to clean itself up
typedef void cleanup_mgr_fn(struct _dbg_guv *owner);

typedef struct _guv_operations {
	init_mgr_fn *init_mgr;
    got_line_fn *got_line;
    lines_req_fn *lines_req;
    cmd_receipt_fn *cmd_receipt;
    log_fn *log;
    fast_update_fn *fast_update;
    slow_update_fn *slow_update;
    draw_operations draw_ops;
    cleanup_mgr_fn *cleanup_mgr;
} guv_operations;

//To fix circular definition of dbg_guv and fpga_connection_info
struct _fpga_connection_info;

#define DBG_GUV_SCROLLBACK 1000
//This struct contains all the state associated with displaying dbg_guv
// information.
typedef struct _dbg_guv {
    //Stores messages received from FGPAs
    linebuf logs;
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
    void *mgr;
    
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
    
    //Fields for reading from socket
    struct event *rd_ev; 
    char in_buf[FCI_BUF_SIZE]; //If a message straddles two network 
                               //packets, this buffer will hold on to 
                               //partially received messages.
    int in_buf_pos;            //Position in input buffer.
    
    //Fields for writing to socket
    struct event *wr_ev;
    char out_buf[FCI_BUF_SIZE]; //Ring buffer of data to send on the
                                //socket when it is next available.
    int out_buf_pos, out_buf_len;
    
    //Name used in symbol table
    char *name;
    
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

//Returns a newly allocated and constructed fpga_connection_info struct,
//or NULL on error
//TODO: make dbg_guv widths parameters to this function?
int new_fpga_connection();

//Cleans up an FPGA connection. Gracefully ignores NULL input
void del_fpga_connection(fpga_connection_info *f);

//Returns string which was displaced by new log. This can be NULL. NOTE: 
//do NOT call this function if you are holding a mutex! By the way, does NOT
//make an internal copy of the string; you must copy it yourself if this is
//desired
char *append_log(dbg_guv *d, char *log);

//TODO: runtime sizes for the stream
int read_fpga_connection(fpga_connection_info *f, int fd, int addr_w);

//Tries to write as much of f->out_buf as possible to the socket. This 
//is non-blocking. Follows usual error return values
int write_fpga_connection(fpga_connection_info *f, int fd);

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
extern char const *const DBG_GUV_IMPOSSIBLE; //= "code reached a location that Marco thought was impossible";
extern char const *const DBG_GUV_BAD_ADDRESS; //= "could not resolve address";
extern char const *const DBG_GUV_OOM; //= "out of memory";
extern char const *const DBG_GUV_NOT_ENOUGH_SPACE; //= "not enough room in buffer";

#endif
