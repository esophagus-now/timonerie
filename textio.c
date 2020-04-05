#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <readline/readline.h>
#include <string.h> //For strncpy
#include <signal.h> //To get access to SIGWINCH
#include <unistd.h> //isatty
#include <sys/ioctl.h> //ioctl, in order to get terminal size
#include "textio.h"
#include "queue.h"
#include "coroutine.h"

//These variables hang onto the old tty state so we can return to it when 
//quitting
static int changed = 0;
static struct termios old;

//Keep track of width and heigh
int term_rows, term_cols;

//These are used if you want to print out an enum value
#define X(x) #x
char *BUTTON_NAMES[] = {
    BTN_IDENTS
};
#undef X

#define X(x) #x
char *FN_KEY_NAMES[] = {
    GETCH_IDENTS
};
#undef X

//Printable error messages
char const *const TEXTIO_SUCC = "success";
char const *const TEXTIO_UNEX = "unexpected character"; //Kind of a catch-all, but whatever
char const *const TEXTIO_BADX = "invalid X coordinate";
char const *const TEXTIO_BADY = "invalid Y coordinate";
char const *const TEXTIO_BADB = "invalid button code";
char const *const TEXTIO_UNICODE_TOO_LONG = "Unicode sequence is too long";
char const *const TEXTIO_UNICODE_UNEX = "unexpected continuation sequence";
char const *const TEXTIO_UNICODE_CONT_EXP = "continuation sequence expected but not found";
char const *const TEXTIO_BAD_FN_KEY_CODE = "expected [PQRS] after ^[[O";
char const *const TEXTIO_BAD_ESCAPE_CODE = "unrecognized escape sequence code";
char const *const TEXTIO_TOO_MANY_PARAMS = "too many parameters in escape sequence";
char const *const TEXTIO_IMPOSSIBLE = "TEXTIO library got to a place in the code that Marco thought was impossible to reach";
char const *const TEXTIO_BAD_TILDE_CODE = "bad code in ^[[#~ or ^[[#;#~ sequence";
char const *const TEXTIO_BAD_MODIFIER_CODE = "bad modifier code";
char const *const TEXTIO_INVALID_PARAM = "invalid parameter";
char const *const TEXTIO_OOM = "out of memory";
char const *const TEXTIO_MSG_WIN_TOO_SMALL = "message window too small";
char const *const TEXTIO_OOB = "out of bounds";

void cursor_pos(int x, int y) {
    char line[80];
    int len;
    sprintf(line, CSI "%d;%dH%n", y, x, &len);
    write(1, line, len);
}

//Writes command into buf, returns number of bytes written
int cursor_pos_cmd(char *buf, int x, int y){
    int len;
    sprintf(buf, CSI "%d;%dH%n", y, x, &len);
    return len;
}

volatile sig_atomic_t called = 0;

static void get_term_sz() {
    //From https://stackoverflow.com/questions/1022957/getting-terminal-width-in-c
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    
    term_rows = w.ws_row;
    term_cols = w.ws_col;
    
    called = 1;
}

//Returns 0 on success, -1 on error
int term_init() {
    //Check if we are outputting to a TTY
    if (isatty(1) == 0) {
        fprintf(stderr, "Cannot use fancy UI when not ouputting to a TTY\n");
        return -1;
    }
    //Check if we have already init'd the term
    if (changed) {
        return 0;
    }
    //Get current TTY attributes and save in old
    tcgetattr(0, &old);
    //Keep track of the fact that we are changing the tty settings
    changed = 1;
    //Copy the old settings and modify to turn off "cooked" mode and echoing
    struct termios mod = old;
    mod.c_lflag &= (~ECHO & ~ICANON);
    tcsetattr(0, TCSANOW, &mod);
    
    //Set up a signal handler to catch window resizes
    struct sigaction act;
    sigemptyset(&act.sa_mask);
    act.sa_handler = get_term_sz;
    act.sa_flags = SA_RESTART;
    
    if (sigaction(SIGWINCH, &act, NULL) < 0) {
        //Umm, I hope this never happens?
        return -2;
    }
    
    //Oh and get the size right now
    get_term_sz();
    
    //Use the standard ANSI escape sequences to switch to the terminal's 
    //alternate buffer (so that we don't disturb the regular text output
    write(1, ALT_BUF_ON, LEN_ALT_BUF_ON);
    //Clear the screen
    write(1, ERASE_ALL, LEN_ERASE_ALL);
    //Turn on mouse reporting
    write(1, REPORT_CURSOR_ON, LEN_REPORT_CURSOR_ON);
    
    cursor_pos(0,0);
    return 0;
}

