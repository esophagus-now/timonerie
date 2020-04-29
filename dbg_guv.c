#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include "dbg_guv.h"
#include "textio.h"
#include "timonier.h"


#define X(x) #x
char const *DBG_GUV_REG_NAMES[] = {
    DBG_GUV_REG_IDENTS
};
#undef X

char const *const DBG_GUV_SUCC = "success";
char const *const DBG_GUV_NULL_ARG = "received NULL argument";
char const *const DBG_GUV_NULL_CB = "received NULL callback";
char const *const DBG_GUV_NULL_CONN_INFO = "received NULL fpga_connection_info";
char const *const DBG_GUV_CNX_CLOSED = "external host closed connection";
char const *const DBG_GUV_IMPOSSIBLE = "code reached a location that Marco thought was impossible";
char const *const DBG_GUV_BAD_ADDRESS = "could not resolve address";
char const *const DBG_GUV_OOM = "out of memory";
char const *const DBG_GUV_NOT_ENOUGH_SPACE = "not enough room in buffer";


//////////////////////////////
//Static functions/variables//
//////////////////////////////

static void init_dbg_guv(dbg_guv *d, fpga_connection_info *f, int addr) {
    memset(d, 0, sizeof(dbg_guv));
    
    init_linebuf(&d->logs, DBG_GUV_SCROLLBACK);
    
    char line[80];
    sprintf(line, "%p[%d]", f, addr);
    d->name = strdup(line);
    
    d->need_redraw = 1; //Need to draw when first added in
    d->values_unknown = 1;
    d->parent = f;
    d->addr = addr;
    
    d->ops = default_guv_ops;
} 

static void deinit_dbg_guv(dbg_guv *d) {
    if (!d) return; //I guess we'll do this?
    free_linebuf_logs(&d->logs);
    deinit_linebuf(&d->logs);
    
    if (d->name) free(d->name); //Valgrind found this one. 
}

///////////////////////////////////////////
//Implementations of prototypes in header//
///////////////////////////////////////////

//Returns a newly allocated and constructed fpga_connection_info struct,
//or NULL on error
//TODO: make dbg_guv widths parameters to this function?
fpga_connection_info* new_fpga_connection() {
    fpga_connection_info *ret = calloc(1, sizeof(fpga_connection_info));
    if (!ret) return NULL;
    
    int i;
    for (i = 0; i < MAX_GUVS_PER_FPGA; i++) {
        init_dbg_guv(ret->guvs + i, ret, i);
    }
    
    return ret;
}

//Cleans up an FPGA connection. Gracefully ignores NULL input
void del_fpga_connection(fpga_connection_info *f) {
    //Gracefully quit early if f is NULL
    if (f == NULL) return;

	//TODO: remove events?

    int i;
    for (i = 0; i < MAX_GUVS_PER_FPGA; i++) {        
        //Slow as hell... there must be a better way...
        //Also, I've gotten into trouble here. The append_log function
        //specifically does not malloc or free or copy anything. However, 
        //this fpga_connection_info cleanup assumes that non-NULL strings
        //should be freed. This is inconsistent. I should fix this 
        deinit_dbg_guv(&f->guvs[i]);
    }
    
    if (f->name) free(f->name);
    
    free(f);
}

//Returns string which was displaced by new log. This can be NULL. NOTE: 
//do NOT call this function if you are holding a mutex! By the way, does NOT
//make an internal copy of the string; you must copy it yourself if this is
//desired
char *append_log(dbg_guv *d, char *log) {
    linebuf *l = &d->logs;
    char *ret = linebuf_append(l, log);
    
    d->need_redraw = 1;
    
    return ret;
}

