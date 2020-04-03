#ifndef DBG_CMD_H
#define DBG_CMD_H 1

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
    X(UNUSED_12		),\
    X(UNUSED_13		),\
    X(UNUSED_14		),\
    X(LATCH			),\
    /*These next commands are for timonerie rather than a dbg_guv*/\
    

#define X(x) x
typedef enum _dbg_cmd_type {
	DBG_GUV_REG_IDENTS
} dbg_cmd_type;
#undef X

extern char const *DBG_GUV_REG_NAMES[];

typedef struct _dbg_cmd {
	dbg_cmd_type type;
	
	//If this command is destined for a dbg_guv, here is the information
	//that gets sent
	unsigned addr;
	int has_param;
	unsigned param;
	
	//Debug/pretty-print information
	unsigned dbg_guv_addr;
	//TODO: also parse FPGA number?
	
	
	//Error information
	char const *error_str;
	int error_pos;
	char smoking_gun;
} dbg_cmd;

//Attempts to parse str containing a dbg_guv command. Fills dbg_cmd
//pointed to by dest. On error, returns negative and fills dest->error_str
//(unless dest is NULL, of course). Otherwise returns 0.
int parse_dbg_cmd(dbg_cmd *dest, char *str);


//////////////////////////////////////////////////
//Error codes, which double as printable strings//
//////////////////////////////////////////////////

extern char const *const DBG_CMD_SUCCESS		; //	= "successfully parsed dbg_guv cmd";
extern char const *const DBG_CMD_ADDR_RANGE		; //	= "dbg_guv address out of range";
extern char const *const DBG_CMD_BAD_REG		; //	= "Bad register code";
extern char const *const DBG_CMD_EXP_OP			; //	= "Expected another operand";
extern char const *const DBG_CMD_TOO_MANY_OPS	; //	= "Too many operands given";
extern char const *const DBG_CMD_UNEX			; //	= "Unexpected character";
extern char const *const DBG_CMD_BAD_PARAM		; //	= "Malformed parameter value";
extern char const *const DBG_CMD_NULL_PTR		; //	= "NULL pointer passed to dbg_cmd parse";
extern char const *const DBG_CMD_IMPOSSIBLE		; //	= "The dbg_cmd code somehow reached an area Marco thought was impossible";

#endif
