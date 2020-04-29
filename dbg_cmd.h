#ifndef DBG_CMD_H
#define DBG_CMD_H 1
#include "symtab.h"
#include "dbg_guv.h"

#define DBG_CMD_IDENTS \
    X(CMD_DUMMY),\
    X(CMD_OPEN),\
    X(CMD_CLOSE),\
    X(CMD_SEL),\
    X(CMD_MGR),\
    X(CMD_NAME),\
    X(CMD_DBG_REG),\
    X(CMD_MSG),\
    X(CMD_QUIT),\
    X(CMD_HANDLED)

#define X(x) x
typedef enum _dbg_cmd_type {
    DBG_CMD_IDENTS
} dbg_cmd_type;
#undef X

extern char const *DBG_CMD_NAMES[];

#define MAX_STR_PARAM_SIZE 64
typedef struct _dbg_cmd {
    dbg_cmd_type type;
    
    //Fields used by parsed command. The ones which are used depends on the
    //dbg_cmd_type
    dbg_reg_type reg;
    char id[MAX_STR_PARAM_SIZE + 1]; //Identifier
    unsigned dbg_guv_addr;
    int has_guv_addr; //The "sel" command can be fore an FPGA or a dbg_guv
    int has_param; //Some dbg_guv register commadns have a parameter, and some don't
    unsigned param;
    char node[MAX_STR_PARAM_SIZE + 1]; //The hostname...
    char serv[MAX_STR_PARAM_SIZE + 1]; //...and port (service) number for opening connections
    
    //Error information
    char const *error_str;
    int error_pos;
    char smoking_gun;
} dbg_cmd;

typedef int parse_fn(dbg_cmd *dest, char const *str);

//Functions for each terminal in the grammar
//Each returns the number of bytes read from str. On error, return -1 and
//set dest->error_str appropriately

int skip_whitespace(dbg_cmd *dest, char const *str);

//This is more of a helper wrapper around sscanf
int parse_strn(char *buf, int n, char const *str);

int parse_dbg_guv_addr(dbg_cmd *dest, char const *str);

int parse_param(dbg_cmd *dest, char const *str);

int parse_action(dbg_cmd *dest, char const *str);

int parse_eos (dbg_cmd *dest, char const *str);

//Just needed to export this one to let the timoniers use it
int parse_dbg_reg_cmd(dbg_cmd *dest, char const *str);

//Attempts to parse str containing a dbg_guv command. Fills dbg_cmd
//pointed to by dest. On error, returns negative and fills dest->error_str
//(unless dest is NULL, of course). On success returns 0.
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
extern char const *const DBG_CMD_CLOSE_USAGE        ; //    = "Usage: close fpga_name";
extern char const *const DBG_CMD_SEL_USAGE        ; //    = "Usage: sel (fpga_name[guv_addr] | guv_name)";
extern char const *const DBG_CMD_MGR_USAGE        ; //    = "Usage: mgr (int | fio)";
extern char const *const DBG_CMD_NAME_USAGE        ; //    = "Usage: name guv_name";

#endif
