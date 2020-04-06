#include <stdlib.h> //For strtoul
#include <ctype.h> //For isspace
#include <limits.h> //For UINT_MAX
#include "dbg_guv.h" //For MAX_GUVS_PER_FPGA
#include "dbg_cmd.h"

#define X(x) #x
char const *DBG_GUV_REG_NAMES[] = {
    DBG_GUV_REG_IDENTS
};
#undef X

char const *const DBG_CMD_SUCCESS            = "successfully parsed dbg_guv cmd";
char const *const DBG_CMD_ADDR_RANGE        = "dbg_guv address out of range";
char const *const DBG_CMD_BAD_REG            = "Bad register code";
char const *const DBG_CMD_EXP_OP            = "Expected another operand";
char const *const DBG_CMD_TOO_MANY_OPS        = "Too many operands given";
char const *const DBG_CMD_UNEX                = "Unexpected character";
char const *const DBG_CMD_BAD_PARAM            = "Malformed parameter value";
char const *const DBG_CMD_NULL_PTR            = "NULL pointer passed to dbg_cmd parse";
char const *const DBG_CMD_IMPOSSIBLE        = "The dbg_cmd code somehow reached an area Marco thought was impossible";

/* GRAMMAR:
 * 
 * command : dbg_guv_addr action EOS
 *            ;
 * 
 * action  : 'c'
 *            | cmd_code param
 *            ;
 * 
 * */


//Functions for each terminal in the grammar
//Each returns the number of bytes read from str. On error, return -1 and
//set dest->error_str appropriately

static int parse_dbg_guv_addr(dbg_cmd *dest, char const *str) {
    //Sanity check on inputs
    if (dest == NULL) {
        return -2; //This is all we can do
    } else if (str == NULL) {
        dest->error_str = DBG_CMD_NULL_PTR;
        return -1;
    } 
    
    //Read the value
    char *endptr;
    unsigned long val = strtoul(str, &endptr, 0);
    if (endptr == str) {
        //strtoul failed. Check if this is because of an early EOS
        while (isspace(*str)) str++;
        if (*str == '\0') dest->error_str = DBG_CMD_EXP_OP;
        else dest->error_str = DBG_CMD_UNEX;
        return -1;
    }
    if (val >= MAX_GUVS_PER_FPGA) {
        dest->error_str = DBG_CMD_ADDR_RANGE;
        return -1;
    }
    
    //No need to care about performance; commands are only parsed as fast
    //as the user can type them
    dest->error_str = DBG_CMD_SUCCESS;
    dest->dbg_guv_addr = val; //Safe to truncate
    return (endptr - str);
}

static int parse_param(dbg_cmd *dest, char const *str) {
    //Sanity check on inputs
    if (dest == NULL) {
        return -2; //This is all we can do
    } else if (str == NULL) {
        dest->error_str = DBG_CMD_NULL_PTR;
        return -1;
    } 
    
    //Read the value
    char *endptr;
    unsigned long val = strtoul(str, &endptr, 0);
    if (endptr == str) {
        //strtoul failed. Check if this is because of an early EOS
        while (isspace(*str)) str++;
        if (*str == '\0') dest->error_str = DBG_CMD_EXP_OP;
        else dest->error_str = DBG_CMD_UNEX;
        return -1;
    }
    if (val >= UINT_MAX) {
        dest->error_str = DBG_CMD_BAD_PARAM;
        return -1;
    }
    
    //No need to care about performance; commands are only parsed as fast
    //as the user can type them
    dest->error_str = DBG_CMD_SUCCESS;
    dest->param = val; //Safe to truncate
    return (endptr - str);
}

