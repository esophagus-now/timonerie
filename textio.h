#ifndef TEXTIO_H
#define TEXTIO_H 1

#include <termios.h>
#include "queue.h"

#define ESC "\x1b"
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

//#define BOX_VERT '\xb3'
//#define BOX_TR   '\xbf'
//#define BOX_BL   '\xc0'
//#define BOX_HORZ '\xc4'
//#define BOX_BR   '\xd9'
//#define BOX_TL   '\xda'

#define BOX_VERT '|'
#define BOX_TR   '+'
#define BOX_BL   '+'
#define BOX_HORZ '-'
#define BOX_BR   '+'
#define BOX_TL   '+'

#define BOX_T_UP    '\xc1'
#define BOX_T_DOWN  '\xc2'
#define BOX_T_LEFT  '\xb4'
#define BOX_T_RIGHT '\xc3'

#define BOX_XROADS '\xc5'

void cursor_pos(int x, int y);

//Writes command into buf, returns number of bytes written
int cursor_pos_cmd(char *buf, int x, int y);

void term_init();

void clean_screen();

//Returns 0 if x and y are valid, -1 otherwise
//Pass NULL for either int to ignore that parameter
int parse_mouse(queue *q, char *btn_info, int *x, int *y);

#endif