//Enqueues the given data, which will be sent when the socket becomes 
//ready next. Returns -1 and sets f->error_str on error, or 0 on success.
//(Returns -2 if f was NULL)
int fpga_enqueue_tx(fpga_connection_info *f, char const *buf, int len) {
	if (f == NULL) {
		return -2; //This is all we can do
	}
	
	if(FCI_BUF_SIZE - f->out_buf_len < len) {
		f->error_str = DBG_GUV_NOT_ENOUGH_SPACE;
		return -1;
	}
	
	//There may be an elegant solution to this, but for now I have to
	//deal with a few ugly cases:
	//
	//Case 1 (no wraparound, wr > rd):
	//  [ | | | | | |x|x|x|x| | | | | | | | ]
	//               ^rd     ^wr   ^wr+len
	//
	//Case 2 (wraparound, wr > rd)
	//  [ | | | | | |x|x|x|x| | | | | | | | ]
	//     ^wr+len   ^rd     ^wr  
	//
	//Case 3 (no wraparound, wr < rd):
	//  [x| | | | | | | | | | |x|x|x|x|x|x|x]
	//     ^wr       ^wr+len   ^rd          
	//
	//Cases 1 and 3 are handled identically, so we just need to
	//distinguish case 2 specifically
	
	int wr_pos = (f->out_buf_pos + f->out_buf_len) % FCI_BUF_SIZE;
	if (wr_pos + len > FCI_BUF_SIZE) {
		//Case 2: need to split into first and second halves
		int first_half_len = FCI_BUF_SIZE - wr_pos - 1; //OBOE?
		memcpy(f->out_buf + wr_pos, buf, first_half_len);
		
		int second_half_len = len - first_half_len;
		memcpy(f->out_buf + 0, buf + first_half_len, second_half_len);
		
	} else {
		//Case 1 or case 3: we can just directly copy in
		memcpy(f->out_buf + wr_pos, buf, len);
	}
	
	f->out_buf_len += len;
	f->error_str = DBG_GUV_SUCC;
	#warning Error code not checked
	event_add(f->wr_ev, NULL); //Now that there is data to send, send it!
	return 0;
}

//TODO: remove hardcoded widths
//Constructs a dbg_guv commadn and queues it for output by calling
//fpga_enequeue_tx
int dbg_guv_send_cmd(dbg_guv *d, dbg_reg_type reg, unsigned param) {
	fpga_connection_info *f = d->parent;
	int dbg_guv_addr = d->addr;
	#warning Remove hardcoded widths
	unsigned cmd_addr = (dbg_guv_addr << 4) | reg;
	
	int rc = fpga_enqueue_tx(f, (char*) &cmd_addr, sizeof(cmd_addr));
	if (rc == 0 && reg != LATCH) {
		rc = fpga_enqueue_tx(f, (char*) &param, sizeof(param));
	}
	
	return rc;
}

//TODO: runtime sizes for the stream?
int read_fpga_connection(fpga_connection_info *f, int fd, int addr_w) {
    if (f == NULL) {
        return -2; //This is all we can do
    }
    //Read from fd into local buffer
    
    //Try reading as many bytes as we have space for. Note: this
    //is kind of ugly, but because we might only read part of a 
    //message, we need to save partial messages in a buffer
    int num_read = read(fd, f->in_buf + f->in_buf_pos, FCI_BUF_SIZE - f->in_buf_pos);
    if (num_read < 0) {
        f->error_str = strerror(errno);
        return -1;
    } else if (num_read == 0) {
        f->error_str = DBG_GUV_CNX_CLOSED;
        return -1;
    }
    f->in_buf_pos += num_read;
    
    //For each complete command in the buffer, dispatch to correct guv
    
    #warning Be careful about endianness, and implement runtime sizes
    unsigned *rd_pos = (unsigned *)f->in_buf; //TODO: manage endianness
    int msgs_left = (f->in_buf_pos / 8); //TODO: runtime size
    
    //Iterate through all the complete messages in the read buffer
    while (msgs_left --> 0) {
        unsigned *beginning = rd_pos; //Hang onto this for guv ops calls
        unsigned addr = *rd_pos++;
        unsigned val = *rd_pos++;
        
        int dbg_guv_addr = addr & ((1 << addr_w) - 1);
        int is_receipt = (addr >> addr_w) & 1;
        
        if (dbg_guv_addr >= MAX_GUVS_PER_FPGA) {
            //ignore this message
            continue;
        }
        
        dbg_guv *d = f->guvs + dbg_guv_addr;
        if (is_receipt) {
            d->keep_pausing = val & 1;
            d->keep_logging = (val>>1) & 1;
            d->keep_dropping = (val>>2) & 1;
            if (d->log_cnt == 0) d->log_cnt = (val>>3) & 1;
            if (d->drop_cnt == 0) d->drop_cnt = (val>>4) & 1;
            d->inj_TVALID = (val>>5) & 1;
            d->dut_reset = (val>>6) & 1;
            d->inj_failed = (val>>7) & 1;
            d->dout_not_rdy_cnt = (val>>8);
            d->values_unknown = 0;
            d->need_redraw = 1;
            
            if (d->ops.cmd_receipt != NULL) {
                //TODO: check error code
                #warning Error code is not checked
                d->ops.cmd_receipt(d, beginning);
            }
        } else {
            //Write a log to a dbg_guv
            char *log = malloc(32);
            sprintf(log, "0x%08x (%u)", val, val);
            char *old = append_log(d, log);
            if (old != NULL) free(NULL);
            
            if (d->ops.log != NULL) {
                //TODO: check error code
                #warning Error code is not checked
                d->ops.log(d, beginning);
            }
        }
    }
    
    //Now the really ugly thing: take whatever partial message
    //is left and shift it to the beginning of the buffer
    int i;
    #warning Need to handle runtime parameter for size
    int leftover_bytes = f->in_buf_pos % 8; //TODO: runtime size
    f->in_buf_pos -= leftover_bytes;
    for (i = 0; i < leftover_bytes; i++) {
        f->in_buf[i] = f->in_buf[f->in_buf_pos++];
    }
    f->in_buf_pos = i;
    
    return 0;
}

