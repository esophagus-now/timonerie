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
#include <unistd.h>
#include "timonier.h"
#include "textio.h"
#include "dbg_guv.h"
#include "dbg_cmd.h"
#include "twm.h"

#define write_const_str(x) write(1, x, sizeof(x))

//This is the window that shows messages going by
msg_win *err_log = NULL;

//Include temprary TWM exercise code
#include "TEMPORARY.txt"

twm_tree *t = NULL; 

//You know what? It's time to embrace the globals! I need to simplify my
//life if I'm ever gonna finish this
struct event_base *ev_base = NULL;

typedef enum {
	SYM_UNINITIALIZED,
    SYM_FCI,
    SYM_DG
} sym_type;

typedef struct _sem_val{
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

//Prototypes for helper functions
int get_nb_sock(char const *node, char const *serv, char const* *error_str);
void cleanup_fpga_connection(fpga_connection_info *f);

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
    int rc = twm_draw_tree(STDOUT_FILENO, t, 1, 1, term_cols, term_rows - 2);
    if (rc < 0) {
        char errmsg[80];
        sprintf(errmsg, "Could not draw tree: %s", t->error_str);
        msg_win_dynamic_append(err_log, errmsg);
    } else if (rc > 0) {
        place_readline_cursor();
    }
}

void fpga_read_cb(evutil_socket_t fd, short what, void *arg) {
    fpga_connection_info *f = arg;
    
    int rc = read_fpga_connection(f, fd, 4);
    if (rc < 0) {
        char errmsg[80];
        sprintf(errmsg, "Could not read from FPGA: %s. Closing...", f->error_str);
        msg_win_dynamic_append(err_log, errmsg);
        cleanup_fpga_connection(f);
    }
}

void fpga_write_cb(evutil_socket_t fd, short what, void *arg) {
    fpga_connection_info *f = arg;
    
    int rc = write_fpga_connection(f, fd);
    if (rc < 0) {
        char errmsg[80];
        sprintf(errmsg, "Could not write to FPGA: %s. Closing...", f->error_str);
        msg_win_dynamic_append(err_log, errmsg);
        cleanup_fpga_connection(f);
    }
}

