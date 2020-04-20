#include <stdio.h>
#include <stdlib.h>
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
    },
    NULL
};

typedef enum _fio_state_t {
	FIO_NOFILE,
    FIO_IDLE, //fd is considered valid?
    FIO_SENDING,
    FIO_PAUSED,
    FIO_WAIT_ACK,
    FIO_DONE,
    FIO_ERROR
} fio_file_state_t;

#define MAX_FIO_NAME_SZ
typedef struct _fio {
    //Input file
    fio_file_state_t send_state;
    char send_file[MAX_FIO_NAME_SZ+1];
    int send_fd;
    int send_bytes;
    char const *send_error_str;
    
    //Output file
    fio_file_state_t log_state;
    char log_file[MAX_FIO_NAME_SZ+1];
    int log_fd;
    pthread_t log_thd;
    queue log_queue; 
    int log_numsaved;
    char const *log_error_str;
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

static int init_fio(dbg_guv *owner) {
    fio *mgr = calloc(1, sizeof(fio));
    
    if (!mgr) return -1; //TODO: error codes
    
    mgr->send_state = FIO_NOFILE;
    mgr->log_state = FIO_NOFILE;
    
    init_queue(&mgr->log_queue, 1, 1);
    
    //TODO: spin up egress thread
    
    owner->mgr = mgr;
    return 0;
}

//File I/O got_line function
static int got_line_fio(dbg_guv *owner, char const *str, dbg_cmd *dest) {
    //TEMPORARY: defer to regular commands just for the sake of incremental
    //testing
    return parse_dbg_reg_cmd(dest, str);
}

static int lines_req_fio(dbg_guv *owner, int w, int h) {
    return 2; //We always use two lines
}

//Draws item. Returns number of bytes added into buf, or -1 on error.
static int draw_fn_fio(void *item, int x, int y, int w, int h, char *buf) {
    if (h == 0) return 0; //No space, don't draw anything
    //Sanity check inputs
    else if (h < 0 || w < 0) return -1;
    
    dbg_guv *g = item;
    fio *f = g->mgr;
    
    //Keep track of original poitner so wan can calculate number of bytes printed.
    char *buf_saved = buf;
    
    //Build up the message that we'll print to the user
    char status[512];
    int pos = 0;
    int incr;
    
	switch (f->send_state) {
    case FIO_NOFILE:
		sprintf(status + pos, "TX (Not in use)%n", &incr);
		pos += incr;
        break;
	case FIO_IDLE: 
		sprintf(status + pos, "TX (Idle): %s%n", f->send_file, &incr);
		pos += incr;
		break;
	case FIO_SENDING:
    case FIO_WAIT_ACK:
		sprintf(status + pos, "TX (%d B sent): %s%n", f->send_bytes, f->send_file, &incr);
		pos += incr;
		break;
	case FIO_PAUSED:
		sprintf(status + pos, "TX (Paused, %d B sent): %s%n", f->send_bytes, f->send_file, &incr);
		pos += incr;
		break;
	case FIO_DONE:
		sprintf(status + pos, "TX (Done): %s%n", f->send_file, &incr);
		pos += incr;
		break;
	case FIO_ERROR:
		sprintf(status + pos, "TX (ERR: %s): %s%n", f->send_error_str, f->send_file, &incr);
		pos += incr;
		break;
	}
    
    //Add first line to display
    incr = cursor_pos_cmd(buf, x, y);
    buf += incr;
    sprintf(buf, "%-*.*s%n", w, w, status, &incr);
    buf += incr;
    
    //If we have a second line of space, also draw info for logfile
    if (h > 1) {
        pos = 0;
        switch (f->log_state) {
        case FIO_NOFILE:
            sprintf(status + pos, "RX (Not in use)%n", &incr);
            pos += incr;
            break;
        case FIO_IDLE: 
            sprintf(status + pos, "RX (Idle): %s%n", f->log_file, &incr);
            pos += incr;
            break;
        case FIO_SENDING:
        case FIO_WAIT_ACK:
            sprintf(status + pos, "RX (%d logged): %s%n", f->log_numsaved, f->log_file, &incr);
            pos += incr;
            break;
        case FIO_PAUSED:
            sprintf(status + pos, "RX (Paused, %d logged): %s%n", f->log_numsaved, f->log_file, &incr);
            pos += incr;
            break;
        case FIO_DONE:
            sprintf(status + pos, "RX (Done): %s%n", f->log_file, &incr);
            pos += incr;
            break;
        case FIO_ERROR:
            sprintf(status + pos, "RX (ERR: %s): %s%n", f->log_error_str, f->log_file, &incr);
            pos += incr;
            break;
        }
        
        //Add second line to display
        incr = cursor_pos_cmd(buf, x, y+1);
        buf += incr;
        sprintf(buf, "%-*.*s%n", w, w, status, &incr);
        buf += incr;
    }
    
    return buf - buf_saved;
}

//Returns how many bytes are needed (can be an upper bound) to draw item
//given the size
static int draw_sz_fio(void *item, int w, int h) {
    if (h > 0) {
        return 2*(10 + w); //Move the cursor and write w characters on two lines
    } else {
        return 0; //In this case, only the dbg_guv title bar is drawn
    }
}

//Tells item that it should redraw, probably because it moved to another
//area of the screen
static void trigger_redraw_fio(void *item) {
    return; //TODO: am I even using this?
}

static void cleanup_fio(dbg_guv *owner) {
    fio *mgr = owner->mgr;
    if (mgr) {
        deinit_queue(&mgr->log_queue);
        //TODO: close egress thread
        //TODO: close any open files
        free(mgr);
    }
    owner->mgr = NULL;
}

guv_operations const fio_guv_ops = {
	init_fio,
    got_line_fio,
    lines_req_fio,
    {
        draw_fn_fio,
        draw_sz_fio,
        trigger_redraw_fio
    },
    cleanup_fio
};
