#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <string.h>
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <poll.h>
#include "queue.h"
#include "textio.h"
#include "dbg_guv.h"
#include "dbg_cmd.h"

void producer_cleanup(void *v) {
#ifdef DEBUG_ON
    fprintf(stderr, "Entered text producer cleanup\n");
#endif
    queue *q = (queue*)v;
    
    pthread_mutex_lock(&q->mutex);
    q->num_producers--;
    pthread_mutex_unlock(&q->mutex);
    
    pthread_cond_broadcast(&q->can_cons);
}

void* producer(void *v) {
#ifdef DEBUG_ON
    fprintf(stderr, "Entered text producer\n");
#endif
    queue *q = (queue*)v;
    
    pthread_cleanup_push(producer_cleanup, q);
    
    //Read a single character from stdin until EOF
    char c;
    while (read(0, &c, 1) > 0) {
        int rc = enqueue_single(q, c);
        if (rc < 0) break;
    }
    
    pthread_cleanup_pop(1);
    
    return NULL;
}

fpga_connection_info *f = NULL;
queue *net_egress = NULL;

//This is the window that shows messages going by
msg_win *err_log = NULL;

void got_rl_line(char *str) {
    cursor_pos(1,2);
    char line[80];
    int len;
    sprintf(line, "Read line: %s" ERASE_TO_END "%n", str, &len);
    write(1, line, len);
    /* If the line has any text in it, save it on the history. */
    if (str && *str) {
		dbg_cmd cmd;
		int rc = parse_dbg_cmd(&cmd, str);
		if (rc < 0) {
			cursor_pos(1, term_rows-1);
			sprintf(line, "Parse error: %s" ERASE_TO_END "%n", cmd.error_str, &len);
			write(1, line, len);
			return;
		} else if (f == NULL) {
			cursor_pos(1, term_rows-1);
			sprintf(line, "FPGA connection is not open" ERASE_TO_END "%n", &len);
			write(1, line, len);
			return;
		} else {
			cursor_pos(1, term_rows-1);
			if (cmd.type == LATCH) {
				sprintf(line, "Committing values to guv[%d]" ERASE_TO_END "%n", 
					cmd.dbg_guv_addr,
					&len
				);
			} else {
				sprintf(line, "Writing 0x%08x (%u) to guv[%d]::%s" ERASE_TO_END "%n", 
					cmd.param, 
					cmd.param,
					cmd.dbg_guv_addr,
					DBG_GUV_REG_NAMES[cmd.type],
					&len
				);
			}
			write(1, line, len);
		}
		
        add_history(str);
        dbg_guv *g = &f->guvs[cmd.dbg_guv_addr];
        msg_win *m = &f->logs[cmd.dbg_guv_addr];
        //Seems silly to do yet another switch statement after the one in
        //parse_dbg_cmd... but anyway, it decouples the two bits of code
        //so it's easier for me to change it later if I have to
        switch (cmd.type) {
		case DROP_CNT:
			g->drop_cnt = cmd.param;
			break;
		case LOG_CNT:
			g->log_cnt = cmd.param;
			break;
		case INJ_TDATA:
			g->inj_TDATA = cmd.param;
			break;
		case INJ_TVALID:
			g->inj_TVALID = cmd.param;
			break;
		case INJ_TLAST:
			g->inj_TLAST = cmd.param;
			break;
		case INJ_TKEEP:
			g->inj_TKEEP = cmd.param;
			break;
		case INJ_TDEST:
			g->inj_TDEST = cmd.param;
			break;
		case INJ_TID:
			g->inj_TID = cmd.param;
			break;
		case KEEP_PAUSING:
			g->keep_pausing = cmd.param;
			break;
		case KEEP_LOGGING:
			g->keep_logging = cmd.param;
			break;
		case KEEP_DROPPING:
			g->keep_dropping = cmd.param;
			break;
		case DUT_RESET:
			g->dut_reset = cmd.param;
			break;
		case LATCH:
			m->need_redraw = 1;
			break;
		default:
			//Just here to get rid of warning for not using everything in the enum
			break;
		}
        
        //Actually send the command
        queue_write(net_egress, (char*) &cmd.addr, sizeof(cmd.addr));
        if (cmd.has_param)
			queue_write(net_egress, (char*) &cmd.param, sizeof(cmd.param));
		
		//Done!
		//For now, until I have code that properly handles command receipts,
		//just pretend that we know the values
		g->values_unknown = 0;
    }
    
    free(str);
}

enum {
    NORMAL,
    READLINE
};

void callback(new_fpga_cb_info info) {
	f = info.f;
	if (f == NULL) {
		char line[80];
		sprintf(line, "Could not connect to FPGA: %s", info.error_str);
		char *old = msg_win_append(err_log, strdup(line));
		if (old != NULL) free(old);
	}
	
	msg_win *g = &f->logs[0];
    g->x = 1;
    g->y = 7;
    g->w = 30;
    g->h = 7;
    g->visible = 1;
    msg_win_set_name(g, "FIZZCNT");
    
    msg_win *h = &f->logs[1];
    h->x = 32;
    h->y = 7;
    h->w = 30;
    h->h = 7;
    h->visible = 1;
    msg_win_set_name(h, "FIZZBUZZ");
}

