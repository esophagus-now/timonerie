#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <signal.h>
#include <event2/event.h>
#include <pthread.h>
#include <unistd.h>
#include "textio.h"
#include "dbg_guv.h"
#include "dbg_cmd.h"
#include "twm.h"

#define write_const_str(x) write(1, x, sizeof(x))

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

//You know what? It's time to embrace the globals! I need to simplify my
//life if I'm ever gonna finish this
struct event_base *ev_base = NULL;

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

void fpga_conn_cb(new_fpga_cb_info info); //Forward-declare

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
            
            rc = new_fpga_connection(fpga_conn_cb, cmd.node, cmd.serv, e);
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
        case CMD_NAME: {
            symtab_entry *e = symtab_lookup(ids, cmd.id);
            if (e != NULL) {
                cursor_pos(1, term_rows - 1);
                write_const_str("This ID is already in use" ERASE_TO_END);
                break;
            }
            
            dbg_guv *g = twm_tree_get_focused_as(t, draw_fn_dbg_guv);
            if (g == NULL) {
                cursor_pos(1, term_rows-1);
                sprintf(line, "This is not a dbg_guv" ERASE_TO_END "%n", &len);
                write(1, line, len);
                return;
            } 
            
            dbg_guv_set_name(g, cmd.id);
            
            sem_val symdat = {
                .type = SYM_DG,
                .v = g
            };
            
            int rc = symtab_append(ids, cmd.id, &symdat, sizeof(symdat));
            if (rc < 0) {
                char line[80];
                sprintf("Could not append symbol to table: %s", ids->error_str);
                msg_win_dynamic_append(err_log, line);
            }
            
            break;
        }
        case CMD_MSG: {
            int rc = twm_tree_focus_item(t, err_log);
            if (t->error_str == TWM_NOT_FOUND) {
                int rc = twm_tree_add_window(t, err_log, msg_win_draw_ops);
                if (rc < 0) {
                    //For once I won't put it in the message window, since
                    //the user wouldn't see it!
                    cursor_pos(1, term_rows - 1);
                    char line[80];
                    int len;
                    sprintf(line, "Could not show message window: %s" ERASE_TO_END "%n", t->error_str, &len);
                    write(STDOUT_FILENO, line, len);
                    break;
                }
            } else if (rc < 0) {
                //For once I won't put it in the message window, since
                //the might not see it!
                cursor_pos(1, term_rows - 1);
                char line[80];
                int len;
                sprintf(line, "Could not focus message window: %s" ERASE_TO_END "%n", t->error_str, &len);
                write(STDOUT_FILENO, line, len);
                break;
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
        case CMD_QUIT: 
            //end libevent event loop
            event_base_loopbreak(ev_base);
            break;
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

void twm_resize_cb(void) {                        
    int rc = twm_tree_redraw(t);
    if (rc < 0) {
        char errmsg[80];
        sprintf(errmsg, "Could not issue redraw: %s", t->error_str);
        msg_win_dynamic_append(err_log, errmsg);
    }
    
    readline_redisplay();
}

void draw_cb(evutil_socket_t fd, short what, void *arg) {
    pthread_mutex_lock(&t_mutex);
    
    int rc = twm_draw_tree(STDOUT_FILENO, t, 1, 1, term_cols, term_rows - 2);
    if (rc < 0) {
        char errmsg[80];
        sprintf(errmsg, "Could not draw tree: %s", t->error_str);
        msg_win_dynamic_append(err_log, errmsg);
    } else if (rc > 0) {
        place_readline_cursor();
    }
    
    pthread_mutex_unlock(&t_mutex);
}

void fpga_read_cb(evutil_socket_t fd, short what, void *arg) {
    fpga_connection_info *f = arg;
    
    int rc = read_fpga_connection(f, f->sfd, 4);
    if (rc < 0) {
        char errmsg[80];
        sprintf(errmsg, "Could not read from FPGA: %s", f->error_str);
        msg_win_dynamic_append(err_log, errmsg);
        event_del(f->ev);
        //TODO: properly close out FCI; free its resources, remove its
        //windows from the TWM, remove its IDs, and remove it from the 
        //global list to free on program exit
    }
}

void handle_stdin_cb(evutil_socket_t fd, short what, void *arg) {
    //Although it's a global, this function takes the event_base as the arg
    //It just means that I won't have to update this function if I find a
    //nice way to get rid of the global variables
    struct event_base *base = arg;
    
    //Sometimes timonerie uses a special key. However, if it doesn't use it,
    //we should pass it to readline
    static char ansi_code[16];
    static int ansi_code_pos = 0;
    
    //Scratchpad for building error messages
    char errmsg[80];
    
    //Slow to read one character at a time, but who cares? The user isn't
    //typing super fast, and timonerie is not meant to be used by piping
    //into its stdin (in fact, it will quit if not used with a TTY).
    char c;
    read(STDIN_FILENO, &c, 1);
    
    //If user pressed CTRL-D we can quit
    if (c == '\x04') {
        event_base_loopbreak(base);
    }
    
    ansi_code[ansi_code_pos++] = c;
    
    textio_input in;
    int rc = textio_getch_cr(c, &in);
    if (rc == 0) {
        //Assume we've used the ansi code we've been saving, until
        //it turns out we didn't
        int used_ansi_code = 1;
        
        switch(in.type) {
        case TEXTIO_GETCH_PLAIN: {
            if (in.c == 12) {
                twm_resize_cb(); //This is the callback that textio calls on SIGWINCH events
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
        
        if (!used_ansi_code) {
            ansi_code[ansi_code_pos] = 0;
            readline_sendstr(ansi_code);
        }
        
        ansi_code_pos = 0;
    } else if (rc < 0) {
        sprintf(errmsg, "Bad input, why = %s, smoking_gun = 0x%02x", in.error_str, in.smoking_gun & 0xFF);
        msg_win_dynamic_append(err_log, errmsg);
    }
}

//TODO: function that runs on every event loop and tries calling timonier
//update functions

void fpga_conn_cb(new_fpga_cb_info info) {
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
    
    //Hook up a new event to read data from the connection
    struct event *read_ev = event_new(ev_base, f->sfd, EV_READ | EV_PERSIST, fpga_read_cb, f);
    event_add(read_ev, NULL);
    f->ev = read_ev;
    
    sym_dat(e, sem_val*)->type = SYM_FCI;
    sym_dat(e, sem_val*)->v = f;
}

int main(int argc, char **argv) {    
    //Setup the TWM screen
    atexit(clean_screen);
    term_init(0);
    init_readline(got_rl_line);
    
    t = new_twm_tree();
    if (!t) {
        fprintf(stderr, "Could not start TWM\n");
        return -1;
    }
    
    set_resize_cb(twm_resize_cb); //Auto-redraw when terminal is resized
    
    //Initialize our (EVIL) global symbol table
    ids = new_symtab(32);
    
    //Set up the message window, and show it by default in the TWM    
    err_log = new_msg_win("Message Window");
    
    pthread_mutex_lock(&t_mutex);
    twm_tree_add_window(t, err_log, msg_win_draw_ops);
    pthread_mutex_unlock(&t_mutex);
    
    //Set up libevent. I ended up just making the base a global; it was a 
    //lot easier that way
    ev_base = event_base_new();
    
    //Event for stdin
    struct event *input_ev = event_new(ev_base, STDIN_FILENO, EV_READ | EV_PERSIST, handle_stdin_cb, ev_base);
    event_add(input_ev, NULL);
    
    //Event for perdiocally drawing the TWM every 50 ms
    struct event *draw_ev = event_new(ev_base, -1, EV_TIMEOUT | EV_PERSIST, draw_cb, NULL);
    event_add(draw_ev, (struct timeval[1]){{0, 50*1000}});
    
    //Events for reading from FPGA connections are added by fpga_conn_cb, 
    //which is triggered when a connection is succesfully opened 
    
    //Main event loop
    event_base_dispatch(ev_base);
    
    //Free list of dummy windows (will delete dummy windows once I'm done
    //debugging)
    //TODO: add optional exit function to TWM windows so that we don't have
    //to maintain a list?
    //  -> That's pretty easy, so I'll do that later today
    dummy *cur = dummy_head;
    while (cur) {
        dummy *next = cur->next;
        free(cur);
        cur = next;
    }
    
    //Close any open FPGA connections. Technically we don't have to do this,
    //since Linux will do it anwyay. 
    fci_list *f = fci_head.next;
    while (f != &fci_head) {
        fci_list *next = f->next;
        del_fpga_connection(f->f);
        free(f);
        f = next;
    }
    
    //Again, technically unnecessary
    free_msg_win_logs(err_log);
    del_msg_win(err_log);
    
    //Return the terminal to its original state    
    clean_screen();
    return 0;
}
