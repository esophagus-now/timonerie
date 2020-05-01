#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <event2/event.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "dbg_cmd.h"
#include "twm.h"
#include "dbg_guv.h"
#include "timonier.h"
#include "textio.h"
#include "coroutine.h"

//I don't feel bad about this global variable being here, since I really 
//only moved this stuff out of main.c to keep the code files more organized.
extern msg_win *err_log;
//Well, it doesn't cause me any additional regret that I didn't already have
//for usign globals in the first place.
//Honestly, I don't think it would be worth the trouble to change this to a
//"better" approach

//Default got_line function (interactive commands)
static int got_line_inter(dbg_guv *owner, char const *str) {
	dbg_cmd cmd;
    int rc = parse_dbg_reg_cmd(&cmd, str);
    if (rc < 0) {
		owner->error_str = cmd.error_str;
		return rc;
	}
	
	char line[80];
	int len;
	
	cursor_pos(1, term_rows-1);
	if (cmd.reg == LATCH) {
		sprintf(line, "Committing values to %s" ERASE_TO_END "%n", owner->name, &len);
	} else {
		sprintf(line, "Writing 0x%08x (%u) to %s::%s" ERASE_TO_END "%n", 
			cmd.param, 
			cmd.param,
			owner->name,
			DBG_GUV_REG_NAMES[cmd.reg],
			&len
		);
	}
	write(1, line, len);
	
	//These are the only fields not updated by the command receipt
	switch (cmd.reg) {
	case DROP_CNT:
		owner->drop_cnt = cmd.param;
		break;
	case LOG_CNT:
		owner->log_cnt = cmd.param;
		break;
	case INJ_TDATA:
		owner->inj_TDATA = cmd.param;
		break;
	case INJ_TLAST:
		owner->inj_TLAST = cmd.param;
		break;
	case DUT_RESET:
		owner->dut_reset = cmd.param;
		break;
	default:
		//Just here to get rid of warning for not using everything in the enum
		break;
	}
	
	//Actually send the command
	rc = dbg_guv_send_cmd(owner, cmd.reg, cmd.param);
	if (rc < 0) {
		sprintf(line, "Could not enqueue command: %s", owner->parent->error_str);
		msg_win_dynamic_append(err_log, line);
	}
    
    return rc;
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
	.init_mgr = NULL,
    .got_line = got_line_inter,
    .lines_req = lines_req_inter,
    .cmd_receipt = NULL,
    .log = NULL,
    .draw_ops = {
        .draw_fn = draw_fn_inter,
        .draw_sz = draw_sz_inter,
        .trigger_redraw = trigger_redraw_inter
    },
    .cleanup_mgr = NULL
};

typedef enum _fio_state_t {
	FIO_NOFILE, //fd is considered invalid
    FIO_IDLE, //fd is considered valid
    FIO_WAIT_READ, //waiting for aread event to get new file data
    FIO_WAIT_ACK, //Waiting for a command receipt
    FIO_PAUSED,
    FIO_DONE,
    FIO_LOGGING,
    FIO_ERROR
} fio_file_state_t;

#define MAX_FIO_NAME_SZ 63
#define FIO_BUF_SIZE 512
#define FIO_LO_WMARK 32 //If output buffer goes below this level, the dbg_guv
                        //is told to unpause
#define FIO_HI_WMARK 384 //If output buffer goes above this level, the dbg_guv
                         //is told to pause
typedef struct _fio {
	dbg_guv *owner;
	
    //Input file
    fio_file_state_t send_state;
    fio_file_state_t __old_send_state; //Used by the FSM for resuming after pause
    int send_retries; //If an inject fails, we try again after 5 ms, 
                      //then 50 ms, then 5000 ms. If the inject still
                      //hasn't succeeded, then we error out
    int send_pause; //If 1, the state machine will pause at its earliest
                    //convenience. To unpause, set this to 0 and call 
                    //the sendfile_fsm function again.
    char send_file[MAX_FIO_NAME_SZ+1];
    int send_fd;
    int send_bytes;
    char const *send_error_str;
    //Input buffer and read event
    char in_buf[FIO_BUF_SIZE];
    int in_buf_pos, in_buf_len;
    struct event *file_rd_ev;
    
    //Output file
    fio_file_state_t log_state;
    char log_file[MAX_FIO_NAME_SZ+1];
    int log_latch_needed; //Ugly kludge: if both sending and logging are on,
                          //we need to manage LATCH commands
    int log_fd;
    int log_numsaved;
    char const *log_error_str;
    //Output buffer and write event
    char out_buf[FIO_BUF_SIZE];
    int out_buf_pos, out_buf_len;
    struct event *file_wr_ev;
} fio;