void fpga_conn_cb(evutil_socket_t fd, short what, void *arg) {
    char *id = arg;
    symtab_entry *e = symtab_lookup(ids, id);
    if (e == NULL) {
		char line[80];
		sprintf(line, "Symbol table error. Maybe this will help: %s", ids->error_str);
		msg_win_dynamic_append(err_log, line);
		close(fd);
		return;
	}
    
    //Check if connection succeeded
    int rc, result;
    rc = getsockopt(fd, SOL_SOCKET, SO_ERROR, &result, (socklen_t[1]){sizeof(int)});
    //Pedantic, but read Henry Spencer's 10 Commandments of C vis-à-vis
    //checking error return codes
    if (rc < 0) {
        char line[80];
        sprintf(line, "Could not query connection status: %s", strerror(errno));
        msg_win_dynamic_append(err_log, line);
        symtab_array_remove(ids, e);
        free(id);
        //TODO: can/should we close fd here?
        return;
	}
    //Now we can actually check the connection status
    if (result != 0) {
        char line[80];
        sprintf(line, "Could not connect to FPGA: %s", strerror(result));
        msg_win_dynamic_append(err_log, line);
        symtab_array_remove(ids, e);
        close(fd);
        free(id);
        return;
	}
    
    fpga_connection_info *f = new_fpga_connection();
    if (f == NULL) {
        msg_win_dynamic_append(err_log, "Could not open FPGA: out of memory");
        symtab_array_remove(ids, e);
        close(fd);
        free(id);
	}
	
	char line[120];
    sprintf(line, "Connection [%s] opened", e->sym);
    msg_win_dynamic_append(err_log, line);
    
    fci_list *n = malloc(sizeof(fci_list));
    n->f = f;
    n->prev = &fci_head;
    n->next = fci_head.next;
    fci_head.next->prev = n;
    fci_head.next = n;
    
    //Hook up new events to read and write data from the connection
    struct event *read_ev = event_new(ev_base, fd, EV_READ | EV_PERSIST, fpga_read_cb, f);
    event_add(read_ev, NULL);
    f->rd_ev = read_ev;
    
    struct event *write_ev = event_new(ev_base, fd, EV_WRITE, fpga_write_cb, f);
    //Don't add the event yet; nothing to write
    f->wr_ev = write_ev;
    
    //Set symbol and save name
    sym_dat(e, sem_val*)->type = SYM_FCI;
    sym_dat(e, sem_val*)->v = f;
    f->name = id; //SUBTLE: this string has already been copied. We are 
    //essentially handing off ownership of the memory here, but for sure
    //I should check valgrind
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
    
    static textio_input in; //VERY subtle! I forgot that textio_getch_cr
    //saves some state inside the textio_input struct. TODO: maybe fix
    //textio_getch_cr so that it maintains a local textio_input for its
    //state then only copies it out on a match
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

void got_rl_line(char *str) {    
    cursor_pos(1,2);
    char line[80];
    int len;
    /* If the line has any text in it, save it on the history. */
    if (str && *str) {
        dbg_cmd cmd;
        //Try to find active dbg_guv
        dbg_guv *g = twm_tree_get_focused_as(t, draw_fn_dbg_guv);
        
        int rc = parse_dbg_cmd(&cmd, str);
        if (rc < 0) {
			//At this point, we did not match a built-in command. Now 
			//see if there Is an active dbg_guv, and ask it if it can 
			//use this command string
			if (g != NULL && g->ops.got_line != NULL) {
				rc = g->ops.got_line(g, str);
				cmd.type = CMD_HANDLED;
				//Propagate error string in case an error occurred
				cmd.error_str = g->error_str;
			}
		}
		
        if (rc < 0) {
			//Okay, we are really out of options
            cursor_pos(1, term_rows-1);
            sprintf(line, "Parse error: %s" ERASE_TO_END "%n", cmd.error_str, &len);
            write(1, line, len);
            return;
        }
        
        add_history(str);
        
        switch(cmd.type) {
        case CMD_OPEN: {
            //TODO: check if user mistakenly reopens an existing connection
            
            msg_win_dynamic_append(err_log, "Warning: no one is making sure you don't open a connection twice");
            
            symtab_entry *e = symtab_lookup(ids, cmd.id);
            if (e != NULL) {
                cursor_pos(1, term_rows - 1);
                write_const_str("This ID is already in use" ERASE_TO_END);
                break;
            }
            
            sem_val uninit = {
				.type = SYM_UNINITIALIZED,
				.v = NULL
			};
            
            int rc = symtab_append(ids, cmd.id, &uninit, sizeof(uninit));
            if (rc < 0) {
                char line[80];
                sprintf("Could not append symbol to table: %s", ids->error_str);
                msg_win_dynamic_append(err_log, line);
                break;
            }
            
            char const *error_str;
            int sfd = get_nb_sock(cmd.node, cmd.serv, &error_str);
            
            if (sfd < 0) {
                char line[80];
                sprintf(line, "Could not open socket: %s", error_str);
                msg_win_dynamic_append(err_log, line);
                break;
			}
            
            //Hook up event listener that waits for the connection to complete
            //SUBTLE: cmd.id is copied here. First of all, cmd.id is a static
            //buffer. Also, eventually, this string will be saved inside an
            //fpga_connection_info struct (I can't remember why this is needed)
            //and when that struct is destroyed the copied memory will be freed
            event_base_once(ev_base, sfd, EV_WRITE, fpga_conn_cb, strdup(cmd.id), NULL);
            break;
        }
        case CMD_CLOSE: {
            symtab_entry *e = symtab_lookup(ids, cmd.id);
            if (!e) {
                char line[120];
                sprintf(line, "Could not find [%s]: %s", cmd.id, ids->error_str);
                msg_win_dynamic_append(err_log, line);
                break;
            }
            if (sym_dat(e, sem_val*)->type == SYM_FCI) {
                fpga_connection_info *f = sym_dat(e, sem_val*)->v;
                cleanup_fpga_connection(f);
            } else {
                dbg_guv *g = sym_dat(e, sem_val*)->v;
                int rc = twm_tree_remove_item(t, g);
                if (rc < 0) {
                    char line[80];
                    sprintf(line, "Error closing dbg_guv window: %s", t->error_str);
                    msg_win_dynamic_append(err_log, line);
                }
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
            } else if (sym_dat(e, sem_val*)->type == SYM_DG) {
                selected = sym_dat(e, sem_val*)->v;
            } else {
				msg_win_dynamic_append(err_log, "This symbol is not an FPGA or dbg_guv");
				break;
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
        case CMD_MGR: {
            if (g == NULL) {
                cursor_pos(1, term_rows-1);
                sprintf(line, "This is not a dbg_guv" ERASE_TO_END "%n", &len);
                write(1, line, len);
                return;
            } 
            
            //TODO (maybe): this just checks a hardcoded list of options.
            //It would be difficult to add a new manager, but then again,
            //it would take more time than necesary to make this "nice"...
            
            //Also: this is a super slow condition, but NO ONE CARES
            if (!strncmp(cmd.id, "int", sizeof(cmd.id)) && g->ops.draw_ops.draw_fn != default_guv_ops.draw_ops.draw_fn) {
                if (g->ops.cleanup_mgr != NULL) g->ops.cleanup_mgr(g);
                g->ops = default_guv_ops;
                g->mgr = NULL; //Doesn't really do anything, but helps with valgrind
                g->need_redraw = 1;
            } else if (!strncmp(cmd.id, "fio", sizeof(cmd.id)) && g->ops.draw_ops.draw_fn != fio_guv_ops.draw_ops.draw_fn) {
                if (g->ops.cleanup_mgr != NULL) g->ops.cleanup_mgr(g);
                g->ops = fio_guv_ops;
                if (g->ops.init_mgr) {
                    int rc = g->ops.init_mgr(g);
                    if (rc < 0) {
                        sprintf(line, "Could not set manager: %s. Resetting...", g->error_str);
                        msg_win_dynamic_append(err_log, line);
                        g->ops = default_guv_ops;
                        g->mgr = NULL;
                    }
                }
                g->need_redraw = 1;
            } else {
                msg_win_dynamic_append(err_log, "Manager unchanged");
            }
            
            break;
        }        
        case CMD_NAME: {
            if (g == NULL) {
                cursor_pos(1, term_rows-1);
                sprintf(line, "This is not a dbg_guv" ERASE_TO_END "%n", &len);
                write(1, line, len);
                return;
            } 
            
            symtab_entry *e = symtab_lookup(ids, cmd.id);
            if (e != NULL) {
                cursor_pos(1, term_rows - 1);
                write_const_str("This ID is already in use" ERASE_TO_END);
                break;
            }
            
            //Check if this dbg_guv already has a name
            e = symtab_lookup(ids, g->name);
            if (e) {
                //Remove old name
                int rc = symtab_array_remove(ids, e);
                if (rc < 0) {
                    sprintf(line, "Could not remove old name: %s", ids->error_str);
                    msg_win_dynamic_append(err_log, line);
                }
            } else if (ids->error_str != SYMTAB_NOT_FOUND) {
                sprintf(line, "Error in symbol table: %s", ids->error_str);
                msg_win_dynamic_append(err_log, line);
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
            twm_tree_add_window(t, d, dummy_ops);
            return;
        }
        case CMD_QUIT: {
            //end libevent event loop
            event_base_loopbreak(ev_base);
            break;
        }
        case CMD_DBG_REG: {
            if (g == NULL) {
                cursor_pos(1, term_rows-1);
                sprintf(line, "This is not a dbg_guv" ERASE_TO_END "%n", &len);
                write(1, line, len);
                return;
            } 
            
            cursor_pos(1, term_rows-1);
            if (cmd.reg == LATCH) {
                sprintf(line, "Committing values to %s" ERASE_TO_END "%n", g->name, &len);
            } else {
                sprintf(line, "Writing 0x%08x (%u) to %s::%s" ERASE_TO_END "%n", 
                    cmd.param, 
                    cmd.param,
                    g->name,
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
			int rc = dbg_guv_send_cmd(g, cmd.reg, cmd.param);
			if (rc < 0) {
				sprintf(line, "Could not enqueue command: %s", g->parent->error_str);
				msg_win_dynamic_append(err_log, line);
			}
            break;
        }
        case CMD_HANDLED: {
			//Nothing to do
			return;
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
    
    twm_tree_add_window(t, err_log, msg_win_draw_ops);
    
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
    
    
    //Close any open FPGA connections. Technically we don't have to do this,
    //since Linux will do it anwyay. 
    fci_list *f = fci_head.next;
    while (f != &fci_head) {
        fci_list *next = f->next;
        del_fpga_connection(f->f);
        free(f);
        f = next;
    }
    
    //Make sure we don't confuse valgrind
    libevent_global_shutdown();
    
    //Just keep clearing things up. Unnecessary, but it separates the chaff
    //from the grain when I look at valgrind
    del_twm_tree(t);
    del_symtab(ids);
    
    //Again, technically unnecessary
    free_msg_win_logs(err_log);
    del_msg_win(err_log);
    
    //Return the terminal to its original state    
    clean_screen();
    return 0;
}

//Given node and serv, tries to resolve the address and to connect a
//non-blocking socket. Returns the file decsriptor on success, or -1 on
//error. In this case, if the given error_str pointer is non-NULL, stores
//a printable error string to explain the cause of failure
int get_nb_sock(char const *node, char const *serv, char const* *error_str) {
    int sfd = -1;
    
    //Try to resolve address
    struct addrinfo *res = NULL;
    struct addrinfo hint = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM, //TODO: support UDP?
        .ai_protocol = 0 //Not sure if this protocol field needs to be set
    };
    
    int rc = getaddrinfo(node, serv, &hint, &res);
    if (rc < 0) {
        if (error_str) *error_str = gai_strerror(rc);
        return -1;
    }
    
    //Open the socket file descriptor in non-blocking mode
    sfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sfd < 0) {
        if (error_str) *error_str = strerror(errno);
        freeaddrinfo(res);
        return -1;
    }
    
    //Connect the socket
    rc = connect(sfd, res->ai_addr, res->ai_addrlen);
    if (rc < 0 && errno != EINPROGRESS) {
        if (error_str) *error_str = strerror(errno);
        close(sfd);
        freeaddrinfo(res);
        return -1;
    }
    
    freeaddrinfo(res);
    
    return sfd;
}

void cleanup_fpga_connection(fpga_connection_info *f) {
    event_del(f->rd_ev);
    event_del(f->wr_ev);
    
    int i;
    for (i = 0; i < MAX_GUVS_PER_FPGA; i++) {
        //Whatever, don't bother error-checking
        dbg_guv *g = f->guvs + i;
        //Release ID
        symtab_entry *e = symtab_lookup(ids, g->name);
        if (e) symtab_array_remove(ids, e);
        //Close windows
        twm_tree_remove_item(t, g);
    }
    
    //Free this FPGA's ID
    if (f->name) {
        symtab_entry *e = symtab_lookup(ids, f->name);
        if (e) symtab_array_remove(ids, e);
    }
    
    //Remove from global list of opened connections
    fci_list *cur = fci_head.next;
    while (cur != &fci_head) {
        if (cur->f == f) {
            cur->prev->next = cur->next;
            cur->next->prev = cur->prev;
            free(cur);
            break;
        } else {
            cur = cur->next;
        }
    }
    
    del_fpga_connection(f);
}