void clean_screen() {
    if (changed) {
        write(1, ALT_BUF_OFF, LEN_ALT_BUF_OFF);
        write(1, REPORT_CURSOR_OFF, LEN_REPORT_CURSOR_OFF);
        tcsetattr(0, TCSANOW, &old);
        changed = 0;
        printf("Called = %d\n", called);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////
//Stolen from https://github.com/ulfalizer/readline-and-ncurses/blob/master/rlncurses.c//
/////////////////////////////////////////////////////////////////////////////////////////

// Input character for readline
static unsigned char input;

// Used to signal "no more input" after feeding a character to readline
static int input_avail = 0;

// Not bothering with 'input_avail' and just returning 0 here seems to do the
// right thing too, but this might be safer across readline versions
static int readline_input_avail(void) {
    return input_avail;
}

static int readline_getc(FILE *dummy) {
    input_avail = 0;
    return input;
}

void forward_to_readline(char c) {
    input = c;
    input_avail = 1;
    rl_callback_read_char();
}

void readline_redisplay(void) {
    //I use term_cols to allocate an array on the stack, so I want to make sure
    //its value is sane
    if (term_cols < 0 || term_cols > 200) return;
    
    cursor_pos(1,term_rows);
    char line[term_cols]; 
    int len;
    
    //Write the line to the correct place on the screen
    sprintf(line, "%s%-*.*s%n", rl_prompt, term_cols-2, term_cols-2, rl_line_buffer, &len);
    write(1, line, len);
    
    //Put cursor in the right place
    cursor_pos(3+rl_point, term_rows);
}

void place_readline_cursor(void) {
    cursor_pos(3+rl_point, term_rows);
}

int init_readline(readline_callback cb) {
    // Disable completion. TODO: Is there a more robust way to do this?
    if (rl_bind_key('\t', rl_insert))
        return -1;

    // Let us do all terminal and signal handling
    rl_catch_signals = 0;
    rl_catch_sigwinch = 0;
    rl_deprep_term_function = NULL;
    rl_prep_term_function = NULL;

    // Prevent readline from setting the LINES and COLUMNS environment
    // variables, which override dynamic size adjustments in ncurses. When
    // using the alternate readline interface (as we do here), LINES and
    // COLUMNS are not updated if the terminal is resized between two calls to
    // rl_callback_read_char() (which is almost always the case).
    rl_change_environment = 0;

    // Handle input by manually feeding characters to readline
    rl_getc_function = readline_getc;
    rl_input_available_hook = readline_input_avail;
    rl_redisplay_function = readline_redisplay;

    rl_callback_handler_install(": ", cb);
    return 0;
}

void deinit_readline(void) {
    rl_callback_handler_remove();
}


void enable_mouse_reporting() {
    write(1, REPORT_CURSOR_ON, LEN_REPORT_CURSOR_ON);
}

void disable_mouse_reporting() {
    write(1, REPORT_CURSOR_OFF, LEN_REPORT_CURSOR_OFF);
}

////////////////////////////////////////
//Helper functions for textio_getch_cr//
////////////////////////////////////////

//Helper function to interpret the button-press char
static int parse_mouse_button(char c, textio_input *res) {   
    //The button-press char is never less than 32 
    if (c < 32) {
        res->error_str = TEXTIO_BADB;
        res->smoking_gun = c;
        return -1;
    }
    
    //This means that the mouse was moved
    if (c >= 64 && c < 96) res->btn = TEXTIO_MVT;
    
    //These bits are set if you are holding the ctrl/shift/alt keys
    if (c & 0b100) res->shift = 1;
    
    if (c & 0b1000) res->meta = 1;
    
    if (c & 0b10000) res->ctrl = 1;
    
    //What a mess... if less than 92, it measn a simple mouse button, otherwise
    //the scroll wheel was spun
    if (c < 92) {
        switch(c&0b11) {
        case 0b00:
            res->btn = TEXTIO_LMB;
            return 0;
        case 0b01:
            res->btn = TEXTIO_MMB;
            return 0;
        case 0b10:
            res->btn = TEXTIO_RMB;
            return 0;
        case 0b11:
            res->btn = TEXTIO_NOB;
            return 0;
        }
    } else {
        switch (c & 0b01100001) {
        case 0b1100000:
            res->btn = TEXTIO_WUP;
            return 0;
        case 0b1100001:
            res->btn = TEXTIO_WDN;
            return 0;
        default:
            res->error_str = TEXTIO_BADB;
            res->smoking_gun = c;
            return -1;
        }
    }
    
    res->error_str = TEXTIO_IMPOSSIBLE;
    res->smoking_gun = 24;
    return -1;
}

//On some occasions, an integer is passed as a CSI sequence parameter. This
//integer is parsed into shift, alt, and ctrl press information
static int parse_modifier_code(int code, textio_input *res) {
    //There are so many damn nuances to the codes... 
    //Shift -> 2
    //Alt -> 3
    //Alt-Shift-> 4
    //CTRL -> 5
    //CTRL-Shift -> 6
    //CTRL-Alt -> 7 but it doesn't work for Fn keys?
    //CTRL-Alt-Shift -> 8
    switch (code) {
    case 2:
        res->shift = 1;
        return 0;
    case 3:
        res->meta = 1;
        return 0;
    case 4:
        res->shift = 1;
        res->meta = 1;
        return 0;
    case 5:
        res->ctrl = 1;
        return 0;
    case 6:
        res->ctrl = 1;
        res->shift = 1;
        return 0;
    case 7:
        res->ctrl = 1;
        res->meta = 1;
        return 0;
    case 8:
        res->ctrl = 1;
        res->shift = 1;
        res->meta = 1;
        return 0;
    default:
        res->error_str = TEXTIO_BAD_MODIFIER_CODE;
        res->smoking_gun = code & 0xFF;
        return -1;
    }
    
    res->error_str = TEXTIO_IMPOSSIBLE;
    res->smoking_gun = 10;
    return -1;
}

//Helper function for CSI sequences ending with '~'. Converts *res to have 
//correct type and key value. Returns -1 on error
static int parse_tilde_sequence(textio_input *res) {
    if (res->num_params == 0) {
        //This isn't a key sequence. Leave it alone.
        return 0;
    } else if (res->num_params < 3) {
        //Figure out which key this is
        switch(res->params[0]) {
        case 15:
            res->key = TEXTIO_KEY_F5;
            break;
        case 17:
            res->key = TEXTIO_KEY_F6;
            break;
        case 18:
            res->key = TEXTIO_KEY_F7;
            break;
        case 19:
            res->key = TEXTIO_KEY_F8;
            break;
        case 20:
            res->key = TEXTIO_KEY_F9;
            break;
        case 21:
            res->key = TEXTIO_KEY_F10;
            break;
        case 2:
            res->key = TEXTIO_KEY_INSERT;
            break;
        case 5:
            res->key = TEXTIO_KEY_PGUP;
            break;
        case 6:
            res->key = TEXTIO_KEY_PGDOWN;
            break;
        case 3:
            res->key = TEXTIO_KEY_DEL;
            break;
        default:
            res->error_str = TEXTIO_BAD_TILDE_CODE;
            return -1;
        }
        
        if (res->num_params == 2) {
            //This key had modifiers
            int rc = parse_modifier_code(res->params[1], res);
            if (rc < 0) return -1;
        }
        
        return 0;
    } else {
        //This isn't a key sequence. Leave it alone.
        return 0;
    }
    
    res->error_str = TEXTIO_IMPOSSIBLE;
    res->smoking_gun = 1;
    return -1;
}

//Helper function for special-case CSI sequences corresponding to function or
//cursor keys. Converts res to have correct type and key value. Returns -1 on 
//error
static int convert_csi_fn_key(textio_input *res) {
    //If it turns out someone uses it, I can add support for ^[[#A for #
    //repeated cursor up movements (and likewise for the other directions)
    
    char c = res->code;
    
    switch (c) {
    case 'A':
        //Up arrow
        res->type = TEXTIO_GETCH_FN_KEY;
        res->key = TEXTIO_KEY_UP;
        if (res->num_params == 2) return parse_modifier_code(res->params[1], res);
        return 0;
    case 'B':
        //Down arrow
        res->type = TEXTIO_GETCH_FN_KEY;
        res->key = TEXTIO_KEY_DOWN;
        if (res->num_params == 2) return parse_modifier_code(res->params[1], res);
        return 0;
    case 'C':
        //Right arrow
        res->type = TEXTIO_GETCH_FN_KEY;
        res->key = TEXTIO_KEY_LEFT;
        if (res->num_params == 2) return parse_modifier_code(res->params[1], res);
        return 0;
    case 'D':
        //Left arrow
        res->type = TEXTIO_GETCH_FN_KEY;
        res->key = TEXTIO_KEY_RIGHT;
        if (res->num_params == 2) return parse_modifier_code(res->params[1], res);
        return 0;
    case 'P':
        //Could be F1???
        res->type = TEXTIO_GETCH_FN_KEY;
        res->key = TEXTIO_KEY_F1;
        if (res->num_params == 2) {
            return parse_modifier_code(res->params[1], res);
        } else {
            res->error_str = TEXTIO_UNEX;
            return -1; //This is probably bad news
        }
    case 'Q':
        //Could be F2???
        res->type = TEXTIO_GETCH_FN_KEY;
        res->key = TEXTIO_KEY_F2;
        if (res->num_params == 2) {
            return parse_modifier_code(res->params[1], res);
        } else {
            res->error_str = TEXTIO_UNEX;
            return -1; //This is probably bad news
        }
    case 'R':
        //Could be F3???
        res->type = TEXTIO_GETCH_FN_KEY;
        res->key = TEXTIO_KEY_F3;
        if (res->num_params == 2) {
            return parse_modifier_code(res->params[1], res);
        } else {
            res->error_str = TEXTIO_UNEX;
            return -1; //This is probably bad news
        }
    case 'S':
        //Could be F4???
        res->type = TEXTIO_GETCH_FN_KEY;
        res->key = TEXTIO_KEY_F4;
        if (res->num_params == 2) {
            return parse_modifier_code(res->params[1], res);
        } else {
            res->error_str = TEXTIO_UNEX;
            return -1; //This is probably bad news
        }
        
    default:
        //Not one of the special cases for function keys. Leave as-is
        return 0;
        
    }
    
    res->error_str = TEXTIO_IMPOSSIBLE;
    res->smoking_gun = 11;
    return -1;
}

///////////////////////
//The big boy himself//
///////////////////////

//Maintains internal state machine. Uses input char to advance state machine,
//returning 0 on succesful acceptance, and returning positive if no error 
//occurred but the state machine is not finished yet.
//On error, returns -1. When this happens, the state machine resets itself and
//an error code is returned in res->error_str (which can also be printed)
int textio_getch_cr(char c, textio_input *res) {
    //Yes, yes, I know, gotos are evil
    //
    //To make them less evil in this function, let me explain how they are used:
    //There are four labels:
    //  - escape_seq
    //  - csi_seq
    //  - mouse_seq
    //  - unicode_char
    //
    //However, my labels NEVER "fall through" into one another. In other words,
    //an explicit goto is always required to enter the block started by the 
    //label; we never enter blocks "from the top". Also, there is only one goto
    //for each label; code paths never "converge" to the same label.
    //
    //If this is the case, why did I use gotos instead of if/else or function 
    //calls? First of all, because I'm using coroutines, using separate helper
    //functions that could consume input wouldn't have cleaned anything up.  
    //(obviously I used helper functions where it didn't cause problems with
    //input consumption)
    //
    //Also, for example, you could move the entire block started by esc_seq to 
    //where I goto it. However, I felt that the gotos were still easier to read.
    //
    //Okay, got that? I only used gotos because it was the least messy solution
        
    scrBegin;
    static int count; //When parsing unicode; how many continuation chars left
    static int wc_pos; //Position into the unicode character string
    static int parsed_num; //When parsing integers in escape sequences
    static int parsed_num_dirty; //If nonzero, it means we read in a few digits
    
    //Some of the code below expects initialized values in the struct
    res->wc[5] = 0; //NUL-terminate unicode char string
    res->csi_seen = 0;
    res->qmark_seen = 0;
    res->num_params = 0;
    res->error_str = TEXTIO_SUCC;
    res->shift = 0;
    res->ctrl = 0;
    res->meta = 0;
    //Also, reset state variables
    parsed_num = 0;
    
    //Get to work on tokenizing this input!
    
    if ((unsigned) c < 0x80 && c != '\x1b') { //This is a simple ASCII char
        res->type = TEXTIO_GETCH_PLAIN;
        res->c = c;
        //Done; restart the state machine
        scrResetReturn(0);
    } else if (c == '\x1b') { //An escape sequence will follow
        res->type = TEXTIO_GETCH_ESCSEQ;
        //Note: this could still indicate that a function key was pressed; we 
        //might change this type later.
        scrReturn(1);
        goto escape_seq;
    } else { //Unicode character
        char saved_c = c;
        res->type = TEXTIO_GETCH_UNICODE; 
        //Count number of bytes in unicode char
        count = 0;
        while (c & 0x80) {
            c <<= 1;
            count++;
        }
        res->unicode_len = count;
        
        //Check if this Unicode character follows our rules for byte lengths
        if (count == 0) {
            res->error_str = TEXTIO_UNICODE_UNEX;
            res->smoking_gun = saved_c;
            scrResetReturn(-1);
        } else if (count > 4) {
            res->error_str = TEXTIO_UNICODE_TOO_LONG;
            res->smoking_gun = saved_c;
            scrResetReturn(-1);
        }
        
        res->wc[count] = 0; //NUL-terminator. Note that wc is defined as char[5]
        
        wc_pos = 0; //This is one of this function's static variables
        res->wc[wc_pos++] = saved_c;
        scrReturn(1);
        goto unicode_char;
    }
    
    //We should never get here
    res->error_str = TEXTIO_IMPOSSIBLE;
    res->smoking_gun = 2;
    scrResetReturn(-1);


    //Note that all clauses in the ifelse block immediately above the escape_seq
    //label end with scrResetReturn(#) or explicitly goto a label.

escape_seq:
    if (c == 'O') { //F[1-4] was pressed
        //Start by setting the type and returning 1 (to indicate that we need 
        //another character)
        res->type = TEXTIO_GETCH_FN_KEY;
        scrReturn(1);
        
        //Now finish off parsing the Fn key.
        int return_code = 0;
        switch(c) {
        case 'P':
            res->key = TEXTIO_KEY_F1;
            break;
        case 'Q':
            res->key = TEXTIO_KEY_F2;
            break;
        case 'R':
            res->key = TEXTIO_KEY_F3;
            break;
        case 'S':
            res->key = TEXTIO_KEY_F4;
            break;
        default:
            res->error_str = TEXTIO_BAD_FN_KEY_CODE;
            res->smoking_gun = c;
            return_code = -1;
            break;
        }
        
        //Done; restart the state machine
        scrResetReturn(return_code);
    } else if (c == '[') { //This is a control sequence
        res->csi_seen = 1;
        //Keep reading more input
        scrReturn(1);
        goto csi_seq;
    } else if (isprint(c)) { //This is a regular escape with just ^[ and a single char
        res->code = c;
        //Done; restart the state machine
        scrResetReturn(0);
    } else {
        res->error_str = TEXTIO_BAD_ESCAPE_CODE;
        res->smoking_gun = c;
        scrResetReturn(-1);
    }
    
    //We should never get here
    res->error_str = TEXTIO_IMPOSSIBLE;
    res->smoking_gun = 3;
    scrResetReturn(-1);
    
    //Note that all clauses in the ifelse block immediately aboce the csi_seq
    //label end with scrResetReturn(#) or explicitly goto a label.

csi_seq: 
    
    //Special checks for first char of escape sequence
    if (c == 'M') {
        res->type = TEXTIO_GETCH_MOUSE;
        scrReturn(1);
        goto mouse_seq;
    } else if (c == '?') {
        res->qmark_seen = 1;
        scrReturn(1);
    }
    
    while(1) {
        if (isdigit(c)) { //This char part of a numerical parameter
            parsed_num *= 10;
            parsed_num += (c - '0');
            parsed_num_dirty = 1;
            //Move on to the next character
            scrReturn(1);
        } else if (c == ';') {
            res->params[res->num_params++] = parsed_num;
            parsed_num = 0;
            parsed_num_dirty = 0;
            if (res->num_params == TEXTIO_MAX_ESC_PARAMS) {
                res->error_str = TEXTIO_TOO_MANY_PARAMS;
                res->smoking_gun = res->num_params;
                scrResetReturn(-1);
            }
            //Move on to the next character
            scrReturn(1);
        } else if (c == '~') {
            if (parsed_num_dirty) {
                res->params[res->num_params++] = parsed_num;
                parsed_num = 0;
                parsed_num_dirty = 0;
            }
            //This is a function key.
            res->type = TEXTIO_GETCH_FN_KEY;
            int rc = parse_tilde_sequence(res);
            scrResetReturn(rc);
        } else if (isprint(c)) {
            if (parsed_num_dirty) {
                res->params[res->num_params++] = parsed_num;
                parsed_num = 0;
                parsed_num_dirty = 0;
            }
            res->code = c;
            //Check for special cases
            int rc = convert_csi_fn_key(res);
            //Done; reset state machine
            scrResetReturn(rc);
        }
    }
    
    //We should never get here
    res->error_str = TEXTIO_IMPOSSIBLE;
    res->smoking_gun = 4;
    scrResetReturn(-1);
    
    
    //Note that all clauses in the ifelse block immediately above the mouse_seq
    //label end with scrResetReturn(#) or an explicit goto

mouse_seq:
    ; //For syntax
    //Parse the mouse button
    int rc = parse_mouse_button(c, res);
    if (rc < 0) {
        //parse_mouse_button has already filled res->error_str
        scrResetReturn(-1);
    } else {
        //So far so good. Let's get another character
        scrReturn(1);
    }
    
    //Parse the X coordinate
    if (c < 32) {
        res->error_str = TEXTIO_BADX;
        scrResetReturn(-1);
    } else {
        res->x = c - 32;
        scrReturn(1);
    }
    
    //Parse the Y coordinate
    if (c < 32) {
        res->error_str = TEXTIO_BADY;
        scrResetReturn(-1);
    } else {
        res->y = c - 32;
        scrResetReturn(0);
    }
    
    //We should never get here
    res->error_str = TEXTIO_IMPOSSIBLE;
    res->smoking_gun = 5;
    scrResetReturn(-1);

unicode_char:

    //Read in all continuation bytes
    while (wc_pos < res->unicode_len) {
        //Check if this is a valid continuation byte
        if ((c & 0b11000000) != 0b10000000) {
            res->error_str = TEXTIO_UNICODE_CONT_EXP;
            res->smoking_gun = c;
            scrResetReturn(-1);
        }
        res->wc[wc_pos++] = c;
        if (wc_pos == res->unicode_len) {
            //Thsi is the last byte. Signal that we're done.
            scrResetReturn(0); 
        } else {
            //Need more bytes
            scrReturn(1);
        }
    }
    
    //We should never get here
    res->error_str = TEXTIO_IMPOSSIBLE;
    res->smoking_gun = 6;
    scrResetReturn(-1);
    
    scrFinish(-1);
}

////////////////////////
//Srolling text window//
////////////////////////

//Statically initialize a linebuf. Returns 0 on success, negative on error.
//In fact, -1 on general error (and sets l->error_str accordingly) and -2
//if l is NULL
//Assumes *l is empty.
//NOTE: all log entries are initialized to NULL
int init_linebuf(linebuf *l, int nlines) {
    //Sanity-check inputs
    if (l == NULL) return -2; //This is all we can do
    if (nlines < 0) {
        l->error_str = TEXTIO_INVALID_PARAM;
        return -1;
    }
    
    //Allocate the lines array
    l->lines = calloc(nlines, sizeof(char *));
    
    //Set the remaining struct values
    l->pos = 0;
    l->nlines = nlines;
    
    l->error_str = TEXTIO_SUCC;
    return 0;
}

//Dynamically allocate and initialize a linebuf. Returns NULL on error. 
//NOTE: all log entries are initialized to NULL
linebuf *new_linebuf(int nlines) {
    //Sanity-check inputs
    if (nlines < 0) {
        return NULL; //This is all we can do
    }
    
    linebuf *l = malloc(sizeof(linebuf));
    
    if (!l) return NULL;
    
    int rc = init_linebuf(l, nlines);
    if (rc < 0) {
        free(l);
        return NULL;
    }
    
    l->error_str = TEXTIO_SUCC;
    return l;
}

//Frees memory allocated with init_linebuf. Gracefully ignores NULL input
void deinit_linebuf(linebuf *l) {
    if (!l) return;
    
    if (!l->lines || l->nlines == 0) return;
    
    free(l->lines);
    //Just for extra safety
    l->lines = NULL;
    l->nlines = 0;
    
    l->error_str = TEXTIO_SUCC;
    return;
}

//Deletes a linebuf allocated with new_linebuf. Gracefuly ignores NULL input
void del_linebuf(linebuf *l) {
    if (!l) return;
    
    deinit_linebuf(l);
    
    free(l);
    
    return;
}

//Overwrites the oldest log in l with input log. DOES NOT COPY ANYTHING! 
//Returns what was previously there, or NULL on error (and l->error_str 
//will be set if possible). NOTE: all logs initially in l are guaranteed to 
//start off as NULL, but can become non-NULL when you start appending things.
char *linebuf_append(linebuf *l, char *log) {
    if (l == NULL) return NULL;
    
    char *ret = l->lines[l->pos];
    l->lines[l->pos++] = log;
    if (l->pos == l->nlines) l->pos = 0;
    
    return ret;
}

//Gathers the last h strings form l (starting from offset) and draws them 
//into the rect defined by x,y,w,h. Returns number of bytes added into buf. 
//Guaranteed to add less than (10+w)*h bytes into buf, so make sure you 
//have at least that much space. Returns number of bytes added into buf, or
//-1 on error (and sets l->error_str if possible). NOTE: returns -2 if l is
//NULL
int draw_linebuf(linebuf *l, int offset, int x, int y, int w, int h, char *buf) {
    //Sanity check inputs
    if (l == NULL) {
        return -2; //This is all we can do
    }
    
    if (w == 0 || h == 0) return 0; //Nothing to draw
    if (x >= term_cols || y >= term_rows) return 0; //Nothing to draw
    
    if (offset + w >= l->nlines) {
        l->error_str = TEXTIO_OOB;
        return -1;
    }
    
    if (x < 0 || y < 0 || w < 0 || h < 0) {
        l->error_str = TEXTIO_INVALID_PARAM;
        return -1;
    }
    
    //Save initial buf poitner so we can calculate total change
    char *buf_saved = buf;
    
    //Clip drawing rect to stay on the screen
    if (x + w >= term_cols) w = term_cols - x;
    if (y + h >= term_rows) h = term_rows - y;
    
    int i;
    for (i = 0; i < h; i++) {
        //Move the cursor
        int incr = cursor_pos_cmd(buf, x, y + i);
        buf += incr;
        
        //Compute index into line buffer's scrollback 
        int ind = (l->pos-1) - offset - (h-1) + i;
        //wrap into the right range (it's a circular buffer)
        ind = (ind + l->nlines) % l->nlines;
        
        //Construct the string that we will print
        sprintf(buf, "%-*.*s%n", w, w, l->lines[ind], &incr);
        buf += incr;  
    }
    
    return buf - buf_saved;
}

#define SCROLLBACK 1000

//Statically initialize a msg_win. If name is not NULL, this name is copied
//into the msg_win struct. Returns 0 on success, negative on error.
//In fact, -1 on general error (and sets l->error_str accordingly) and -2
//if m is NULL
//NOTE: all log entries are initialized to NULL
int init_msg_win(msg_win *m, char const *name) {
    //Sanity check on inputs
    if (m == NULL) {
        return -2; //This is all we can do
    }
    
    //For now, msg_wins have SCROLLBACK lines of scrollback
    int rc = init_linebuf(&m->l, SCROLLBACK); //Note: guaranteed that &m->l is not NULL
    if (rc < 0) {
        //Propagate error upward
        m->error_str = m->l.error_str;
        return -1;
    }
    
    //Copy name if one was given
    if (name != NULL) {
        strncpy(m->name, name, 31);
        m->name[32] = 0; //For extra safety
    } else {
        snprintf(m->name, 32, "msg_win@%p", m);
        m->name[32] = 0; //For extra safety
    }
    
    //Give reasonable defaults to position and size
    m->x = 1;
    m->y = 1;
    m->w = 14;
    m->h = 6;
    //By default, show most recent messages
    m->buf_offset = 0;
    //Make sure we get drawn
    m->need_redraw = 1;
    //By default, don't show ourselves
    m->visible = 0;
    
    //All done with no errors
    m->error_str = TEXTIO_SUCC;
    return 0;
}

//Dynamically allocate and initialize a linebuf. Returns NULL on error. 
//NOTE: all log entries are initialized to NULL
msg_win* new_msg_win(char const *name) {
    msg_win *m = malloc(sizeof(msg_win));
    
    if (!m) return NULL;
    
    int rc = init_msg_win(m, name);
    if (rc < 0) {
        free(m);
        return NULL;
    }
    
    m->error_str = TEXTIO_SUCC;
    return m;
}

//Frees memory allocated with init_msg_win. Gracefully ignores NULL input
void deinit_msg_win(msg_win *m) {
    if (!m) return;
    
    deinit_linebuf(&m->l);
    
    //Try to prevent anyone from trying to read msg_win.linebuf contents
    m->need_redraw = 0;
    
    m->error_str = TEXTIO_SUCC;
    return;
}

//Deletes a linebuf allocated with new_linebuf. Gracefuly ignores NULL input
void del_msg_win(msg_win *m) {
    if (!m) return;
    
    deinit_linebuf(&m->l);
    
    free(m);
    
    return;
}

//If the strings you've saved in m are allocated with malloc, you can use
//this helper function to free them all
void free_msg_win_logs(msg_win *m) {
    int i;
    
    //Cleans up the code a little
    linebuf *l = &m->l;
    
    for (i = 0; i < l->nlines; i++) {
        if (l->lines[i] != NULL) free(l->lines[i]);
    }
    
    l->nlines = 0;
}

//Duplicates string in name (if non-NULL) and saves it into m. 
void msg_win_set_name(msg_win *m, char *name) {
    if (name == NULL) return;
    strncpy(m->name, name, 32);
    m->name[31] = 0; //For extra safety
}

//Calls linebuf_append(&m->l, log). No strings are ever copied or freed. 
//This will also return any old logs that were "dislodged" by the new one.
//Finally, a redraw is triggered,
char* msg_win_append(msg_win *m, char *log) {
    char *ret = linebuf_append(&m->l, log);
    m->need_redraw = 1;
    return ret;
}

//Returns number of bytes added into buf. Not really safe, should probably try
//to improve this... returns -1 on error.
int draw_msg_win(msg_win *m, char *buf) {
    //Check if we need a redraw
    if (m->need_redraw == 0 || m->visible == 0) return 0;
    if (m->w < 12 || m->h < 6) {
        m->need_redraw = 0;
        m->error_str = TEXTIO_MSG_WIN_TOO_SMALL;
        return -1;
    }
    char *buf_saved = buf;
    //Draw top row
    //Move to top-left
    int incr = cursor_pos_cmd(buf, m->x, m->y);
    buf += incr;
    int len;
    sprintf(buf, "+%.*s-%n",
        m->w - 6,
        m->name,
        &len
    );
    buf += len;
    int i;
    for (i = len; i < m->w - 1; i++) *buf++ = '-';
    *buf++ = '+';
    /*
    //Draw logs and box edges
    for (i = m->h-2-1; i >= 0; i--) {
        //Move the cursor
        incr = cursor_pos_cmd(buf, m->x, m->y + 1 + (m->h-2-1 - i)); //I hope there's no OBOE
        buf += incr;
        
        //Compute index into line buffer's scrollback 
        int ind = m->l.pos - 1 - m->buf_offset - i;
        //wrap into the right range (it's circular buffer)
        ind = (ind + m->l.nlines) % m->l.nlines;
        
        //Construct the string that we will print
        sprintf(buf, "|%-*.*s|%n", m->w-2, m->w-2, m->l.lines[ind], &incr);
        buf += incr;        
    }    
    */
    
    incr = draw_linebuf(&m->l, m->buf_offset, m->x + 1, m->y + 1, m->w - 2, m->h - 2, buf);
    if (incr < 0) {
        //Propagate error code up
        m->error_str = m->l.error_str;
        return -1;
    }
    buf += incr;
    
    //Tricky business: draw vertical lines for border
    incr = cursor_pos_cmd(buf, m->x, m->y+1);
    buf += incr;
    *buf++ = '|';
    for (i = 1; i < m->h-2; i++) {
        *buf++ = '\e';
        *buf++ = '[';
        *buf++ = 'B'; //Special CSI sequence to move cursor down
        *buf++ = '\e';
        *buf++ = '[';
        *buf++ = 'D'; //Special CSI sequence to move cursor left
        *buf++ = '|';
    }
    
    incr = cursor_pos_cmd(buf, m->x + m->w-1, m->y+1);
    buf += incr;
    *buf++ = '|';
    for (i = 1; i < m->h-2; i++) {
        *buf++ = '\e';
        *buf++ = '[';
        *buf++ = 'B'; //Special CSI sequence to move cursor down
        *buf++ = '\e';
        *buf++ = '[';
        *buf++ = 'D'; //Special CSI sequence to move cursor left
        *buf++ = '|';
    }
    
    //Draw bottom row
    //Move to bottom-left
    incr = cursor_pos_cmd(buf, m->x, m->y + m->h - 1);
    buf += incr;
    *buf++ = '+';
    for (i = 1; i < m->w - 1; i++) *buf++ = '-';
    *buf++ = '+';
    
    m->need_redraw = 0;
    return buf - buf_saved;
}