//Handles event to read from input file
static void fio_file_rd_ev(evutil_socket_t fd, short what, void *arg) {
	fio *f = arg;
	
    //Special case: if ring buffer is empty, put the position back to 0.
    //Reduces ugly straddling cases.
    if (f->in_buf_len == 0) f->in_buf_pos = 0;
    
	int rc = read(
		fd, 
		f->in_buf + f->in_buf_pos + f->in_buf_len, //Location of next free byte
		FIO_BUF_SIZE - (f->in_buf_pos + f->in_buf_len) //Amount of room in buffer
	);
	if (rc < 0) {
		f->send_state = FIO_ERROR;
		f->send_error_str = strerror(errno);
		return;
	} else if (rc == 0) {
        //Turn off TVALID to prevent accidental sends
        rc = dbg_guv_send_cmd(f->owner, INJ_TVALID, 0);
        if (rc == 0) {
            rc = dbg_guv_send_cmd(f->owner, LATCH, 0);
        }
        
        if (rc < 0) {
            //Is this really necessary?
            f->send_error_str = f->owner->parent->error_str;
            f->send_state = FIO_ERROR;
        }
        
		//If we are at the end of the file, we expect our input buffer
		//to be completely consumed. Make sure this is the case
		if (f->in_buf_len != 0) {
			f->send_error_str = FIO_STRAGGLERS;
			f->send_state = FIO_ERROR;
			return;
		}
		f->send_state = FIO_DONE;
		f->send_error_str = FIO_SUCCESS;
		return;
	} else {
        f->in_buf_len += rc;
    }
	
	#warning Remove hardcoded widths
	#warning This only does TDATA and TVALID, but not anything else
    
	//Check if there are enough bytes in the input buffer. If not, wait
    //for more bytes to be read in
	if (f->in_buf_len < 4) {
        #warning Return value not checked
        event_add(f->file_rd_ev, NULL);
		f->send_state = FIO_WAIT_READ;
		f->send_error_str = FIO_SUCCESS;
		return;
	}
    
	//Get the inject data out of the input buffer
	unsigned tdata = *(unsigned*)(f->in_buf + f->in_buf_pos);
	f->in_buf_pos += 4;
    f->in_buf_len -= 4;
	
	//Send the inject and latch commands
	rc = dbg_guv_send_cmd(f->owner, INJ_TDATA, tdata);
    if (rc == 0) {
        rc = dbg_guv_send_cmd(f->owner, INJ_TVALID, 1);
    }
	if (rc == 0) {
		rc = dbg_guv_send_cmd(f->owner, LATCH, 0);
	}
	
	if (rc < 0) {
		f->send_error_str = f->owner->parent->error_str;
		f->send_state = FIO_ERROR;
		return;
	}
	
    //A latch was sent
    f->log_latch_needed = 0;
    
	f->send_state = FIO_WAIT_ACK;
	f->send_retries = 0; //Reset retry counter
	//The next time sendfile_fsm is called is from within fio_cmd_receipt
	f->send_error_str = FIO_SUCCESS;
	return;
}

static int filename_from_path(char const *path, char *dest) {
	if (path == NULL || dest == NULL) return -1;
	
    int slash_ind = 0;
    int i;
    for (i = 0; path[i]; i++) {
        if (path[i] == '/') slash_ind = i;
    }
    if (i - slash_ind <= 1) return -1;
    strncpy(dest, path + slash_ind + (path[slash_ind] == '/'), MAX_FIO_NAME_SZ);
    
    return 0;
}

