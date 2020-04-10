#include <stdlib.h> //For strtoul
#include <ctype.h> //For isspace
#include <limits.h> //For UINT_MAX
#include <stdio.h> //sscanf, which is not in string.h for some reason
#include <string.h>
#include "dbg_guv.h" //For MAX_GUVS_PER_FPGA
#include "dbg_cmd.h"

#define xstr(x) #x
#define stringify(x) xstr(x)

#define X(x) #x
char const *DBG_GUV_REG_NAMES[] = {
    DBG_GUV_REG_IDENTS
};
#undef X


#define X(x) #x
char const *DBG_CMD_NAMES[] = {
    DBG_CMD_IDENTS
};
#undef X

//Functions for each terminal in the grammar
//Each returns the number of bytes read from str. On error, return -1 and
//set dest->error_str appropriately

int skip_whitespace(dbg_cmd *dest, char const *str) {
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

int parse_dbg_guv_addr(dbg_cmd *dest, char const *str) {
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

int parse_param(dbg_cmd *dest, char const *str) {
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

int parse_action(dbg_cmd *dest, char const *str) {
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
        dest->reg = LATCH;
        dest->has_param = 0;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'p':
    case 'P':
        dest->reg = KEEP_PAUSING;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'l':
        dest->reg = LOG_CNT;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'L':
        dest->reg = KEEP_LOGGING;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'd':
        dest->reg = DROP_CNT;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'D':
        dest->reg = KEEP_DROPPING;
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
        dest->reg = INJ_TDATA;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'v':
    case 'V':
        dest->reg = INJ_TVALID;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'k':
    case 'K':
        dest->reg = INJ_TKEEP;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'l':
    case 'L':
        dest->reg = INJ_TLAST;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 't':
    case 'T':
        dest->reg = INJ_TDEST;
        rc = parse_param(dest, str);
        if (rc < 0) return -1; //parse_param has already set error_str
        num_read += rc;
        dest->has_param = 1;
        dest->error_str = DBG_CMD_SUCCESS;
        return num_read;
    case 'i':
    case 'I':
        dest->reg = INJ_TID;
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

int parse_eos (dbg_cmd *dest, char const *str) {
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

//Functions for each non-terminal (i.e. command)

static int parse_open_cmd(dbg_cmd *dest, char const *str) {
    //Sanity check on inputs
    if (dest == NULL) {
        return -2; //This is all we can do
    } else if (str == NULL) {
        dest->error_str = DBG_CMD_NULL_PTR;
        return -1;
    } 
    
    int num_read;
    int rc = sscanf(str, 
        "%" stringify(MAX_STR_PARAM_SIZE) "s"
        "%" stringify(MAX_STR_PARAM_SIZE) "s"
        "%" stringify(MAX_STR_PARAM_SIZE) "s"
        "%n", 
        dest->id,
        dest->node,
        dest->serv,
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
        dest->error_str = DBG_CMD_CLOSE_USAGE;
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
    
    
    int incr = skip_whitespace(dest, str);
    int num_read = incr;
    str += incr;
    
    //Try scanning identifier
    //I couldn't get sscanf to do what I needed, so I coded up this dinky
    //"%s" parser
    int i;
    for (i = 0; i < MAX_STR_PARAM_SIZE; i++) {
        char c = *str;
        if (!isgraph(c) || c == '[') break;
        dest->id[i] = c;
        str++;
        num_read++;
    }
    
    if (i == 0) {
        dest->error_str = DBG_CMD_SEL_USAGE;
        return -1;
    }
    
    dest->id[i] = '\0';
    
    if (*str == '[') {
        str++;
        //This is an fpga[guv_addr] command
        //Try parsing dbg_guv address
        incr = parse_dbg_guv_addr(dest, str);
        if (incr < 0) {
            return -1; //dest->error_str already set
        }
        
        dest->has_guv_addr = 1;
        num_read += incr;
        str += incr;
    } else {
        dest->has_guv_addr = 0;
    }
    
    incr = skip_whitespace(dest, str);
    num_read += incr;
    str += incr;
    
    //Eat closing square bracket if one was given. I won't complain to much
    //if no one gives it, though
    if (*str == ']') {
        str++;
        num_read++;
    }
    
    incr = parse_eos(dest, str);
    if (incr < 0) {
        return -1; //dest->error_str already set
    }
    num_read += incr;
    
    dest->type = CMD_SEL;
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
        dest->error_str = DBG_CMD_NAME_USAGE;
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

int parse_dbg_reg_cmd(dbg_cmd *dest, char const *str) {
    //Sanity check on inputs
    if (dest == NULL) {
        return -2; //This is all we can do
    } else if (str == NULL) {
        dest->error_str = DBG_CMD_NULL_PTR;
        return -1;
    } 
    
    int rc = parse_action(dest, str);
    if (rc < 0) return -1; //parse_action already set dest->error_str
    str += rc;
    
    rc = parse_eos(dest, str);
    if (rc < 0) return -1; //parse_eos already set dest->error_str
    str += rc;
    
    dest->type = CMD_DBG_REG;
    dest->error_str = DBG_CMD_SUCCESS;
    return 0;
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
struct dbg_cmd_unused##CMD {} 

//^^That last line is an UNFORGIVABLE hack to swallow the semicolon
make_simple_parse_fn(CMD_DUMMY);
make_simple_parse_fn(CMD_MSG);
make_simple_parse_fn(CMD_QUIT);

//Just for syntax
typedef struct _cmd_info {
    char *cmd;
    parse_fn *fn;
} cmd_info;

static cmd_info builtin_cmds[] = {
    {"dummy",   parse_CMD_DUMMY},      //Just for testing
    {"open",	parse_open_cmd},	   //Open FPGA connection
    {"close",	parse_close_cmd},	   //Close FPGA connection
    {"sel",	    parse_sel_cmd},		   //Select active dbg_guv
    {"set",	    parse_dbg_reg_cmd},	   //Issue a command to active dbg_guv
    {"name",	parse_name_cmd},	   //Rename active dbg_guv
    {"msg",	    parse_CMD_MSG},	       //Focus message window
    {"quit",    parse_CMD_QUIT},       //End timonerie session
    {"exit",    parse_CMD_QUIT},       //End timonerie session
    //Command for deleting a name?
};
#define num_builtin_cmds (sizeof(builtin_cmds)/sizeof(*builtin_cmds))

//Attempts to parse str containing a dbg_guv command. Fills dbg_cmd
//pointed to by dest. On error, returns negative and fills dest->error_str
//(unless dest is NULL, of course). On success returns 0. The active 
//dbg_guv is passed in (or NULL if there are no active guvs) in case the
//guv has its own command interpreter
int parse_dbg_cmd(dbg_cmd *dest, char const *str, dbg_guv *active) {
    //Sanity check on inputs
    if (dest == NULL) {
        return -2; //This is all we can do
    } else if (str == NULL) {
        dest->error_str = DBG_CMD_NULL_PTR;
        return -1;
    } 
    
    int num_read = 0;
    //Check which command this is
    char cmd[16];
    int rc = sscanf(str, "%15s%n", cmd, &num_read);
    if (rc < 1) {
        dest->error_str = DBG_CMD_EXP_OP;
        return -1;
    }
    
    //Do a boring old linear search. Slow, but who cares?
    int i;
    for (i = 0; i < num_builtin_cmds; i++) {
        if(!strcmp(cmd, builtin_cmds[i].cmd)) {
            rc = builtin_cmds[i].fn(dest, str + num_read);
            if (rc < 0) {
                return -1; //dest->error_str already set
            }
            
            return 0;
        }
    }
   
    //At this point, we did not match a built-in command. Now see if there
    //is an active dbg_guv, and ask it if it can use this command string
    if (active != NULL) {
        if (active->ops.got_line != NULL) {
            return active->ops.got_line(active, str, dest);
        }
    }
   
    dest->error_str = DBG_CMD_BAD_CMD;
    return -1;
}


//////////////////////////////////////////////////
//Error codes, which double as printable strings//
//////////////////////////////////////////////////

//Does this make you seasick? 
char const *const DBG_CMD_SUCCESS   = "successfully parsed dbg_guv cmd";
char const *const DBG_CMD_ADDR_RANGE   = "dbg_guv address out of range";
char const *const DBG_CMD_BAD_REG     = "Bad register code";
char const *const DBG_CMD_EXP_OP      = "Expected another operand";
char const *const DBG_CMD_TOO_MANY_OPS    = "Too many operands given";
char const *const DBG_CMD_UNEX      = "Unexpected character";
char const *const DBG_CMD_BAD_PARAM     = "Malformed parameter value";
char const *const DBG_CMD_NULL_PTR        = "NULL pointer passed to dbg_cmd parse";
char const *const DBG_CMD_IMPOSSIBLE         = "The dbg_cmd code somehow reached an area Marco thought was impossible";
char const *const DBG_CMD_NOT_IMPL        = "This function is not implement";
char const *const DBG_CMD_REDEF        = "Identifier is already in use";
char const *const DBG_CMD_BAD_CMD          = "No such command";
char const *const DBG_CMD_OPEN_USAGE         = "Usage: open fpga_name hostname port";
char const *const DBG_CMD_CLOSE_USAGE  = "Usage: close fpga_name";
char const *const DBG_CMD_SEL_USAGE      = "Usage: sel (fpga_name[guv_addr] | guv_name)";
char const *const DBG_CMD_NAME_USAGE          = "Usage: name guv_name";
