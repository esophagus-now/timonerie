#ifndef TEXTIO_H
#define TEXTIO_H 1

#include <termios.h>
#include "queue.h"

#define ESCAPE "\x1b"
#define CSI "\x1b["

#define ALT_BUF_ON "\x1b[?1049h"
#define LEN_ALT_BUF_ON 8

#define ALT_BUF_OFF "\x1b[?1049l"
#define LEN_ALT_BUF_OFF 8

#define ERASE_ALL "\x1b[2J"
#define LEN_ERASE_ALL 4

#define REPORT_CURSOR_ON "\x1b[?1003h"
#define LEN_REPORT_CURSOR_ON 8

#define REPORT_CURSOR_OFF "\x1b[?1003l"
#define LEN_REPORT_CURSOR_OFF 8

#define BOX_VERT '|'
#define BOX_TR   '+'
#define BOX_BL   '+'
#define BOX_HORZ '-'
#define BOX_BR   '+'
#define BOX_TL   '+'

/////////////////////////////////////////////////////
//Constants and printable strings for mouse buttons//
/////////////////////////////////////////////////////
#define BTN_IDENTS \
    X(TEXTIO_LMB), /*Left mouse button*/ \
    X(TEXTIO_RMB), \
    X(TEXTIO_MMB), \
    X(TEXTIO_NOB), \
    X(TEXTIO_MVT), /*movement*/ \
    X(TEXTIO_WUP), /*mouse wheel up*/ \
    X(TEXTIO_WDN)

#define X(x) x
typedef enum _btn_const {
    BTN_IDENTS
} btn_const;
#undef X

extern char *BUTTON_NAMES[];

////////////////////////////////////////////////////////////
//Constants and printable strings for cursor/function keys//
////////////////////////////////////////////////////////////
#define GETCH_IDENTS \
    X(TEXTIO_KEY_UP), \
    X(TEXTIO_KEY_DOWN), \
    X(TEXTIO_KEY_LEFT), \
    X(TEXTIO_KEY_RIGHT), \
    X(TEXTIO_KEY_F1), \
    X(TEXTIO_KEY_F2), \
    X(TEXTIO_KEY_F3), \
    X(TEXTIO_KEY_F4), \
    X(TEXTIO_KEY_F5), \
    X(TEXTIO_KEY_F6), \
    X(TEXTIO_KEY_F7), \
    X(TEXTIO_KEY_F8), \
    X(TEXTIO_KEY_F9), \
    X(TEXTIO_KEY_F10), \
    X(TEXTIO_KEY_INSERT), \
    X(TEXTIO_KEY_PGUP), \
    X(TEXTIO_KEY_PGDOWN), \
    X(TEXTIO_KEY_DEL) \

#define X(x) x
typedef enum _getch_fn_key_t {
    GETCH_IDENTS
} getch_fn_key_t;
#undef X
extern char *FN_KEY_NAMES[];

///////////////////////////////////////////
//structs and enums for textio_getch_cr()//
///////////////////////////////////////////

typedef enum _getch_type {
    TEXTIO_GETCH_PLAIN,
    TEXTIO_GETCH_WIDE, //For multi-byte unicode characters
    TEXTIO_GETCH_FN_KEY, //If an arrow or Fn key was pressed
    TEXTIO_GETCH_ESCSEQ,
    TEXTIO_GETCH_MOUSE //If a mouse sequence was parsed
} getch_type;

//When calling textio_getch_cr, it will fill a textio_input struct
#define TEXTIO_MAX_ESC_PARAMS 16
typedef struct _textio_input{
    getch_type type;
    
    //Used when type = TEXTIO_GETCH_PLAIN
    char c; 
    
    //Used when type = TEXTIO_GETCH_WIDE
    unsigned wc; 
    
    //Used when type = TEXTIO_GETCH_FN_KEY
    getch_fn_key_t key; 
    
    //Used when type = TEXTIO_GETCH_ESCSEQ
    int csi_seen;
    int qmark_seen;
    int params[TEXTIO_MAX_ESC_PARAMS];
    int num_params;
    char code;
    
    //Used when type = TEXTIO_GETCH_MOUSE
    btn_const btn;
    int shift;
    int meta;
    int ctrl;
    int x;
    int y;
    
    //For error reporting
    char *error_str;
    char smoking_gun;
    char expected;
} textio_input;

//////////////
//Prototypes//
//////////////

void cursor_pos(int x, int y);

//Writes command into buf, returns number of bytes written
int cursor_pos_cmd(char *buf, int x, int y);

void term_init();

void clean_screen();

//Maintains internal state machine. Uses input char to advance state machine,
//returning 0 on succesful acceptance, and returning 1 if no error occurred but
//the state machine is not finished yet.
//On error, returns -1. When this happens, the state machine resets itself and
//an error code is returned in res. Use textio_strerror to get the associated
//printable string
int textio_getch_cr(char c, textio_input *res);

//////////////////////////////////////////////////
//Error codes, which double as printable strings//
//////////////////////////////////////////////////
extern char *TEXTIO_SUCC;
extern char *TEXTIO_UNEX; //Kind of a catch-all, but whatever
extern char *TEXTIO_BADX;
extern char *TEXTIO_BADY;
extern char *TEXTIO_BADB;
extern char *TEXTIO_UNICODE_TOO_LONG;
extern char *TEXTIO_UNICODE_UNEX;
extern char *TEXTIO_UNICODE_CONT_EXP;
extern char *TEXTIO_BAD_FN_KEY_CODE;
extern char *TEXTIO_BAD_ESCAPE_CODE;
extern char *TEXTIO_TOO_MANY_PARAMS;
extern char *TEXTIO_IMPOSSIBLE;
extern char *TEXTIO_BAD_TILDE_CODE;
extern char *TEXTIO_BAD_MODIFIER_CODE;

#endif
