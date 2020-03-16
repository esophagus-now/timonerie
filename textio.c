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
char *TEXTIO_BAD_TILDE_CODE = "bad code in ^[[#~ or ^[[#;#~ sequence";
char *TEXTIO_BAD_MODIFIER_CODE = "bad modifier code";

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
        } else if (c == ';') {
            res->params[res->num_params++] = parsed_num;
            parsed_num = 0;
            if (res->num_params == TEXTIO_MAX_ESC_PARAMS) {
                res->error_str = TEXTIO_TOO_MANY_PARAMS;
                scrResetReturn(-1);
            }
        } else if (c == '~') {
            //This is a function key.
            res->type = TEXTIO_GETCH_FN_KEY;
            int rc = parse_tilde_sequence(res);
            scrResetReturn(rc);
        } else if (isprint(c)) {
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