static int parse_action(dbg_cmd *dest, char const *str) {
    //Sanity check on inputs
    if (dest == NULL) {
        return -2; //This is all we can do
    } else if (str == NULL) {
        dest->error_str = DBG_CMD_NULL_PTR;
        return -1;
    } 
    
    int num_read = 0;
    
    //Skip over whitespace
    while (isspace(*str)) {
        str++;
        num_read++;
    }
    
    int rc;
    
    //Nothing can be done at this point; need to use ugly switch statement
    num_read++;
    switch (*str++) {
    case 'c':
        dest->type = LATCH;
        dest->has_param = 0;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'p':
    case 'P':
        dest->type = KEEP_PAUSING;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'l':
        dest->type = LOG_CNT;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'L':
        dest->type = KEEP_LOGGING;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'd':
        dest->type = DROP_CNT;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'D':
        dest->type = KEEP_DROPPING;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'i':
    case 'I':
        //Move on to next switch statement, which takes care of inject codes
        break;
    case '\0':
        dest->error_str = DBG_CMD_EXP_OP;
        return -1;
    default:
        dest->error_str = DBG_CMD_BAD_REG;
        return -1;
    }
    
    num_read++;
    switch (*str++) {
    case 'd':
    case 'D':
        dest->type = INJ_TDATA;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'v':
    case 'V':
        dest->type = INJ_TVALID;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'k':
    case 'K':
        dest->type = INJ_TKEEP;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'l':
    case 'L':
        dest->type = INJ_TLAST;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 't':
    case 'T':
        dest->type = INJ_TDEST;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'i':
    case 'I':
        dest->type = INJ_TID;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case '\0':
        dest->error_str = DBG_CMD_EXP_OP;
        return -1;
    default:
        dest->error_str = DBG_CMD_BAD_REG;
        return -1;
    }
    
    dest->error_str = DBG_CMD_IMPOSSIBLE;
    dest->smoking_gun = 34;
    return -1;
}

static int parse_eos (dbg_cmd *dest, char const *str) {
    //Sanity check on inputs
    if (dest == NULL) {
        return -2; //This is all we can do
    } else if (str == NULL) {
        dest->error_str = DBG_CMD_NULL_PTR;
        return -1;
    } 
    
    int num_read = 0;
    while (isspace(*str)) {
        str++;
        num_read++;
    }
    num_read++;
    if (*str++ != '\0') {
        dest->error_str = DBG_CMD_TOO_MANY_OPS;
        return -1;
    }
    
    dest->error_str = DBG_CMD_SUCCESS;
    return num_read;
}

//Just for syntax
typedef struct _cmd_info {
    char *cmd;
    parse_fn *fn;
} cmd_info;

static cmd_info builtin_cmds[] = {
    {"open",	NULL},  //Open FPGA connection
    {"close",	NULL},  //Close FPGA connection
    {"sel",	    NULL},  //Select active dbg_guv
    {"desel",	NULL},  //De-select active dbg_guv
    {";",	    NULL},  //Issue a command to active dbg_guv
    {"name",	NULL},  //Rename active dbg_guv
    {"show",	NULL},  //Show active dbg_guv
    {"hide",	NULL},  //Hide active dbg_guv
    {"left",	NULL},  //Move active dbg_guv to the left
    {"right",	NULL},  //Move active dbg_guv to the right
    {"up",	    NULL},  //Move active dbg_guv up
    {"down",	NULL},  //Move active dbg_guv down
};

//Attempts to parse str containing a dbg_guv command. Fills dbg_cmd
//pointed to by dest. On error, returns negative and fills dest->error_str
//(unless dest is NULL, of course). On success returns 0.
int parse_dbg_cmd(dbg_cmd *dest, char const *str) {
    //Sanity check on inputs
    if (dest == NULL) {
        return -2; //This is all we can do
    } else if (str == NULL) {
        dest->error_str = DBG_CMD_NULL_PTR;
        return -1;
    } 
    
    int rc = parse_dbg_guv_addr(dest, str);
    if (rc < 0) return -1; //parse_dbg_guv_addr already set dest->error_str
    str += rc;
    
    rc = parse_action(dest, str);
    if (rc < 0) return -1; //parse_action already set dest->error_str
    str += rc;
    
    rc = parse_eos(dest, str);
    if (rc < 0) return -1; //parse_eos already set dest->error_str
    str += rc;
    
    
    //At this point, all the parsing succeeded. Compute the actual code that
    //should be sent to the dbg_guv's command stream:
    dest->addr = (dest->dbg_guv_addr << 4) | dest->type;
    //dest->param is already set
    dest->error_str = DBG_CMD_SUCCESS;
    return 0;
}
