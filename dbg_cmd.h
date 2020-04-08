#ifndef DBG_CMD_H
#define DBG_CMD_H 1
#include "dbg_guv.h"
#include "symtab.h"

//The trick here is that the register names will match to the correct
//register address in the enum.
#define DBG_GUV_REG_IDENTS \
    X(DROP_CNT      ),\
    X(LOG_CNT       ),\
    X(INJ_TDATA     ),\
    X(INJ_TVALID    ),\
    X(INJ_TLAST     ),\
    X(INJ_TKEEP     ),\
    X(INJ_TDEST     ),\
    X(INJ_TID       ),\
    X(KEEP_PAUSING  ),\
    X(KEEP_LOGGING  ),\
    X(KEEP_DROPPING ),\
    X(DUT_RESET     ),\
    X(UNUSED_12        ),\
    X(UNUSED_13        ),\
    X(UNUSED_14        ),\
    X(LATCH            ),\
    /*These next commands are for timonerie rather than a dbg_guv*/\
    X(CMD_DUMMY),\
    X(CMD_OPEN),\
    X(CMD_CLOSE),\
    X(CMD_SEL),\
    X(CMD_DESEL),\
    X(CMD_NAME),\
    X(CMD_SHOW),\
    X(CMD_HIDE),\
    X(CMD_UP),\
    X(CMD_DOWN),\
    X(CMD_LEFT),\
    X(CMD_RIGHT),\
    X(CMD_QUIT),\
    X(CMD_INFO),
    

#define X(x) x
typedef enum _dbg_cmd_type {
    DBG_GUV_REG_IDENTS
} dbg_cmd_type;
#undef X

extern char const *DBG_GUV_REG_NAMES[];

#define MAX_STR_PARAM_SIZE 64
typedef struct _dbg_cmd {
    dbg_cmd_type type;
    
    //Fields used by parsed command. The ones which are used depends on the
    //dbg_cmd_type
    char id[MAX_STR_PARAM_SIZE + 1]; //Identifier
    unsigned dbg_guv_addr;
    int has_guv_addr; //The "sel" command can be fore an FPGA or a dbg_guv
    unsigned reg_addr;
    int has_param; //Some dbg_guv register commadns have a parameter, and some don't
    unsigned param;
    char node[MAX_STR_PARAM_SIZE + 1]; //The hostname...
    char *serv[MAX_STR_PARAM_SIZE + 1]; //...and port (service) number for opening connections
    
    //Error information
    char const *error_str;
    int error_pos;
    char smoking_gun;
} dbg_cmd;

typedef int parse_fn(dbg_cmd *dest, char const *str);

//Attempts to parse str containing a dbg_guv command. Fills dbg_cmd
//pointed to by dest. On error, returns negative and fills dest->error_str
//(unless dest is NULL, of course). Otherwise returns 0.
int parse_dbg_cmd(dbg_cmd *dest, char const *str);


//////////////////////////////////////////////////
//Error codes, which double as printable strings//
//////////////////////////////////////////////////

extern char const *const DBG_CMD_SUCCESS        ; //    = "successfully parsed dbg_guv cmd";
extern char const *const DBG_CMD_ADDR_RANGE        ; //    = "dbg_guv address out of range";
extern char const *const DBG_CMD_BAD_REG        ; //    = "Bad register code";
extern char const *const DBG_CMD_EXP_OP            ; //    = "Expected another operand";
extern char const *const DBG_CMD_TOO_MANY_OPS    ; //    = "Too many operands given";
extern char const *const DBG_CMD_UNEX            ; //    = "Unexpected character";
extern char const *const DBG_CMD_BAD_PARAM        ; //    = "Malformed parameter value";
extern char const *const DBG_CMD_NULL_PTR        ; //    = "NULL pointer passed to dbg_cmd parse";
extern char const *const DBG_CMD_IMPOSSIBLE        ; //    = "The dbg_cmd code somehow reached an area Marco thought was impossible";
extern char const *const DBG_CMD_NOT_IMPL        ; //    = "This function is not implement";
extern char const *const DBG_CMD_REDEF        ; //    = "Identifier is already in use";
extern char const *const DBG_CMD_BAD_CMD        ; //    = "No such command";
extern char const *const DBG_CMD_OPEN_USAGE        ; //    = "Usage: open fpga_name hostname port";
extern char const *const DBG_CMD_SEL_USAGE        ; //    = "Usage: sel (fpga_name[guv_addr] | guv_name)";

#endif
