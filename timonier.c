#include <stdio.h>
#include <string.h>
#include "dbg_cmd.h"
#include "twm.h"
#include "dbg_guv.h"
#include "timonier.h"
#include "textio.h"
#include "queue.h"

//I don't feel bad about this global variable being here, since I really 
//only moved this stuff out of main.c to keep the code files more organized.
extern msg_win *err_log;
//Well, it doesn't cause me any additional regret that I didn't already have
//for usign globals in the first place.
//Honestly, I don't think it would be worth the trouble to change this to a
//"better" approach

//Default got_line function (interactive commands)
static int got_line_inter(dbg_guv *owner, char const *str, dbg_cmd *dest) {
    return parse_dbg_reg_cmd(dest, str);
}

static int lines_req_inter(dbg_guv *owner, int w, int h) {
    //If this guv is tall enough, print a little message to remind the user
    //that this guv under interactive control
    if (h > 2) {
        return 1;
    } else {
        return 0;
    }
}

//Draws item. Returns number of bytes added into buf, or -1 on error.
static int draw_fn_inter(void *item, int x, int y, int w, int h, char *buf) {
    //If draw_fn_dbg_guv gave us enough room, print a little message to
    //remind the user that this guv under interactive control
    if (h > 0) {
        char *buf_saved = buf; //Keep track of original buf so we can count
        //number of bytes written
        
        int incr = cursor_pos_cmd(buf, x, y); //Moving the cursor
        buf += incr;
        
        sprintf(buf, UNDERLINE "%-*.*s" NO_UNDERLINE "%n", w, w, "Interactive mode", &incr);
        buf += incr;
        
        return buf - buf_saved;
    } else {
        return 0;
    }
}

//Returns how many bytes are needed (can be an upper bound) to draw item
//given the size
static int draw_sz_inter(void *item, int w, int h) {
    //If draw_fn_dbg_guv gave us enough room, print a little message to
    //remind the user that this guv under interactive control
    if (h > 0) {
        int const cursor_mvmt = 10; //Moving the cursor
        int const set_underline = 4 + 5; //Turn underline on and back off again
        int const line_width = w; //Actually drawing the message
        return cursor_mvmt + set_underline + line_width;
    } else {
        return 0;
    }
}

//Tells item that it should redraw, probably because it moved to another
//area of the screen
//TODO: am I even using this?
static void trigger_redraw_inter(void *item) {
    return;
}

guv_operations const default_guv_ops = {
	NULL,
    got_line_inter,
    lines_req_inter,
    {
        draw_fn_inter,
        draw_sz_inter,
        trigger_redraw_inter
    }
};

typedef enum _fio_state_t {
	FIO_NOFILE,
    FIO_IDLE, //fd is considered valid?
    FIO_SENDING,
    FIO_PAUSED,
    FIO_WAIT_ACK,
    FIO_DONE,
    FIO_SEND_ERROR
} fio_state_t;

#define MAX_FIO_NAME_SZ
typedef struct _fio {
    //Input file
    fio_state_t send_state;
    char infile_path[MAX_FIO_NAME_SZ+1];
    int infile_fd;
    int bytes_sent;
    
    //Output file
    char logfile[MAX_FIO_NAME_SZ+1];
    int logfile_fd;
    int logfile_fd_valid;
    pthread_t log_writer;
    queue egress_queue; 
    int logs_saved;
    char const *out_error_str;
    //Hideous problem: what do we do if the received data is faster than
    //what we can save to disk? I do think this is possible if the OS is
    //doing a lot of file I/O in the background. Right now my "solution" is
    //to simply quit with an overflow error
} fio;

/*
static int filename_from_path(char const *path, char *dest) {
    int slash_ind = 0;
    int i;
    for (i = 0; path[i]; i++) {
        if (path[i] == '/') slash_ind = i;
    }
    if (i - slash_ind <= 1) return -1;
    strncpy(dest, path + slash_ind + 1, MAX_FIO_NAME_SZ);
}
*/

//File I/O got_line function
static int got_line_fio(dbg_guv *owner, char const *str, dbg_cmd *dest) {
    dest->error_str = DBG_CMD_NOT_IMPL;
    return -1;
}

static int lines_req_fio(dbg_guv *owner, int w, int h) {
    return 2; //We always use two lines
}

//Draws item. Returns number of bytes added into buf, or -1 on error.
static int draw_fn_fio(void *item, int x, int y, int w, int h, char *buf) {
    if (h == 0) return 0;
    else if (h < 0 || w < 0) return -1;
    
    dbg_guv *g = item;
    
    fio *f = g->manager;
    
    char status[512];
    int pos = 0;
    int incr;
    
    if (f->send_state != FIO_NOFILE) {
		sprintf(status + pos, "TX %s: %n", f->infile_path, &incr);
		pos += incr;
	}
	switch (f->send_state) {
	case FIO_IDLE: 
		sprintf(status + pos, "TX (idle): %s%n", f->infile_path, &incr);
		pos += incr;
		break;
	case FIO_SENDING:
		sprintf(status + pos, "TX: %s, %d bytes sent%n", f->infile_path, f->bytes_sent, &incr);
		pos += incr;
		break;
	case FIO_PAUSED:
	case FIO_WAIT_ACK:
	case FIO_DONE:
	case FIO_SEND_ERROR:
	default:
		break; //Get rid of error while I check syntax
	}
    return 0;
}

//Returns how many bytes are needed (can be an upper bound) to draw item
//given the size
static int draw_sz_fio(void *item, int w, int h) {
    if (h > 0) {
        return 10 + w; //Move the cursor and write w characters
    } else {
        return 0; //In this case, only the dbg_guv title bar is drawn
    }
}

//Tells item that it should redraw, probably because it moved to another
//area of the screen
static void trigger_redraw_fio(void *item) {
    
}

guv_operations const file_tx_guv_ops = {
	NULL, //TODO: write FIO init function
    got_line_fio,
    lines_req_fio,
    {
        draw_fn_fio,
        draw_sz_fio,
        trigger_redraw_fio
    }
};