//Tries to write as much of f->out_buf as possible to the socket. This 
//is non-blocking. Follows usual error return values
int write_fpga_connection(fpga_connection_info *f, int fd) {
    if (f == NULL) {
        return -2; //This is all we can do
    }
    
    //See how any contiguous bytes we can use from the circular buffer
    int contig = f->out_buf_len;
    if (f->out_buf_pos + f->out_buf_len > FCI_BUF_SIZE) {
		contig = FCI_BUF_SIZE - f->out_buf_pos;
	}
	
	int rc = write(fd, f->out_buf + f->out_buf_pos, contig);
	if (rc < 0) {
		//Check if this is an error we should signal to the user
		if (rc != EAGAIN && rc != EWOULDBLOCK) {
			f->error_str = strerror(errno);
			return -1;
		}
		
		//Technically, this should never happen, since in theory this
		//function is only called once libevent determines that the 
		//socket is writable. But we'll deal with this case anyway, with
		//the caveat that well set the error string to IMPOSSIBLE
		f->error_str = DBG_GUV_IMPOSSIBLE;
		return 0; //Nothing to do, but not an error
	} else if (rc == 0) {
		f->error_str = DBG_GUV_CNX_CLOSED;
		return -1;
	}
	
	//Update the position/length in the circular buffer
	f->out_buf_len -= rc;
	if (f->out_buf_len == 0) {
		//Special case: since I don't a priori constrain the size of
		//data appended into the outpu buffer, take this chance when
		//the buffer is empty to move the position to the start. This
		//minimizes some straddling.
		f->out_buf_pos = 0;
	} else {
		f->out_buf_pos += rc;
		f->out_buf_pos %= FCI_BUF_SIZE;
		//Also, we need to reschedule the write event given that there
		//is still data to send
		#warning Error code not checked
		event_add(f->wr_ev, NULL);
	}
	
	f->error_str = DBG_GUV_SUCC;
	return 0;
}

//Duplicates string in name and saves it into d. If name was previously set, it
//will be freed
void dbg_guv_set_name(dbg_guv *d, char *name) {
    if (d->name != NULL) free(d->name);
    d->name = strdup(name);
    d->need_redraw = 1;
}

