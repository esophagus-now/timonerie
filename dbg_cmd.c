#include <stdlib.h> //For strtoul
#include <ctype.h> //For isspace
#include <limits.h> //For UINT_MAX
#include <stdio.h> //sscanf, which is not in string.h for some reason
#include <string.h>
#include "dbg_guv.h" //For MAX_GUVS_PER_FPGA
#include "dbg_cmd.h"

#define stringify(x) #x

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

static int skip_whitespace(dbg_cmd *dest, char const *str) {
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
    
    return num_read;
}

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
    
    int rc = skip_whitespace(dest, str);
    num_read += rc;
    str += rc;
    
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

static int parse_open_cmd(dbg_cmd *dest, char const *str) {
    //Sanity check on inputs
    if (dest == NULL) {
        return -2; //This is all we can do
    } else if (str == NULL) {
        dest->error_str = DBG_CMD_NULL_PTR;
        return -1;
    } 
    
    int num_read;
    int rc = sscanf(str, "%*s%*s%*s%n", 
        MAX_STR_PARAM_SIZE, dest->id,
        MAX_STR_PARAM_SIZE, dest->node,
        MAX_STR_PARAM_SIZE, dest->serv,
        &num_read
    );
    
    if (rc != 3) {
        dest->error_str = DBG_CMD_OPEN_USAGE;
        return -1;
    }
    
    str += num_read;
    
    rc = parse_eos(dest, str);
    if (rc < 0) {
        return -1; //dest->error_str already set
    }
    num_read += rc;
    
    dest->type = CMD_OPEN;
    dest->error_str = DBG_CMD_SUCCESS;
    return num_read;
}

static int parse_close_cmd(dbg_cmd *dest, char const *str) {
    //Sanity check on inputs
    if (dest == NULL) {
        return -2; //This is all we can do
    } else if (str == NULL) {
        dest->error_str = DBG_CMD_NULL_PTR;
        return -1;
    } 
    
    int num_read;
    int rc = sscanf(str, "%" stringify(MAX_STR_PARAM_SIZE) "s%n", 
        dest->id,
        &num_read
    );
    
    if (rc != 1) {
        dest->error_str = DBG_CMD_OPEN_USAGE;
        return -1;
    }
    
    str += num_read;
    
    rc = parse_eos(dest, str);
    if (rc < 0) {
        return -1; //dest->error_str already set
    }
    num_read += rc;
    
    dest->type = CMD_CLOSE;
    dest->error_str = DBG_CMD_SUCCESS;
    return num_read;
}

static int parse_sel_cmd(dbg_cmd *dest, char const *str) {
    //Sanity check on inputs
    if (dest == NULL) {
        return -2; //This is all we can do
    } else if (str == NULL) {
        dest->error_str = DBG_CMD_NULL_PTR;
        return -1;
    } 
    
    //Try parsing identifier
    int num_read = 0;
    int incr;
    int rc = sscanf(str, "%" stringify(MAX_STR_PARAM_SIZE) "s%n", 
        dest->id,
        &incr
    );
    
    if (rc != 1) {
        dest->error_str = DBG_CMD_OPEN_USAGE;
        return -1;
    }
    
    num_read += incr;
    str += incr;
    
    rc = skip_whitespace(dest, str);
    num_read += rc;
    str += rc;
    
    if (*str == '[') {
        str++;
        //This is an fpga[guv_addr] command
        //Try parsing dbg_guv address
        rc = sscanf(str, "%d]%n", &dest->dbg_guv_addr, &incr);
        if (rc != 1) {
            dest->error_str = DBG_CMD_OPEN_USAGE;
            return -1;
        }
        
        num_read += incr;
        str += incr;
    }
    
    rc = parse_eos(dest, str);
    if (rc < 0) {
        return -1; //dest->error_str already set
    }
    num_read += rc;
    
    dest->type = CMD_CLOSE;
    dest->error_str = DBG_CMD_SUCCESS;
    return num_read;
}

static int parse_name_cmd(dbg_cmd *dest, char const *str) {
    //Sanity check on inputs
    if (dest == NULL) {
        return -2; //This is all we can do
    } else if (str == NULL) {
        dest->error_str = DBG_CMD_NULL_PTR;
        return -1;
    } 
    
    int num_read;
    int rc = sscanf(str, "%" stringify(MAX_STR_PARAM_SIZE) "s%n", 
        dest->id,
        &num_read
    );
    
    if (rc != 1) {
        dest->error_str = DBG_CMD_OPEN_USAGE;
        return -1;
    }
    
    str += num_read;
    
    rc = parse_eos(dest, str);
    if (rc < 0) {
        return -1; //dest->error_str already set
    }
    num_read += rc;
    
    dest->type = CMD_NAME;
    dest->error_str = DBG_CMD_SUCCESS;
    return num_read;
}

//A lot of my commands just set dest->type and make sure no arguments were
//given
#define make_simple_parse_fn(CMD) \
static int parse_##CMD (dbg_cmd *dest, char const *str) { \
    /*Sanity check on inputs*/ \
    if (dest == NULL) { \
        return -2; \
    } else if (str == NULL) { \
        dest->error_str = DBG_CMD_NULL_PTR; \
        return -1; \
    } \
    int num_read = parse_eos(dest, str); \
    if (num_read < 0) { \
        return -1; \
    } \
    dest->type = CMD ; \
    dest->error_str = DBG_CMD_SUCCESS; \
    return num_read; \
} \
struct dbg_cmd_unused ## __LINE__ {}
//^^That last line is an UNFORGIVABLE hack to swallow the semicolon
make_simple_parse_fn(CMD_DESEL);
make_simple_parse_fn(CMD_SHOW);
make_simple_parse_fn(CMD_HIDE);
make_simple_parse_fn(CMD_UP);
make_simple_parse_fn(CMD_DOWN);
make_simple_parse_fn(CMD_LEFT);
make_simple_parse_fn(CMD_RIGHT);
make_simple_parse_fn(CMD_QUIT);

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
    
    
    //Check which command this is
    char cmd[16];
    sscanf(str, "%15s", cmd);
    //For now, check for special dummy command
    if (!strcmp(cmd, "dummy")) {
        dest->type = CMD_DUMMY;
        return 0;
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