static void retry_inject(evutil_socket_t fd, short what, void *arg) {
	fio *f = arg;
	int rc = dbg_guv_send_cmd(f->owner, LATCH, 0);
	if (rc < 0) {
		//Should I do this?
		f->send_error_str = f->owner->parent->error_str;
	} else {
		f->send_error_str = FIO_SUCCESS;
	}
}

//Yes, this has the classic performance problem that we make a ton of
//small read() system calls... but who cares?
//Returns 0 on success, -1 on error, setting f->send_error_str if possible
static int sendfile_fsm(fio *f) {
    //This pausing logic is ALL WRONG!!!!!!
	if(f->send_pause) {
		if (f->send_state == FIO_PAUSED) {
			//The function was called before unpausing. I treat this as
			//an error
			f->send_state = FIO_ERROR;
			f->send_error_str = FIO_WOKE_EARLY;
			return -1;
		}
		//Save where we currently are so we can come back later
		f->__old_send_state = f->send_state;
		f->send_state = FIO_PAUSED;
		f->send_error_str = FIO_SUCCESS;
		return 0;
	} else if (f->send_state == FIO_PAUSED) {
		//Pick up where we left off
		f->send_state = f->__old_send_state;
	}

    //This code got real spaghettified, but ¯\_(ツ)_/¯
    switch (f->send_state) {
        case FIO_NOFILE: {
            f->send_error_str = FIO_NONE_OPEN;
            return -1;
        }

        case FIO_IDLE: {
            //User has opened the file and wishes to send it
            #warning Error code is not checked
            event_add(f->file_rd_ev, NULL);
            //The next time sendfile_fsm is called is from within the read event handler
            f->send_state = FIO_WAIT_READ;
            f->send_error_str = FIO_SUCCESS;
            return 0;
        }

        //Ugly business: one of the states (FIO_WAIT_READ) is managed by the
        //fio_file_rd_ev instead of this switch statement

        case FIO_WAIT_ACK: {
            //Check if inject failed
            if(f->owner->inj_failed) {
                //Too many retries. Fail with an error
                if (f->send_retries > 2) {
                    f->send_error_str = FIO_INJ_TIMEOUT;
                    f->send_state = FIO_ERROR;
                    return -1;
                }
                //Wait 5 ms on first retry, 50 ms on second, 2 sec on third
                struct timeval dly = {
                    .tv_sec = (f->send_retries == 2) ? 2 : 0,
                    .tv_usec = (f->send_retries == 1) ? 50000 : 5000
                };
                f->send_retries++;
                
                //Schedule retry_commit after the calculated delay
                #warning Return value not checked
                struct event_base *eb = event_get_base(f->file_rd_ev);
                event_base_once(eb, -1, EV_TIMEOUT, retry_inject, f, &dly);
                f->send_error_str = FIO_SUCCESS;
                return 0;
            } else {
                //Reset retry counter given succesful inject
                f->send_retries = 0;
                #warning Change this to use runtime size parameters
                f->send_bytes += 4;
                f->owner->need_redraw = 1;
                
                //If we don't have enough data in the read buffer, schedule a new
                //read event and wait for it
                if (f->in_buf_len < 4) {
                    //Shift down any partial messages to beginning of buffer
                    memmove(f->in_buf, f->in_buf + f->in_buf_pos, f->in_buf_len);
                    
                    //Schedule a read event to fill free space in the buffer
                    #warning Error code is not checked
                    event_add(f->file_rd_ev, NULL);
                    
                    //Because we might wait an unbounded amount of time for a read,
                    //trigger a latch if one is needed
                    if (f->log_latch_needed) {
                        int rc = dbg_guv_send_cmd(f->owner, LATCH, 0);			
                        if (rc < 0) {
                            //Is this really necessary?
                            f->send_error_str = f->owner->parent->error_str;
                            f->send_state = FIO_ERROR;
                            return -1;
                        }
                        f->log_latch_needed = 0;
                    }
                    
                    //The next time this function is called is from within the read event handler
                    f->send_state = FIO_WAIT_READ;
                    f->send_error_str = FIO_SUCCESS;
                    return 0;
                } else {
                    //Send the next flit and wait for it
                    //Get the inject data out of the input buffer
                    unsigned tdata = *(unsigned*)(f->in_buf + f->in_buf_pos);
                    f->in_buf_pos += 4;
                    f->in_buf_len -= 4;
                    
                    //Send the inject and latch commands
                    int rc = dbg_guv_send_cmd(f->owner, INJ_TDATA, tdata);
                    if (rc == 0) {
                        rc = dbg_guv_send_cmd(f->owner, LATCH, 0);
                    }
                    
                    if (rc < 0) {
                        //Is this really necessary?
                        f->send_error_str = f->owner->parent->error_str;
                        f->send_state = FIO_ERROR;
                        return -1;
                    }
                    
                    //A latch was sent
                    f->log_latch_needed = 0;
                    
                    //f->send_state = FIO_WAIT_ACK; //Only written to remind me that this state loops back on itself
                    f->send_error_str = FIO_SUCCESS;
                    return 0;
                }
            }
        }

        default: {
            //We want to signal an error if we get a trigger, but it's possible
            //we'd clobber an old error string which might be useful for the user
            //to see
            if (f->send_error_str == FIO_SUCCESS) {
                f->send_error_str = FIO_BAD_STATE;
            }
            return -1;
        }

    }
}