//Returns number of bytes added into buf, or -1 on error.
int draw_fn_dbg_guv(void *item, int x, int y, int w, int h, char *buf) {
    dbg_guv *d = (dbg_guv*) item;
    if (!d) return -1;
    
    if (d->need_redraw == 0) return 0; //Nothing to draw!
    
    //Save old value of buf so we can calculate number of characters added
    char *buf_saved = buf;
    
    //Draw title bar
    //First, turn on inverted video mode
    *buf++ = '\e'; *buf++ = '['; *buf++ = '7'; *buf++ = 'm';
    
    //Construct the little status icons
    char status[8];
    status[0] = '|';
    status[1] = d->inj_failed ? 'F' : '-';
    status[2] = '0' + d->dout_not_rdy_cnt; //Not super robust, but whatever
    status[3] = d->keep_pausing ? 'P' : '-';
    status[4] = d->keep_logging ? 'L' : (d->log_cnt != 0 ? 'l' : '-');
    status[5] = d->keep_dropping ? 'D' : (d->drop_cnt != 0 ? 'd' : '-');
    status[6] = d->inj_TVALID ? 'V' : '-';
    status[7] = '\0';
    static char const *const unknown = "|??????";
    
    int incr = cursor_pos_cmd(buf, x, y);
    buf += incr;
    
    //Print the title bar
    sprintf(buf, "%-*.*s%s%n", w - 7, w - 7, d->name, (d->values_unknown ? unknown : status), &incr);
    buf += incr;
    //Turn off inverted video
    *buf++ = '\e'; *buf++ = '['; *buf++ = '2'; *buf++ = '7'; *buf++ = 'm';
    
    //We consumed one line
    h--;
    y++;
    
    //Check how many lines the manager wants
    if (d->ops.lines_req == NULL || d->ops.draw_ops.draw_fn == NULL) {
        d->error_str = DBG_GUV_NULL_CB;
        return -1;
    }
    int mgr_lines = d->ops.lines_req(d, w, h);
    if (mgr_lines > h) {
		mgr_lines = h;
	}
    if (mgr_lines > 0) {
        incr = d->ops.draw_ops.draw_fn(d, x, y, w, mgr_lines, buf);
        buf += incr;
        y += mgr_lines;
        h -= mgr_lines;
    }
    
    //Now simply draw the linebuf in the remaining space, if there is 
    //any
    if (h > 0) {
		incr = draw_linebuf(&d->logs, d->log_pos, x, y, w, h, buf);
		
		if (incr < 0) {
			//Propagate error, not that it really matters...
			d->error_str = d->logs.error_str;
			return -1;
		}
		buf += incr;
	}
    
    d->need_redraw = 0;
    
    return buf - buf_saved;
}

//Returns how many bytes are needed (can be an upper bound) to draw dbg_guv
//given the size
int draw_sz_dbg_guv(void *item, int w, int h) {
    dbg_guv *d = (dbg_guv*) item;
    if (!d) return -1;
    
    if (d->need_redraw == 0) return 0; //Nothing to draw!
    
    int total_sz = 0;
    
    //Check how many lines the manager wants
    if (d->ops.lines_req == NULL || d->ops.draw_ops.draw_sz == NULL) {
        d->error_str = DBG_GUV_NULL_CB;
        return -1;
    }
    int mgr_lines = d->ops.lines_req(d, w, h - 1);
    if (mgr_lines > 0) {
        total_sz += d->ops.draw_ops.draw_sz(d, w, mgr_lines);
        h -= mgr_lines;
    }
    
    total_sz += 10; //Bytes needed to initially position the cursor
    total_sz += 4; //Bytes needed to invert colours for title bar
    total_sz += w; //Bytes needed for title bar
    total_sz += 5; //Bytes needed to turn off inverted colours
    
    int log_sz = 10 + w; //Bytes needed to move the cursor to a line, plus the length of a line
    
    total_sz += (h-1) * log_sz; //We draw h-1 lines from the linebuf, since the first line is a title bar
    
    return total_sz;
}

//Tells us that we should redraw, probably because we moved to another
//area of the screen
void trigger_redraw_dbg_guv(void *item) {
    dbg_guv *d = (dbg_guv*) item;
    if (!d) return;
    
    d->need_redraw = 1;
}

//Simply scrolls the linebuf; positive for up, negative for down. A special
//check in this function, along with a more robust check in draw_linebuf, 
//make sure that you won't read out of bounds.
void dbg_guv_scroll(dbg_guv *d, int amount) {
    d->log_pos += amount;
    if (d->log_pos < 0) d->log_pos = 0;
    if (d->log_pos >= d->logs.nlines) d->log_pos = d->logs.nlines - 1;
    d->need_redraw = 1;
}

draw_operations const dbg_guv_draw_ops = {
    draw_fn_dbg_guv,
    draw_sz_dbg_guv,
    trigger_redraw_dbg_guv,
    NULL //No exit function needed
};
