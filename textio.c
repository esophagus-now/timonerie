#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include "textio.h"
#include "queue.h"

static int changed = 0;
static struct termios old;

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

//Returns 0 if x and y are valid, -1 otherwise
//Pass NULL for either int to ignore that parameter
int parse_mouse(queue *q, char *btn_info, int *x, int *y) {
    int pos = 0;
    
    char c;
    int rc;
    
    rc = dequeue_single(q, &c);
    if (rc < 0) {
        sprintf(btn_info, "Dequeue error");
        return rc;
    } else if (c != '[') {
        sprintf(btn_info, "Received %02x instead of \'[\'(%02x)", c, '[');
        return -1;
    }
    
    rc = dequeue_single(q, &c);
    if (rc < 0) {
        sprintf(btn_info, "Dequeue error");
        return rc;
    } else if (c != 'M') {
        sprintf(btn_info, "Received %02x instead of \'M\'(%02x)", c, 'M');
        return -1;
    }
    
    rc = dequeue_single(q, &c);
    if (rc < 0) {
        sprintf(btn_info, "Dequeue error");
        return rc;
    }
    //C contains the button code
    if (c < 32) {
        sprintf(btn_info, "Invalid button code (<32)");
        return -1;
    }
    int tmp;
    if (c >= 64 && c < 96) {
        sprintf(btn_info + pos, "(motion)%n", &tmp);
        pos += tmp;
    }
    
    if (c & 0b100) {
        sprintf(btn_info + pos, "(shift)%n", &tmp);
        pos += tmp;
    }
    
    if (c & 0b1000) {
        sprintf(btn_info + pos, "(meta)%n", &tmp);
        pos += tmp;
    }
    
    if (c & 0b10000) {
        sprintf(btn_info + pos, "(ctrl)%n", &tmp);
        pos += tmp;
    }
    
    if (c < 92) {
        switch(c&11) {
        case 0b00:
            sprintf(btn_info + pos, "LMB%n", &tmp);
            pos += tmp;
            break;
        case 0b01:
            sprintf(btn_info + pos, "MMB%n", &tmp);
            pos += tmp;
            break;
        case 0b10:
            sprintf(btn_info + pos, "RMB%n", &tmp);
            pos += tmp;
            break;
        case 0b11:
            sprintf(btn_info + pos, "noB%n", &tmp);
            pos += tmp;
            break;
        }
    } else {
        switch (c & 0b01100001) {
        case 0b1100000:
            sprintf(btn_info + pos, "WUP%n", &tmp);
            pos += tmp;
            break;
        case 0b1100001:
            sprintf(btn_info + pos, "WDN%n", &tmp);
            pos += tmp;
            break;
        }
    }
    
    //Read x and y    
    rc = dequeue_single(q, &c);
    if (rc < 0) {
        sprintf(btn_info, "Dequeue error");
        return rc;
    } else if (c < 32) {
        sprintf(btn_info, "Invalid X code (<32)");
        return -1;
    }
    if (x != NULL) *x = c - 32;
    
    rc = dequeue_single(q, &c);
    if (rc < 0) {
        sprintf(btn_info, "Dequeue error");
        return rc;
    } else if (c < 32) {
        sprintf(btn_info, "Invalid Y code (<32)");
        return -1;
    }
    if (y != NULL) *y = c - 32;
    
    return 0;
}