static int init_fio(dbg_guv *owner) {
	//Takes care of buffer positions/lengths
    fio *mgr = calloc(1, sizeof(fio));
    
    if (!mgr) {
		owner->error_str = FIO_OOM;
		return -1;
	}
    
    //Keep handle to owning dbg_guv
    mgr->owner = owner;
    
    mgr->send_state = FIO_NOFILE;
    mgr->log_state = FIO_NOFILE;
    
    owner->mgr = mgr;
    return 0;
}

typedef enum _fio_cmd {
	FIO_TXFILE,
	FIO_RXFILE,
	FIO_LOGON,
	FIO_LOGOFF,
	FIO_SEND,
	FIO_PAUSE,
	FIO_CONT,
	FIO_NUM_CMDS,
} fio_cmd;

static struct {char const * const str; fio_cmd cmd;} const fio_cmd_map[] = {
	{"txfile", FIO_TXFILE},
	{"rxfile", FIO_RXFILE},
	{"logon", FIO_LOGON},
	{"logoff", FIO_LOGOFF},
	{"send", FIO_SEND},
	{"pause", FIO_PAUSE},
	{"cont", FIO_CONT},
};

//File I/O command parser
static int got_line_fio(dbg_guv *owner, char const *str) {
	//Sanity check inputs
	if (owner == NULL) {
		return -2; //This is all we can do
	}
	
	if (str == NULL) {
		owner->error_str = FIO_NULL_ARG;
		return -1;
	}
	
	//Get the command
	char cmd[16];
	int rc = parse_strn(cmd, sizeof(cmd), str);
	if (rc < 0) {
		owner->error_str = FIO_BAD_CMD;
		return -1;
	}
	
	//Increment past command string
	str += rc;
	
	int i;
	fio_cmd cmd_id = FIO_NUM_CMDS;
	//Look through map of strings to command IDs. Slow linear search,
	//but who cares?
	for (i = 0; i < sizeof(fio_cmd_map)/sizeof(*fio_cmd_map); i++) {
		if (!strcmp(fio_cmd_map[i].str, cmd)) {
			cmd_id = fio_cmd_map[i].cmd;
			break;
		}
	}
	
	//Check if we found a command
	if (cmd_id == FIO_NUM_CMDS) {
		owner->error_str = FIO_BAD_CMD;
		return -1;
	}
	
	dbg_cmd dummy;
	//TODO: get rid of this need for the first parameter to skip_whitespace
	int incr = skip_whitespace(&dummy, str);
	str += incr;
	
    fio *f = owner->mgr;
    
	switch(cmd_id) {
	case FIO_TXFILE: {
        //Close currently open file if necessary
        if (f->send_state != FIO_NOFILE) {
            event_free(f->file_rd_ev);
            close(f->send_fd);
            f->send_fd = -1;
        }
        
        //Deliberately use rest of string. This way, the user can enter spaces
        //in their filename if necessary
		f->send_fd = open(str, O_RDONLY | O_NONBLOCK);
        if (f->send_fd < 0) {
            owner->error_str = strerror(errno);
            return -1;
        }
        
        #warning Return values not checked
        struct event_base *eb = event_get_base(owner->parent->rd_ev);
        f->file_rd_ev = event_new(eb, f->send_fd, EV_READ, fio_file_rd_ev, f);
        
        //Save the filename for the user's sake (no one has perfect memory!)
        filename_from_path(str, f->send_file);
        
        f->send_state = FIO_IDLE;
        f->owner->need_redraw = 1;
        return 0;
	} 
	case FIO_RXFILE: {
		owner->error_str = FIO_BAD_DEVELOPER;
        return -1;
	}
	case FIO_LOGON: {
		owner->error_str = FIO_BAD_DEVELOPER;
        return -1;
	}
	case FIO_LOGOFF: {
		owner->error_str = FIO_BAD_DEVELOPER;
        return -1;
	}
	case FIO_SEND: {
		if (f->send_state != FIO_IDLE) {
            owner->error_str = FIO_BAD_STATE;
            return -1;
        }
        
        //Give the okay to start sending
        sendfile_fsm(f);
        
        f->owner->need_redraw = 1;
        return 0;
	}
	case FIO_PAUSE: {
		owner->error_str = FIO_BAD_DEVELOPER;
        return -1;
	}
	case FIO_CONT: {
		owner->error_str = FIO_BAD_DEVELOPER;
        return -1;
	}
	default: {
		owner->error_str = FIO_IMPOSSIBLE;
		return -1;
	}
	}
}

