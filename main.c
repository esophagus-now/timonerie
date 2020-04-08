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
#include <time.h>
#include "queue.h"
#include "textio.h"
#include "dbg_guv.h"
#include "dbg_cmd.h"
#include "pollfd_array.h"
#include "twm.h"

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

typedef struct _dummy {
    int need_redraw;
    int colour;
    struct _dummy *next;
} dummy;

dummy *dummy_head = NULL;

static int dummy_col = 40;

int draw_fn_dummy(void *item, int x, int y, int w, int h, char *buf) {
    dummy *d = (dummy*) item;
    if (d == NULL) return -1;
    
    if (!d->need_redraw) return 0;
    
    char *buf_saved = buf;
    
    int incr;
    sprintf(buf, "\e[%dm%n", d->colour, &incr);
    buf += incr;
    
    int i;
    for (i = y; i < y+h; i++) {
        incr = cursor_pos_cmd(buf, x, i);
        buf += incr;
        int j;
        for (j = 0; j < w; j++) *buf++ = '*';
    }
    
    sprintf(buf, "\e[49m%n", &incr);
    buf += incr;
    
    d->need_redraw = 0;
    
    return buf - buf_saved;
}

int draw_sz_dummy(void *item, int w, int h) {
    dummy *d = (dummy*) item;
    if (d == NULL) return -1;
    
    if (!d->need_redraw) return 0;
    
    return 10 + h*(10 + w);
}

void trigger_redraw_dummy(void *item) {
    dummy *d = (dummy*) item;
    if (d == NULL) return;
    
    d->need_redraw = 1;
}

draw_operations const dummy_ops = {
    draw_fn_dummy,
    draw_sz_dummy,
    trigger_redraw_dummy
};

#ifdef DEBUG_ON
int num_poll_logs = 1000;
#endif
    
//This is the window that shows messages going by
msg_win *err_log = NULL;

twm_tree *t = NULL; //Also fix this too?
pthread_mutex_t t_mutex = PTHREAD_MUTEX_INITIALIZER;

fpga_connection_info *FIX_THIS = NULL;

void got_rl_line(char *str) {
    fpga_connection_info *f = FIX_THIS;
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
        }
        
        add_history(str);
        
        switch(cmd.type) {
        case CMD_DUMMY: {
            dummy *d = malloc(sizeof(dummy));
            d->colour = dummy_col++;
            if (dummy_col == 47) dummy_col = 40;
            d->need_redraw = 1;
            d->next = dummy_head;
            dummy_head = d;
            twm_tree_add_window(t, d, dummy_ops);
            return;
        }
        case CMD_DBG_REG: {
            if (f == NULL) {
                cursor_pos(1, term_rows-1);
                sprintf(line, "FPGA connection is not open" ERASE_TO_END "%n", &len);
                write(1, line, len);
                return;
            } else {
                cursor_pos(1, term_rows-1);
                if (cmd.reg == LATCH) {
                    sprintf(line, "Committing values to guv[%d]" ERASE_TO_END "%n", 
                        cmd.dbg_guv_addr,
                        &len
                    );
                    #ifdef DEBUG_ON
                    num_poll_logs = 0;
                    #endif
                } else {
                    sprintf(line, "Writing 0x%08x (%u) to guv[%d]::%s" ERASE_TO_END "%n", 
                        cmd.param, 
                        cmd.param,
                        cmd.dbg_guv_addr,
                        DBG_GUV_REG_NAMES[cmd.reg],
                        &len
                    );
                }
                write(1, line, len);
            }
            
            dbg_guv *g = &f->guvs[cmd.dbg_guv_addr];
            //Seems silly to do yet another switch statement after the one in
            //parse_dbg_cmd... but anyway, it decouples the two bits of code
            //so it's easier for me to change it later if I have to
            
            //These are the only fields not updated by the command receipt
            switch (cmd.reg) {
            case DROP_CNT:
                g->drop_cnt = cmd.param;
                break;
            case LOG_CNT:
                g->log_cnt = cmd.param;
                break;
            case INJ_TDATA:
                g->inj_TDATA = cmd.param;
                break;
            case INJ_TLAST:
                g->inj_TLAST = cmd.param;
                break;
            case DUT_RESET:
                g->dut_reset = cmd.param;
                break;
            default:
                //Just here to get rid of warning for not using everything in the enum
                break;
            }
            
            //Actually send the command
            unsigned cmd_addr = (cmd.dbg_guv_addr << 4) | cmd.reg;
            queue_write(&f->egress, (char*) &cmd_addr, sizeof(cmd_addr));
            if (cmd.has_param)
                queue_write(&f->egress, (char*) &cmd.param, sizeof(cmd.param));
        }
        default: {
            cursor_pos(1, term_rows-1);
            sprintf(line, "Received a %s command" ERASE_TO_END "%n", DBG_CMD_NAMES[cmd.type], &len);
            write(1, line, len);
            return;
        }
        }
    }
    
    free(str);
}

