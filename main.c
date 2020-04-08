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

#define write_const_str(x) write(1, x, sizeof(x))

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
    
//This is the window that shows messages going by
msg_win *err_log = NULL;

twm_tree *t = NULL; 
pthread_mutex_t t_mutex = PTHREAD_MUTEX_INITIALIZER;

//Unfortunately, this has to be global, since got_rl_line needs access to it
//in order to give it to new_fpga_connection so that it will be passed into
//callback... me wonders if this is getting out of hand
pollfd_array *pfd_arr = NULL;

//You know what? It's time to embrace the globals! I need to simplify my
//life if I'm ever gonna finish this
typedef enum {
    SYM_FCI,
    SYM_DG
} sym_type;

typedef struct {
    sym_type type;
    void *v;
} sem_val;

static symtab *ids = NULL;

typedef struct _fci_list {
    fpga_connection_info *f;
    struct _fci_list *prev, *next;
} fci_list;

fci_list fci_head = {
    .f = NULL,
    .prev = &fci_head,
    .next = &fci_head
};

void callback(new_fpga_cb_info info) {
    fpga_connection_info *f = info.f;
    symtab_entry *e = (symtab_entry*) info.user_data;
    if (f == NULL) {
        char line[80];
        sprintf(line, "Could not connect to FPGA: %s", info.error_str);
        msg_win_dynamic_append(err_log, line);
        symtab_array_remove(ids, e);
        return;
    } else {
        char line[120];
        sprintf(line, "Connection [%s] opened", e->sym);
        msg_win_dynamic_append(err_log, line);
    }
    
    fci_list *n = malloc(sizeof(fci_list));
    n->f = f;
    n->prev = &fci_head;
    n->next = fci_head.next;
    fci_head.next->prev = n;
    fci_head.next = n;
    
    //TEMPORARY: until I add the commands, we'll do this for now
    /*dbg_guv *g = &f->guvs[0];
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
    }*/
    
    pollfd_array *p = pfd_arr;
    int rc = pollfd_array_append_nodup(p, f->sfd, POLLIN | POLLHUP, f);
    if (rc < 0) {
        char line[80];
        sprintf(line, "Could not add new connection to pollfd array: %s", p->error_str);
        char *old = msg_win_append(err_log, strdup(line));
        if (old != NULL) free(old);
        return;
    }
    
    sym_dat(e, sem_val*)->type = SYM_FCI;
    sym_dat(e, sem_val*)->v = f;
}

