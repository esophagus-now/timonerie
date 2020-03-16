#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include "textio.h"
#include "queue.h"
#include "coroutine.h"

//These variables hang onto the old tty state so we can return to it when 
//quitting
static int changed = 0;
static struct termios old;

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
char *TEXTIO_SUCC = "success";
char *TEXTIO_UNEX = "unexpected character"; //Kind of a catch-all, but whatever
char *TEXTIO_BADX = "invalid X coordinate";
char *TEXTIO_BADY = "invalid Y coordinate";
char *TEXTIO_BADB = "invalid button code";
char *TEXTIO_UNICODE_TOO_LONG = "Unicode sequence is too long";
char *TEXTIO_UNICODE_UNEX = "unexpected continuation sequence";
char *TEXTIO_UNICODE_CONT_EXP = "continuation sequence expected but not found";
char *TEXTIO_BAD_FN_KEY_CODE = "expected [PQRS] after ^[[O";
char *TEXTIO_BAD_ESCAPE_CODE = "unrecognized escape sequence code";
char *TEXTIO_TOO_MANY_PARAMS = "too many parameters in escape sequence";
char *TEXTIO_IMPOSSIBLE = "TEXTIO library got to a place in the code that Marco thought was impossible to reach";

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

void term_init() {
    //Get current TTY attributes and save in old
    tcgetattr(0, &old);
    //Keep track of the fact that we are changing the tty settings
    changed = 1;
    //Copy the old settings and modify to turn off "cooked" mode and echoing
    struct termios mod = old;
    mod.c_lflag &= (~ECHO & ~ICANON);
    tcsetattr(0, TCSANOW, &mod);
    
    //Use the standard ANSI escape sequences to switch to the terminal's 
    //alternate buffer (so that we don't disturb the regular text output
    write(1, ALT_BUF_ON, LEN_ALT_BUF_ON);
    //Clear the screen
    write(1, ERASE_ALL, LEN_ERASE_ALL);
    //Turn on mouse reporting
    write(1, REPORT_CURSOR_ON, LEN_REPORT_CURSOR_ON);
    
    cursor_pos(0,0);
}

void clean_screen() {
    if (changed) {
        write(1, ALT_BUF_OFF, LEN_ALT_BUF_OFF);
        write(1, REPORT_CURSOR_OFF, LEN_REPORT_CURSOR_OFF);
        tcsetattr(0, TCSANOW, &old);
        changed = 0;
    }
}

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
        switch(c&11) {
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
    return -1;
}

//Maintains internal state machine. Uses input char to advance state machine,
//returning 0 on succesful acceptance, and returning 1 if no error occurred but
//the state machine is not finished yet.
//On error, returns -1. When this happens, the state machine resets itself and
//an error code is returned in res. Use textio_strerror to get the associated
//printable string
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
    static int parsed_num; //When parsing integers in escape sequences
    
    //Some of the code below expects initialized values in the struct
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
    
    if (c < 0x80 || c != '\x1b') { //This is a simple ASCII char
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
    } else {
        res->type = TEXTIO_GETCH_WIDE;
        count = 0;
        while ((c <<= 1) & 0x80) count++; //Count number of remaining bytes
        
        //Check if this Unicode character follows our rules for byte lengths
        if (count == 0) {
            res->error_str = TEXTIO_UNICODE_UNEX;
            res->smoking_gun = c;
            scrResetReturn(-1);
        } else if (count > 3) {
            res->error_str = TEXTIO_UNICODE_TOO_LONG;
            res->smoking_gun = c;
            scrResetReturn(-1);
        }
        
        scrReturn(1);
        goto unicode_char;
    }
    
    //We should never get here
    res->error_str = TEXTIO_IMPOSSIBLE;
    scrResetReturn(-1);


    //Note that all clauses in the ifelse block immediately aboce the escape_seq
    //label end with csiResetReturn(#) or explicitly goto a label.

    //This is turning into spaghetti, but there isn't much I can do about it
escape_seq:
    if (c == 'O') { //F[1-4] was pressed
        //Start by setting the type and returning 1 (to indicate that we need 
        //another character)
        res->type = TEXTIO_GETCH_FN_KEY;
        scrResetReturn(1);
        
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
        scrResetReturn(1);
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
    scrResetReturn(-1);
    
    //Note that all clauses in the ifelse block immediately aboce the csi_seq
    //label end with csiResetReturn(#) or explicitly goto a label.

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
        } else if (c == ';') {
            res->params[res->num_params++] = parsed_num;
            parsed_num = 0;
            if (res->num_params == TEXTIO_MAX_ESC_PARAMS) {
                res->error_str = TEXTIO_TOO_MANY_PARAMS;
                scrResetReturn(-1);
            }
        } else if (isprint(c)) {
            res->code = c;
            //Done; reset state machine
            scrResetReturn(0);
        }
    }
    
    //We should never get here
    res->error_str = TEXTIO_IMPOSSIBLE;
    scrResetReturn(-1);
    
    
    //Note that all clauses in the ifelse block immediately above the unicode_char
    //label end with csiResetReturn(#)

mouse_seq:

    

unicode_char:
    
    scrResetReturn(0);
    
    //We should never get here
    scrFinish(-1);
}