static int lines_req_fio(dbg_guv *owner, int w, int h) {
    return 2; //We always use two lines
}

static int cmd_receipt_fio(dbg_guv *owner, unsigned const *receipt) {
    fio *f = owner->mgr;
    if (f->send_state == FIO_WAIT_ACK)
        return sendfile_fsm(f);
    else
        return 0;
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
	case FIO_WAIT_READ:
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
    default:
        sprintf(status + pos, "TX is in invalid state! %n", &incr);
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
        case FIO_LOGGING:
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
        default:
            sprintf(status + pos, "RX is in invalid state! %n", &incr);
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
    fio *f = owner->mgr;
    if (f) {
        #warning Remember to write the code for closing the logfile once I get to that!
        if (f->send_state != FIO_NOFILE) {
            event_free(f->file_rd_ev);
            close(f->send_fd);
            f->send_state = FIO_NOFILE; //No real need to set the state...
        }
        free(f);
    }
    owner->mgr = NULL;
}

guv_operations const fio_guv_ops = {
	.init_mgr = init_fio,
    .got_line = got_line_fio,
    .lines_req = lines_req_fio,
    .cmd_receipt = cmd_receipt_fio,
    .log = NULL,
    .draw_ops = {
        .draw_fn = draw_fn_fio,
        .draw_sz = draw_sz_fio,
        .trigger_redraw = trigger_redraw_fio
    },
    .cleanup_mgr = cleanup_fio
};

char const * const FIO_SUCCESS = "success";
char const * const FIO_NONE_OPEN = "no open file";
char const * const FIO_OVERFLOW = "buffer overflowed";
char const * const FIO_INJ_TIMEOUT = "inject timeout";
char const * const FIO_WOKE_EARLY = "got an unexpected event while paused";
char const * const FIO_IMPOSSIBLE = "code reached a location that Marco thought was impossible";
char const * const FIO_STRAGGLERS = "got EOF, but partial message leftover";
char const * const FIO_TOO_FEW_BYTES = "got EOF, but not enough bytes for complete message";
char const * const FIO_OOM = "out of memory";
char const * const FIO_BAD_CMD = "no such command";
char const * const FIO_NULL_ARG = "NULL argument";
char const * const FIO_BAD_STATE = "unexpected signal";
char const * const FIO_BAD_DEVELOPER = "not implemented";
char const * const FIO_ALREADY_SENDING = "already sending";