void got_rl_line(char *str) {    
    cursor_pos(1,2);
    char line[80];
    int len;
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
        case CMD_OPEN: {
            symtab_entry *e = symtab_lookup(ids, cmd.id);
            if (e != NULL) {
                cursor_pos(1, term_rows - 1);
                write_const_str("This ID is already in use" ERASE_TO_END);
                break;
            }
            
            int rc = symtab_append(ids, cmd.id, NULL, 0);
            if (rc < 0) {
                char line[80];
                sprintf("Could not append symbol to table: %s", ids->error_str);
                msg_win_dynamic_append(err_log, line);
                break;
            }
            
            //Slow, but who cares?
            e = symtab_lookup(ids, cmd.id);
            if (e == NULL) {
                char line[80];
                sprintf("Fatal symol table error. Maybe this will help: %s", ids->error_str);
                msg_win_dynamic_append(err_log, line);
                break;
            }
            
            rc = new_fpga_connection(callback, cmd.node, cmd.serv, e);
            if (rc < 0) {
                msg_win_dynamic_append(err_log, "Could not open FPGA connection");
            }
            break;
        }
        case CMD_SEL: {
            symtab_entry *e = symtab_lookup(ids, cmd.id);
            if (!e) {
                char line[120];
                sprintf(line, "Could not find [%s]: %s", cmd.id, ids->error_str);
                msg_win_dynamic_append(err_log, line);
                break;
            }
            dbg_guv *selected;
            if (sym_dat(e, sem_val*)->type == SYM_FCI) {
                if (!cmd.has_guv_addr) {
                    cursor_pos(1, term_rows - 1);
                    write_const_str(DBG_CMD_SEL_USAGE);
                    break;
                }
                fpga_connection_info *f = sym_dat(e, sem_val*)->v;
                selected = f->guvs + cmd.dbg_guv_addr;
            } else {
                selected = sym_dat(e, sem_val*)->v;
            }
            
            int rc = twm_tree_focus_item(t, selected);
            if (t->error_str == TWM_NOT_FOUND) {
                int rc = twm_tree_add_window(t, selected, dbg_guv_draw_ops);
                if (rc < 0) {
                    char line[80];
                    sprintf(line, "Could not add guv to display: %s", t->error_str);
                    msg_win_dynamic_append(err_log, line);
                }
            } else if (rc < 0) {
                char line[80];
                sprintf(line, "Error while searching tree: %s", t->error_str);
                msg_win_dynamic_append(err_log, line);
            }
            break;
        }
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
            int dbg_guv_addr;
            dbg_guv *g = twm_tree_get_focused_as(t, draw_fn_dbg_guv);
            if (g == NULL) {
                cursor_pos(1, term_rows-1);
                sprintf(line, "This is not a dbg_guv" ERASE_TO_END "%n", &len);
                write(1, line, len);
                return;
            } 
            dbg_guv_addr = g->addr;
            
            cursor_pos(1, term_rows-1);
            if (cmd.reg == LATCH) {
                sprintf(line, "Committing values to guv[%d]" ERASE_TO_END "%n", 
                    dbg_guv_addr,
                    &len
                );
            } else {
                sprintf(line, "Writing 0x%08x (%u) to guv[%d]::%s" ERASE_TO_END "%n", 
                    cmd.param, 
                    cmd.param,
                    dbg_guv_addr,
                    DBG_GUV_REG_NAMES[cmd.reg],
                    &len
                );
            }
            write(1, line, len);
            
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
            unsigned cmd_addr = (dbg_guv_addr << 4) | cmd.reg;
            queue_write(&g->parent->egress, (char*) &cmd_addr, sizeof(cmd_addr));
            if (cmd.has_param) queue_write(&g->parent->egress, (char*) &cmd.param, sizeof(cmd.param));
            break;
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

//Don't forget: callbacks for when SIGWINCH is signalled

int main(int argc, char **argv) {    
    int rc;
    atexit(clean_screen);
    term_init(0);
    
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
    
    //Initialize our (Evil) globals
    pfd_arr = new_pollfd_array(16);
    ids = new_symtab(32);
    //new_fpga_connection(callback, argc > 1 ? argv[1] : "localhost", argc > 2 ? argv[2] : "5555", pfd_arr);
        
    err_log = new_msg_win("Message Window");
    
    pthread_mutex_lock(&t_mutex);
    rc = twm_tree_add_window(t, err_log, msg_win_draw_ops);
    pthread_mutex_unlock(&t_mutex);
    
    //Buffer for constructing strings to write to stdout
    char line[80];
    int len = 0;
    
    //Draw initial message
    cursor_pos(1, term_rows - 1);
    sprintf(line, "Soyez la bienvenue à la timonerie" ERASE_TO_END "%n", &len);
    write(1, line, len);
    
    //Main event loop
    char ansi_code[16];
    int ansi_code_pos = 0;
    
    clock_t last_redraw = clock();
    while(1) {
        char c;  
        char errmsg[80];   
        
        pthread_mutex_lock(&t_mutex);
        clock_t now = clock();
        //Prevent crazy screen slowdown by not redrawing twice (or more)
        //inside of 50 milliseconds
        if (((double) (now - last_redraw) / (double) CLOCKS_PER_SEC) > 0.050) {
            rc = twm_draw_tree(STDOUT_FILENO, t, 1, 1, term_cols, term_rows - 2);
            last_redraw = clock();
            if (rc < 0) {
                sprintf(errmsg, "Could not draw tree: %s", t->error_str);
                msg_win_dynamic_append(err_log, errmsg);
            } else if (rc > 0) {
                place_readline_cursor();
            }
        }
        pthread_mutex_unlock(&t_mutex);
        
        //Get network data
        //Are any fds available for reading right now?
        rc = poll(pfd_arr->pfds, pfd_arr->num, 0);
        
        if (rc < 0) {
            int saved_errno = errno;
            sprintf(errmsg, "Could not issue poll system call: %s", strerror(saved_errno));
            msg_win_dynamic_append(err_log, errmsg);
        } else {
            //Traverse the active fds in pfd_arr
            struct pollfd *cur = NULL;
            while ((cur = pollfd_array_get_active(pfd_arr, cur)) != NULL) {
                //Check if this connection was lost
                if (cur->revents & POLLHUP) {
                    sprintf(errmsg, "A socket was unexpectedly closed");
                    msg_win_dynamic_append(err_log, errmsg);
                    //Remove from pollfd_array? The traversal function needs 
                    //to be smart...
                    break;
                }
                
                //Get the fpga_connection_info associated with this fd
                void **v = pollfd_array_get_user_data(pfd_arr, cur);
                if (v == NULL) {
                    sprintf(errmsg, "Could not issue poll system call: %s", pfd_arr->error_str);
                    msg_win_dynamic_append(err_log, errmsg);
                    continue;
                }
                fpga_connection_info *f = (fpga_connection_info*)*v;
                
                rc = read_fpga_connection(f, cur->fd, 4, err_log);
                if (rc < 0) {
                    sprintf(errmsg, "Could not handle information from FPGA: %s", f->error_str);
                    msg_win_dynamic_append(err_log, errmsg);
                    continue;
                }
            }
            //Make sure no errors occurred while we traversed the active fds
            if (pfd_arr->error_str != PFD_ARR_SUCC) {
                    sprintf(errmsg, "pollfd_array_get_active_error: %s", pfd_arr->error_str);
                    msg_win_dynamic_append(err_log, errmsg);
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
        
        
        //If user pressed CTRL-D we can quit
        if (c == '\x04') {
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
            break;
        } else {
            ansi_code[ansi_code_pos++] = c;
            //sprintf(errmsg, "ansi_code_pos = %d", ansi_code_pos);
            //msg_win_dynamic_append(err_log, errmsg);
            
            textio_input in;
            int rc = textio_getch_cr(c, &in);
            if (rc == 0) {
                //Assume we've used the ansi code we've been saving, until
                //it turns out we didn't
                int used_ansi_code = 1;
                
                switch(in.type) {
                case TEXTIO_GETCH_PLAIN: {
                    if (in.c == 12) {
                        int rc = twm_tree_redraw(t);
                        if (rc < 0) {
                            sprintf(errmsg, "Could not issue redraw: %s", t->error_str);
                            msg_win_dynamic_append(err_log, errmsg);
                        }
                    }
                }
                case TEXTIO_GETCH_FN_KEY: {
                    int dir;
                    int got_arrow_key = 1;
                    switch(in.key) {
                    case TEXTIO_KEY_UP:
                        dir = TWM_UP;
                        break;
                    case TEXTIO_KEY_DOWN:
                        dir = TWM_DOWN;
                        break;
                    case TEXTIO_KEY_LEFT:
                        dir = TWM_LEFT;
                        break;
                    case TEXTIO_KEY_RIGHT:
                        dir = TWM_RIGHT;
                        break;
                    default:
                        got_arrow_key = 0;
                        break;
                    }
                    
                    if (got_arrow_key) {
                        //Let the compiler optimize these predicates
                        if (in.meta && in.shift && !in.ctrl) {
                            int rc = twm_tree_move_focused_node(t, dir);
                            if (rc < 0) {
                                sprintf(errmsg, "Could not move window: %s", t->error_str);
                                msg_win_dynamic_append(err_log, errmsg);
                            }
                        } else if (in.meta && !in.shift && !in.ctrl) {
                            int rc = twm_tree_move_focus(t, dir);
                            if (rc < 0) {
                                sprintf(errmsg, "Could not move focus: %s", t->error_str);
                                msg_win_dynamic_append(err_log, errmsg);
                            }
                        } else if (!in.meta && in.ctrl) {
                            dbg_guv *g = twm_tree_get_focused_as(t, draw_fn_dbg_guv);
                            if (g) {
                                if (dir == TWM_UP) dbg_guv_scroll(g, in.shift ? 10: 1);
                                if (dir == TWM_DOWN) dbg_guv_scroll(g, in.shift ? -10: -1);
                            }
                            msg_win *m = twm_tree_get_focused_as(t, draw_fn_msg_win);
                            if (m) {
                                if (dir == TWM_UP) msg_win_scroll(m, in.shift ? 10: 1);
                                if (dir == TWM_DOWN) msg_win_scroll(m, in.shift ? -10: -1);
                            }
                            
                            if (g == NULL && m == NULL) {
                                msg_win_dynamic_append(err_log, "Not a scrollable window");
                            }
                        } else {
                            used_ansi_code = 0;
                        }
                    } else {
                        used_ansi_code = 0;
                    }
                    
                    break;
                }
                case TEXTIO_GETCH_ESCSEQ:
                    switch(in.code) {
                    case 'q':
                        rc = twm_tree_remove_focused(t);
                        if (rc < 0) {
                            sprintf(errmsg, "Could not delete window: %s", t->error_str);
                            msg_win_dynamic_append(err_log, errmsg);
                        }
                        break;
                    case 'v':
                        rc = twm_set_stack_dir_focused(t, TWM_VERT);
                        if (rc < 0) {
                            sprintf(errmsg, "Could not set to vertical: %s", t->error_str);
                            msg_win_dynamic_append(err_log, errmsg);
                        }
                        break;
                    case 'h':
                        rc = twm_set_stack_dir_focused(t, TWM_HORZ);
                        if (rc < 0) {
                            sprintf(errmsg, "Could not set to horizontal: %s", t->error_str);
                            msg_win_dynamic_append(err_log, errmsg);
                        }
                        break;
                    case 'w':
                        rc = twm_toggle_stack_dir_focused(t);
                        if (rc < 0) {
                            sprintf(errmsg, "Could not toggle stack direction: %s", t->error_str);
                            msg_win_dynamic_append(err_log, errmsg);
                        }
                        break;
                    case 'a':
                        rc = twm_tree_move_focus(t, TWM_PARENT);
                        if (rc < 0) {
                            sprintf(errmsg, "Could not move focus up: %s", t->error_str);
                            msg_win_dynamic_append(err_log, errmsg);
                        }
                        break;
                    case 'z':
                        rc = twm_tree_move_focus(t, TWM_CHILD);
                        if (rc < 0) {
                            sprintf(errmsg, "Could not move focus down: %s", t->error_str);
                            msg_win_dynamic_append(err_log, errmsg);
                        }
                        break;
                    default:
                        used_ansi_code = 0;
                        break;
                    }
                    break;
                default:
                    used_ansi_code = 0;
                    break;
                }
                
                //sprintf(errmsg, "used_ansi_code = %d", used_ansi_code);
                //msg_win_dynamic_append(err_log, errmsg);
                
                if (!used_ansi_code) {
                    /*int pos = 0;
                    int tmp;
                    sprintf(errmsg + pos, "Passing (%d) %n", ansi_code_pos, &tmp);
                    pos += tmp;
                    int i;
                    for (i = 0; i < ansi_code_pos; i++) {
                        sprintf(errmsg + pos, "%02x %n", +ansi_code[i] & 0xFF, &tmp);
                        pos += tmp;
                    }
                    sprintf(errmsg + pos, "to readline");
                    msg_win_dynamic_append(err_log, errmsg);*/
                    ansi_code[ansi_code_pos] = 0;
                    readline_sendstr(ansi_code);
                }
                
                ansi_code_pos = 0;
            } else if (rc < 0) {
                sprintf(line, "Bad input, why = %s, smoking_gun = 0x%02x", in.error_str, in.smoking_gun & 0xFF);
                msg_win_dynamic_append(err_log, line);
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
    
    fci_list *f = fci_head.next;
    while (f != &fci_head) {
        fci_list *next = f->next;
        del_fpga_connection(f->f);
        free(f);
        f = next;
    }
    
    free_msg_win_logs(err_log);
    del_msg_win(err_log);
    
    pthread_join(prod, NULL);
#ifdef DEBUG_ON
    fprintf(stderr, "Joined prod\n");
#endif
    
    clean_screen();
    return 0;
}
