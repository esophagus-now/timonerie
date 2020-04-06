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

#define ERASE_TO_END CSI "K"

#define REPORT_CURSOR_ON "\x1b[?1003h"
#define LEN_REPORT_CURSOR_ON 8

#define REPORT_CURSOR_OFF "\x1b[?1003l"
#define LEN_REPORT_CURSOR_OFF 8

#define BOX_VERT "\u2502"
#define BOX_TR   "\u2510"
#define BOX_BL   "\u2514"
#define BOX_HORZ "\u2500"
#define BOX_BR   "\u2518"
#define BOX_TL   "\u250C"

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
    TEXTIO_GETCH_UNICODE, //For multi-byte unicode characters
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
    
    //Used when type = TEXTIO_GETCH_UNICODE
    int unicode_len;
    char wc[5]; 
    
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
    char const *error_str;
    char smoking_gun;
    char expected;
} textio_input;

//////////////
//Prototypes//
//////////////

//TODO: make this less hacky
extern int term_rows, term_cols;

void cursor_pos(int x, int y);

//Writes command into buf, returns number of bytes written
int cursor_pos_cmd(char *buf, int x, int y);

//Returns 0 on success, -1 on error
int term_init();
void clean_screen();

//Maintains internal state machine. Uses input char to advance state machine,
//returning 0 on succesful acceptance, and returning positive if no error 
//occurred but the state machine is not finished yet.
//On error, returns -1. When this happens, the state machine resets itself and
//an error code is returned in res->error_str (which can also be printed)
int textio_getch_cr(char c, textio_input *res);

typedef void (*readline_callback)(char *);

//From https://github.com/ulfalizer/readline-and-ncurses/blob/master/rlncurses.c
void forward_to_readline(char c);
void readline_redisplay(void);
void place_readline_cursor(void);
//Returns -1 on error, 0 on success
int init_readline(readline_callback cb);
void deinit_readline(void);

void enable_mouse_reporting();

void disable_mouse_reporting();

////////////////////////
//Srolling text window//
////////////////////////
//This is essentially a circular buffer, but there is only one writer which
//just constantly overwrites old data. Also, the reader can read whatever they
//want, usually at some fixed offset from pos
typedef struct _linebuf {
    char **lines;   //Array of strings
    int pos;        //Where we will put our next string
    int nlines;     //Number of strings in the linebuffer
    
    //Error information
    char const *error_str;
} linebuf;

//Statically initialize a linebuf. Returns 0 on success, negative on error.
//In fact, -1 on general error (and sets l->error_str accordingly) and -2
//if l is NULL
//Assumes *l is empty.
//NOTE: all log entries are initialized to NULL
int init_linebuf(linebuf *l, int nlines);

//Dynamically allocate and initialize a linebuf. Returns NULL on error. 
//NOTE: all log entries are initialized to NULL
linebuf *new_linebuf(int nlines);

//Frees all non-NULL logs stored in l. Gracefully ignores NULL input
void free_linebuf_logs(linebuf *l);

//Frees memory allocated with init_linebuf. Does not free whatever is still
//inside l.lines[]. Gracefully ignores NULL input
void deinit_linebuf(linebuf *l);

//Deletes a linebuf allocated with new_linebuf. Gracefuly ignores NULL input
void del_linebuf(linebuf *l);

//Overwrites the oldest log in l with input log. DOES NOT COPY ANYTHING! 
//Returns what was previously there, or NULL on error (and l->error_str 
//will be set if possible). NOTE: all logs initially in l are guaranteed to 
//start off as NULL, but can become non-NULL when you start appending things.
char *linebuf_append(linebuf *l, char *log);

//Gathers the last h strings form l (starting from offset) and draws them 
//into the rect defined by x,y,w,h. Returns number of bytes added into buf. 
//Guaranteed to add less than (10+w)*h bytes into buf, so make sure you 
//have at least that much space. Returns number of bytes added into buf, or
//-1 on error (and sets l->error_str if possible). NOTE: returns -2 if l is
//NULL
int draw_linebuf(linebuf *l, int offset, int x, int y, int w, int h, char *buf);

#define MSG_WIN_SCROLLBACK 1000
typedef struct _msg_win {
    //Stores lines in the message window
    linebuf l;
    int buf_offset;
    
    //Display information
    char name[32];
    int need_redraw;
    
    //Error information
    char const *error_str;
} msg_win;

//Statically initialize a msg_win. If name is not NULL, this name is copied
//into the msg_win struct. Returns 0 on success, negative on error.
//In fact, -1 on general error (and sets l->error_str accordingly) and -2
//if m is NULL
//NOTE: all log entries are initialized to NULL
//NOTE: all log entries are initialized to NULL
int init_msg_win(msg_win *m, char const *name);

//Dynamically allocate and initialize a linebuf. Returns NULL on error. 
//NOTE: all log entries are initialized to NULL
msg_win* new_msg_win(char const *name);

//Frees memory allocated with init_msg_win. Gracefully ignores NULL input
void deinit_msg_win(msg_win *m);

//Deletes a linebuf allocated with new_linebuf. Gracefuly ignores NULL input
void del_msg_win(msg_win *m);

//If the strings you've saved in m are allocated with malloc, you can use
//this helper function to free them all
void free_msg_win_logs(msg_win *m);

//Duplicates string in name (if non-NULL) and saves it into m. 
void msg_win_set_name(msg_win *m, char *name);

//Calls linebuf_append(&m->l, log). No strings are ever copied or freed. 
//This will also return any old logs that were "dislodged" by the new one.
//Finally, a redraw is triggered,
char* msg_win_append(msg_win *m, char *log);

//Returns number of bytes added into buf, or -1 on error.
int draw_fn_msg_win(void *item, int x, int y, int w, int h, char *buf);

//Returns how many bytes are needed (can be an upper bound) to draw dbg_guv
//given the size
int draw_sz_msg_win(void *item, int w, int h);

//Tells us that we should redraw, probably because we moved to another
//area of the screen
void trigger_redraw_msg_win(void *item);

//////////////////////////////////////////////////
//Error codes, which double as printable strings//
//////////////////////////////////////////////////
extern char const *const TEXTIO_SUCC;
extern char const *const TEXTIO_UNEX; //Kind of a catch-all, but whatever
extern char const *const TEXTIO_BADX;
extern char const *const TEXTIO_BADY;
extern char const *const TEXTIO_BADB;
extern char const *const TEXTIO_UNICODE_TOO_LONG;
extern char const *const TEXTIO_UNICODE_UNEX;
extern char const *const TEXTIO_UNICODE_CONT_EXP;
extern char const *const TEXTIO_BAD_FN_KEY_CODE;
extern char const *const TEXTIO_BAD_ESCAPE_CODE;
extern char const *const TEXTIO_TOO_MANY_PARAMS;
extern char const *const TEXTIO_IMPOSSIBLE;
extern char const *const TEXTIO_BAD_TILDE_CODE;
extern char const *const TEXTIO_BAD_MODIFIER_CODE;
extern char const *const TEXTIO_INVALID_PARAM;
extern char const *const TEXTIO_OOM;
extern char const *const TEXTIO_MSG_WIN_TOO_SMALL;
extern char const *const TEXTIO_OOB;


#endif