enum {
    NORMAL,
    READLINE
};

void callback(new_fpga_cb_info info) {
    #ifdef DEBUG_ON
    fprintf(stderr, "Callback called! f = %p\n", info.f);
    fflush(stderr);
    #endif
    fpga_connection_info *f = info.f;
    if (f == NULL) {
        char line[80];
        sprintf(line, "Could not connect to FPGA: %s", info.error_str);
        char *old = msg_win_append(err_log, strdup(line));
        if (old != NULL) free(old);
        return;
    }
    FIX_THIS = f;
    
    dbg_guv *g = &f->guvs[0];
    dbg_guv_set_name(g, "FIZZCNT");
    
    if (t != NULL) {
        pthread_mutex_lock(&t_mutex);
        int rc = twm_tree_add_window(t, g, dbg_guv_draw_ops);
        if (rc < 0) {
            char line[80];
            sprintf(line, "Could not add FIZZCNT to tree: %s", t->error_str);
            char *old = msg_win_append(err_log, strdup(line));
            if (old != NULL) free(old);
        } 
        pthread_mutex_unlock(&t_mutex);
    }
    
    dbg_guv *h = &f->guvs[1];
    dbg_guv_set_name(h, "FIZZBUZZ");
    
    if (t != NULL) {
        pthread_mutex_lock(&t_mutex);
        int rc = twm_tree_add_window(t, h, dbg_guv_draw_ops);
        if (rc < 0) {
            char line[80];
            sprintf(line, "Could not add FIZZBUZZ to tree: %s", t->error_str);
            char *old = msg_win_append(err_log, strdup(line));
            if (old != NULL) free(old);
        }
        pthread_mutex_unlock(&t_mutex);
    }
    
    pollfd_array *p = (pollfd_array *)info.user_data;
    int rc = pollfd_array_append_nodup(p, f->sfd, POLLIN | POLLHUP, f);
    if (rc < 0) {
        char line[80];
        sprintf(line, "Could not add new connection to pollfd array: %s", p->error_str);
        char *old = msg_win_append(err_log, strdup(line));
        if (old != NULL) free(old);
        return;
    }
}

//Don't forget: callbacks for when SIGWINCH is signalled