int main(int argc, char **argv) {    
    atexit(clean_screen);
    term_init();
    
    init_readline(got_rl_line);
    
    
    //Set up thread that reads from keyboard/mouse. This will disappear soon
    //once I set up the call to poll() in the main event loop
    queue q = QUEUE_INITIALIZER;
    q.num_producers++;
    q.num_consumers++;    
    
    pthread_t prod;
    pthread_create(&prod, NULL, producer, &q);
    
    new_fpga_connection(callback, "localhost", "5555", NULL);
        
    err_log = new_msg_win("Message Window");
    err_log->x = 1;
    err_log->y = 14;
    err_log->w = term_cols - 2;
    err_log->h = 8;
    err_log->visible = 1;
    
    //Buffer for constructing strings to write to stdout
    char line[1024];
    int len = 0;
    
    textio_input in;
    
    int status_drawn = 0;
    //Draw "status bar"
    cursor_pos(1, term_rows);
    sprintf(line, "Soyez la bienvenue à la timonerie" ERASE_TO_END "%n", &len);
    write(1, line, len);
    status_drawn = 1;
    
	int mode = NORMAL;
	//Main event loop
    while(1) {
        int rc;
        char c;     
        
        //Get network data
        if (f != NULL) {
			struct pollfd pfd = {
				.fd = f->sfd,
				.events = POLLIN | POLLHUP
			};
			#ifdef DEBUG_ON
			fprintf(stderr, "Starting poll!\n");
			fflush(stderr);
			#endif
			
			rc = poll(&pfd, 1, 0);
			
			#ifdef DEBUG_ON
			fprintf(stderr, "Poll finished!\n");
			fflush(stderr);
			#endif
			
			if (pfd.revents & POLLIN) {
				int num_read;
				unsigned msg[2];
				
				num_read = read(f->sfd, (char*) msg, sizeof(msg));
				char *old;
				if (num_read != sizeof(msg)) {
					old = msg_win_append(err_log, strdup("Did not read enough from network. This code could be a lot smarter!"));
					if (old != NULL) free(old);
				} else {
					unsigned addr = msg[0];
					char *log_msg = malloc(32);
					sprintf(log_msg, "0x%08x (%u)", msg[1], msg[1]);
					old = append_log(f, addr, log_msg);
					if (old != NULL) free(old);
				}
			}
			
			if (pfd.revents & POLLHUP) {
				char *old = msg_win_append(err_log, strdup("Lost FPGA connection"));
				if (old != NULL) free(old);
				del_fpga_connection(f);
				f = NULL;
			}
		}
        
        //Resize message window appropriately
        err_log->w = term_cols - 2;
        //Draw the message window
        len = draw_msg_win(err_log, line);
        if (len > 0) {
            write(1, line, len);
            if (mode == READLINE) {
                //Put cursor in the right place
                place_readline_cursor();
            }
        }
        
        if (f != NULL) {
			//Draw dbg_guvs. This is just a test; later, the user can turn 
			//windows on and off, and we'll need a smarter loop
			len = draw_dbg_guv(&f->guvs[0], line);
			if (len > 0) {
				write(1, line, len);
				if (mode == READLINE) {
					//Put cursor in the right place
					place_readline_cursor();
				}
			}
			
			len = draw_dbg_guv(&f->guvs[1], line);
			if (len > 0) {
				write(1, line, len);
				if (mode == READLINE) {
					//Put cursor in the right place
					place_readline_cursor();
				}
			}
		}
        
        //A little slow but we'll read one character at a time, guarding each
        //one with the mutexes. For more speed, we should read them out in a 
        //loop
        rc = nb_dequeue_single(&q, &c);
        if (rc > 0) {
            sched_yield();
            continue;
        } else if (rc < 0) {
            cursor_pos(0,0);
            sprintf(line, "Error reading from queue. Quitting..." ERASE_TO_END "%n", &len);
            write(1, line, len);
            break;
        }
        
        //Draw "status bar"
        if (mode == NORMAL && !status_drawn) {
            cursor_pos(1, term_rows);
            sprintf(line, "Soyez la bienvenue à la timonerie" ERASE_TO_END "%n", &len);
            write(1, line, len);
            status_drawn = 1;
        }
            
        
        //If user pressed CTRL-D we can quit
        if (c == '\x04') {
			#ifdef DEBUG_ON
            fprintf(stderr, "Caught ctrl-D\n");
			#endif
            pthread_mutex_lock(&q.mutex);
            q.num_producers = -1;
            q.num_consumers = -1;
            pthread_mutex_unlock(&q.mutex);
            
            //This is just desperate...
            pthread_cond_broadcast(&q.can_prod);
            pthread_cond_broadcast(&q.can_cons);
            
            usleep(1000);
            
            //We gave peace a chance, but one of the threads may be blocked in
            //a read call. We have no choice but to cancel them; I should find
            //a way to fix this...
            pthread_cancel(prod);
            
            //Cross your fingers that this works!
            del_fpga_connection(f);
            break;
        } else {
            if (mode == READLINE) {
                forward_to_readline(c);
            }
            int rc = textio_getch_cr(c, &in);
            if (rc == 0) {
                switch(in.type) {
                case TEXTIO_GETCH_PLAIN: {
                    int tmp;
                    sprintf(line, "You entered: 0x%02x%n", in.c & 0xFF, &tmp);
                    len = tmp;
                    if (isprint(in.c)) {
                        sprintf(line + tmp, " = \'%c\'" ERASE_TO_END "%n", in.c, &tmp);
                        len += tmp;
                    } else {
                        sprintf(line + tmp, " = <?>" ERASE_TO_END "%n", &tmp);
                        len += tmp;
                    }        
                    if (mode == NORMAL && in.c == ':') {
                        mode = READLINE;
                        disable_mouse_reporting();
                        readline_redisplay();
                        status_drawn = 0;
                        continue;
                    }
                    break;
                } case TEXTIO_GETCH_UNICODE:
                    sprintf(line, "You entered: %s" ERASE_TO_END "%n", in.wc, &len);
                    break;
                case TEXTIO_GETCH_FN_KEY:
                    sprintf(line, "You entered: %s%s%s%s" ERASE_TO_END "%n", 
                        in.shift ? "(shift)" : "", 
                        in.meta ? "(alt)" : "", 
                        in.ctrl ? "(ctrl)" : "", 
                        FN_KEY_NAMES[in.key], 
                        &len
                    );
                    break;
                case TEXTIO_GETCH_ESCSEQ:
                    if (in.code == 'q') {
                        mode = NORMAL;
                        enable_mouse_reporting();
                        break;
                    }
                    sprintf(line, "You entered some kind of escape sequence ending in %c" ERASE_TO_END "%n", in.code, &len);
                    break;
                case TEXTIO_GETCH_MOUSE: {
					if (f != NULL) {
						msg_win *g = &f->logs[0];
						msg_win *h = &f->logs[1];
						//Just for fun: use scrollwheel inside dbg_guv
						if (in.btn == TEXTIO_WUP) {
							if (in.x >= g->x && in.x < g->x + g->w && in.y >= g->y && in.y < g->y + g->h) {
								if (g->buf_offset < g->l.nlines - g->h - 1) g->buf_offset++;
								g->need_redraw = 1;
							}
						} else if (in.btn == TEXTIO_WDN) {
							if (in.x >= g->x && in.x < g->x + g->w && in.y >= g->y && in.y < g->y + g->h) {
								if (g->buf_offset > 0) g->buf_offset--;
								g->need_redraw = 1;
							}
						}
						
						if (in.btn == TEXTIO_WUP) {
							if (in.x >= h->x && in.x < h->x + h->w && in.y >= h->y && in.y < h->y + h->h) {
								if (h->buf_offset < h->l.nlines - h->h - 1) h->buf_offset++;
								h->need_redraw = 1;
							}
						} else if (in.btn == TEXTIO_WDN) {
							if (in.x >= h->x && in.x < h->x + h->w && in.y >= h->y && in.y < h->y + h->h) {
								if (h->buf_offset > 0) h->buf_offset--;
								h->need_redraw = 1;
							}
						}
					}
                    //Again: this is just for fun! This is not staying around permanently, I promise!
                    if (in.btn == TEXTIO_WUP) {
                        if (in.x >= err_log->x && in.x < err_log->x + err_log->w && in.y >= err_log->y && in.y < err_log->y + err_log->h) {
                            if (err_log->buf_offset < err_log->l.nlines - err_log->h - 1) err_log->buf_offset++;
                            err_log->need_redraw = 1;
                        }
                    } else if (in.btn == TEXTIO_WDN) {
                        if (in.x >= err_log->x && in.x < err_log->x + err_log->w && in.y >= err_log->y && in.y < err_log->y + err_log->h) {
                            if (err_log->buf_offset > 0) err_log->buf_offset--;
                            err_log->need_redraw = 1;
                        }
                    }
                    
                    sprintf(line, "Mouse: %s%s%s%s at %d,%d" ERASE_TO_END "%n", 
                        in.shift ? "(shift)" : "", 
                        in.meta ? "(alt)" : "", 
                        in.ctrl ? "(ctrl)" : "", 
                        BUTTON_NAMES[in.btn],
                        in.x,
                        in.y,
                        &len);
                    break;
                }
                }
                cursor_pos(1,1);
                write(1, line, len);
                if (mode == READLINE) {
                    //Put cursor in the right place
                    place_readline_cursor();
                }
            } else if (rc < 0) {
                cursor_pos(1,5);
                sprintf(line, "Bad input, why = %s, smoking_gun = 0x%02x" ERASE_TO_END "%n", in.error_str, in.smoking_gun & 0xFF, &len);
                write(1, line, len);
            }
        }
        
        sched_yield();
    }
    
    free_msg_win_logs(err_log);
    del_msg_win(err_log);
    
    pthread_join(prod, NULL);
#ifdef DEBUG_ON
    fprintf(stderr, "Joined prod\n");
#endif
    
    del_fpga_connection(f);
    clean_screen();
    return 0;
}