int main(int argc, char **argv) {    
    int rc;
    atexit(clean_screen);
    term_init();
    
    init_readline(got_rl_line);
    
    t = new_twm_tree();
    if (!t) {
        return -1;
    }
    
    //Set up thread that reads from keyboard/mouse. This will disappear soon
    //once I set up the call to poll() in the main event loop
    queue q = QUEUE_INITIALIZER;
    q.num_producers++;
    q.num_consumers++;    
    
    pthread_t prod;
    pthread_create(&prod, NULL, producer, &q);
    
    pollfd_array *pfd_arr = new_pollfd_array(16);
    new_fpga_connection(callback, argc > 1 ? argv[1] : "localhost", argc > 2 ? argv[2] : "5555", pfd_arr);
        
    err_log = new_msg_win("Message Window");
    
    pthread_mutex_lock(&t_mutex);
    rc = twm_tree_add_window(t, err_log, msg_win_draw_ops);
    pthread_mutex_unlock(&t_mutex);
    
    //Buffer for constructing strings to write to stdout
    char line[2048];
    int len = 0;
    
    textio_input in;
    
    int status_drawn = 0;
    //Draw "status bar"
    cursor_pos(1, term_rows);
    sprintf(line, "Soyez la bienvenue à la timonerie" ERASE_TO_END "%n", &len);
    write(1, line, len);
    status_drawn = 1;
    
    int mode = NORMAL;
    
    int num_samples = 0;
    unsigned long sum = 0;
    
    //Main event loop
    while(1) {
        char c;     
        
        pthread_mutex_lock(&t_mutex);
        //redraw_twm_node_tree(t->head);
        clock_t clk = clock();
        rc = twm_draw_tree(STDOUT_FILENO, t, 1, 11, term_cols, (term_rows - 2 - 8 - 2));
        clk = clock() - clk;
        sum += clk;
        num_samples++;
        if (num_samples == 500000) {
            char *msg = malloc(80);
            sprintf(msg, "Average time: %lf ms", ((double) sum / 500.0) / ((double) CLOCKS_PER_SEC));
            char *old = msg_win_append(err_log, msg);
            if (old) free(old);
            num_samples = 0;
            sum = 0;
        }
        if (rc < 0) {
            char *errmsg = malloc(80);
            sprintf(errmsg, "Could not draw tree: %s", t->error_str);
            char *old = msg_win_append(err_log, errmsg);
            if (old != NULL) free(old);
        } else if (rc > 0) {
            if (mode == READLINE) place_readline_cursor();
        }
        pthread_mutex_unlock(&t_mutex);
        
        //Get network data
        //Are any fds available for reading right now?
        rc = poll(pfd_arr->pfds, pfd_arr->num, 0);
        
        #ifdef DEBUG_ON
        if (num_poll_logs < 100) {
            fprintf(stderr, "poll returned %d\n", rc);
            fflush(stderr);
            num_poll_logs++;
        }
        #endif
        
        if (rc < 0) {
            int saved_errno = errno;
            char *errmsg = malloc(80);
            sprintf(errmsg, "Could not issue poll system call: %s", strerror(saved_errno));
            char *old = msg_win_append(err_log, errmsg);
            if (old != NULL) free(old);
        } else {
            //Traverse the active fds in pfd_arr
            struct pollfd *cur = NULL;
            while ((cur = pollfd_array_get_active(pfd_arr, cur)) != NULL) {
                //Check if this connection was lost
                if (cur->revents & POLLHUP) {
                    char *errmsg = malloc(80);
                    sprintf(errmsg, "A socket was unexpectedly closed");
                    char *old = msg_win_append(err_log, errmsg);
                    if (old != NULL) free(old);
                    //Remove from pollfd_array? The traversal function needs 
                    //to be smart...
                    continue;
                }
                
                //Get the fpga_connection_info associated with this fd
                void **v = pollfd_array_get_user_data(pfd_arr, cur);
                if (v == NULL) {
                    char *errmsg = malloc(80);
                    sprintf(errmsg, "Could not issue poll system call: %s", pfd_arr->error_str);
                    char *old = msg_win_append(err_log, errmsg);
                    if (old != NULL) free(old);
                    continue;
                }
                fpga_connection_info *f = (fpga_connection_info*)*v;
                
                rc = read_fpga_connection(f, cur->fd, 4, err_log);
                if (rc < 0) {
                    char *errmsg = malloc(80);
                    sprintf(errmsg, "Could not handle information from FPGA: %s", f->error_str);
                    char *old = msg_win_append(err_log, errmsg);
                    if (old != NULL) free(old);
                    continue;
                }
            }
            //Make sure no errors occurred while we traversed the active fds
            if (pfd_arr->error_str != PFD_ARR_SUCC) {
                    char *errmsg = malloc(80);
                    sprintf(errmsg, "pollfd_array_get_active_error: %s", pfd_arr->error_str);
                    char *old = msg_win_append(err_log, errmsg);
                    if (old != NULL) free(old);
            }
        }
        
        //Draw the message window
        //This is only temporay, to make it easier for me to see error
        //information
        len = draw_fn_msg_win(err_log, 1, 3, term_cols, 8, line);
        if (len > 0) {
            write(1, line, len);
            if (mode == READLINE) {
                //Put cursor in the right place
                place_readline_cursor();
            }
            err_log->need_redraw = 0;
        }
        
        //Draw "status bar"
        if (mode == NORMAL && !status_drawn) {
            cursor_pos(1, term_rows);
            sprintf(line, "Soyez la bienvenue à la timonerie" ERASE_TO_END "%n", &len);
            write(1, line, len);
            status_drawn = 1;
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
            del_fpga_connection(FIX_THIS);
            FIX_THIS = NULL;
            break;
        } else {
            if (mode == READLINE) {
                forward_to_readline(c);
            }
            int rc = textio_getch_cr(c, &in);
            if (rc == 0) {
                int len = 0;
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
                    if (mode == NORMAL)
                    switch(in.key) {
                    case TEXTIO_KEY_UP:
                        if (in.meta) {
                            rc = twm_tree_move_focused_node(t, TWM_UP);
                            if (rc < 0) {
                                char *errmsg = malloc(80);
                                sprintf(errmsg, "Could not move window up: %s", t->error_str);
                                char *old = msg_win_append(err_log, errmsg);
                                if (old) free(old);
                            }
                        } else if (!in.shift && !in.ctrl) {
                            rc = twm_tree_move_focus(t, TWM_UP);
                            if (rc < 0) {
                                char *errmsg = malloc(80);
                                sprintf(errmsg, "Could not move focus up: %s", t->error_str);
                                char *old = msg_win_append(err_log, errmsg);
                                if (old) free(old);
                            }
                        }
                        break;
                    case TEXTIO_KEY_DOWN:
                        if (in.meta) {
                            rc = twm_tree_move_focused_node(t, TWM_DOWN);
                            if (rc < 0) {
                                char *errmsg = malloc(80);
                                sprintf(errmsg, "Could not move window down: %s", t->error_str);
                                char *old = msg_win_append(err_log, errmsg);
                                if (old) free(old);
                            }
                        } else if (!in.shift && !in.ctrl) {
                            rc = twm_tree_move_focus(t, TWM_DOWN);
                            if (rc < 0) {
                                char *errmsg = malloc(80);
                                sprintf(errmsg, "Could not move focus down: %s", t->error_str);
                                char *old = msg_win_append(err_log, errmsg);
                                if (old) free(old);
                            }
                        }
                        break;
                    case TEXTIO_KEY_LEFT:
                        if (in.meta) {
                            rc = twm_tree_move_focused_node(t, TWM_LEFT);
                            if (rc < 0) {
                                char *errmsg = malloc(80);
                                sprintf(errmsg, "Could not move window left: %s", t->error_str);
                                char *old = msg_win_append(err_log, errmsg);
                                if (old) free(old);
                            }
                        } else if (!in.shift && !in.ctrl) {
                            rc = twm_tree_move_focus(t, TWM_LEFT);
                            if (rc < 0) {
                                char *errmsg = malloc(80);
                                sprintf(errmsg, "Could not move focus left: %s", t->error_str);
                                char *old = msg_win_append(err_log, errmsg);
                                if (old) free(old);
                            }
                        }
                        break;
                    case TEXTIO_KEY_RIGHT:
                        if (in.meta) {
                            rc = twm_tree_move_focused_node(t, TWM_RIGHT);
                            if (rc < 0) {
                                char *errmsg = malloc(80);
                                sprintf(errmsg, "Could not move window right: %s", t->error_str);
                                char *old = msg_win_append(err_log, errmsg);
                                if (old) free(old);
                            }
                        } else if (!in.shift && !in.ctrl) {
                            rc = twm_tree_move_focus(t, TWM_RIGHT);
                            if (rc < 0) {
                                char *errmsg = malloc(80);
                                sprintf(errmsg, "Could not move focus right: %s", t->error_str);
                                char *old = msg_win_append(err_log, errmsg);
                                if (old) free(old);
                            }
                        }
                        break;
                    default:
                        //Remove warnings
                        break;
                    }
                    
                    break;
                case TEXTIO_GETCH_ESCSEQ:
                    switch(in.code) {
                    case 'q':
                        if (mode == READLINE) {
                            status_drawn = 0;
                            mode = NORMAL;
                            enable_mouse_reporting();
                        } else {
                            rc = twm_tree_remove_focused(t);
                            if (rc < 0) {
                                char *error_msg = malloc(80);
                                sprintf(error_msg, "Could not delete window: %s", t->error_str);
                                char *old = msg_win_append(err_log, error_msg);
                                if (old) free(old);
                            }
                        }
                        break;
                    case 'v':
                        if (mode == NORMAL) {
                            rc = twm_set_stack_dir_focused(t, TWM_VERT);
                            if (rc < 0) {
                                char *error_msg = malloc(80);
                                sprintf(error_msg, "Could not set to vertical: %s", t->error_str);
                                char *old = msg_win_append(err_log, error_msg);
                                if (old) free(old);
                            }
                        }
                        break;
                    case 'h':
                        if (mode == NORMAL) {
                            rc = twm_set_stack_dir_focused(t, TWM_HORZ);
                            if (rc < 0) {
                                char *error_msg = malloc(80);
                                sprintf(error_msg, "Could not set to horizontal: %s", t->error_str);
                                char *old = msg_win_append(err_log, error_msg);
                                if (old) free(old);
                            }
                        }
                        break;
                    case 'w':
                        if (mode == NORMAL) {
                            rc = twm_toggle_stack_dir_focused(t);
                            if (rc < 0) {
                                char *error_msg = malloc(80);
                                sprintf(error_msg, "Could not toggle stack direction: %s", t->error_str);
                                char *old = msg_win_append(err_log, error_msg);
                                if (old) free(old);
                            }
                        }
                        break;
                    case 'a':
                        if (mode == NORMAL) {
                            rc = twm_tree_move_focus(t, TWM_PARENT);
                            if (rc < 0) {
                                char *error_msg = malloc(80);
                                sprintf(error_msg, "Could not move focus up: %s", t->error_str);
                                char *old = msg_win_append(err_log, error_msg);
                                if (old) free(old);
                            }
                        }
                        break;
                    case 'z':
                        if (mode == NORMAL) {
                            rc = twm_tree_move_focus(t, TWM_CHILD);
                            if (rc < 0) {
                                char *error_msg = malloc(80);
                                sprintf(error_msg, "Could not move focus down: %s", t->error_str);
                                char *old = msg_win_append(err_log, error_msg);
                                if (old) free(old);
                            }
                        }
                        break;
                    default:
                        sprintf(line, "You entered some kind of escape sequence ending in %c" ERASE_TO_END "%n", in.code, &len);
                        break;
                    }
                    break;
                case TEXTIO_GETCH_MOUSE: {
                    if (FIX_THIS != NULL) {
                        dbg_guv *g = &FIX_THIS->guvs[0];
                        dbg_guv *h = &FIX_THIS->guvs[1];
                        //Just for fun: use scrollwheel inside dbg_guv
                        if (in.btn == TEXTIO_WUP) {
                            g->need_redraw = 1;
                            if (g->log_pos < DBG_GUV_SCROLLBACK - 1) g->log_pos++;
                            h->need_redraw = 1;
                            if (h->log_pos < DBG_GUV_SCROLLBACK - 1) h->log_pos++;
                            err_log->need_redraw = 1;
                            if (err_log->buf_offset < MSG_WIN_SCROLLBACK - 1) err_log->buf_offset++;
                        } else if (in.btn == TEXTIO_WDN) {
                            g->need_redraw = 1;
                            if (g->log_pos > 0) g->log_pos--;
                            h->need_redraw = 1;
                            if (h->log_pos > 0) h->log_pos--;
                            err_log->need_redraw = 1;
                            if (err_log->buf_offset > 0) err_log->buf_offset--;
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
                if (len != 0) write(1, line, len); //Writing a length of 0 has weird effects
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
    
    dummy *cur = dummy_head;
    while (cur) {
        dummy *next = cur->next;
        free(cur);
        cur = next;
    }
    
    free_msg_win_logs(err_log);
    del_msg_win(err_log);
    
    pthread_join(prod, NULL);
#ifdef DEBUG_ON
    fprintf(stderr, "Joined prod\n");
#endif
    
    del_fpga_connection(FIX_THIS);
    clean_screen();
    return 0;
}
