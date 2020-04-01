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

void get_net_cleanup(void *v) {
#ifdef DEBUG_ON
    fprintf(stderr, "Entered get_net_cleanup\n");
#endif
    queue *q = (queue*)v;
    
    pthread_mutex_lock(&q->mutex);
    q->num_producers--;
    pthread_mutex_unlock(&q->mutex);
    
    pthread_cond_broadcast(&q->can_cons);
}

void get_net_addrinfo_cleanup(void *v) {
#ifdef DEBUG_ON
    fprintf(stderr, "Entered get_net_addrinfo_cleanup\n");
#endif
    struct addrinfo **res = (struct addrinfo **)v;
    if (*res != NULL) freeaddrinfo(*res);
}

void get_net_socket_cleanup(void *v) {
#ifdef DEBUG_ON
    fprintf(stderr, "Entered get_net_socket_cleanup\n");
#endif
    int *p = (int*)v;
    close(*p);
}


typedef struct {
    char *node;
    char *serv;
    
    int sfd;
    pthread_t tx_thread;
    
    queue *ingress;
    queue *egress;
} get_net_args;

void get_net_tx_cleanup(void *v) {
#ifdef DEBUG_ON
    fprintf(stderr, "Entered get_net_tx_cleanup\n");
#endif
    get_net_args *args = (get_net_args *)v;
    
    //Forcibly disconnect all producers and consumers of egress queue
    pthread_mutex_lock(&args->egress->mutex);
    args->egress->num_producers = -1;
    args->egress->num_consumers = -1;
    pthread_mutex_unlock(&args->egress->mutex);
    
    pthread_join(args->tx_thread, NULL);
    
#ifdef DEBUG_ON
    fprintf(stderr, "net_tx joined\n");
#endif
}

void* net_tx(void *v) {
#ifdef DEBUG_ON
    fprintf(stderr, "Entered net_tx\n");
#endif
    get_net_args *args = (get_net_args *)v;
    queue *q = args->egress;
    
    char cmd[4];
    while (dequeue_n(q, cmd, 4) == 0) {
        int len = write(args->sfd, cmd, 4);
        if (len <= 0) break;
    }
    
    
    pthread_exit(NULL);
}

void* get_net(void *v) {
#ifdef DEBUG_ON
    fprintf(stderr, "Entered net_mgr\n");
#endif
    get_net_args *args = (get_net_args *)v;
    queue *q = args->ingress;
    
    char line[80];
    int len;
    
    pthread_cleanup_push(get_net_cleanup, q);
    
    struct addrinfo *res;
    struct addrinfo hint = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = 0 //Not sure if this protocol field needs to be set
    };
    
    int rc = getaddrinfo(args->node, args->serv, &hint, &res);
    if (rc < 0) {
		cursor_pos(1, term_rows-1);
        sprintf(line, "Could not resolve [%s]: %s" ERASE_TO_END "%n", args->node, gai_strerror(rc), &len);
        write(STDOUT_FILENO, line, len);
        goto err_close_tx_thread;
    }
    pthread_cleanup_push(get_net_addrinfo_cleanup, &res);
    
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
		cursor_pos(1, term_rows-1);
        sprintf(line, "Could not open socket: %s" ERASE_TO_END "%n", strerror(errno), &len);
        write(STDOUT_FILENO, line, len);
        goto err_free_addrinfo;
    }
    
    args->sfd = sfd;
    
    pthread_cleanup_push(get_net_socket_cleanup, &sfd);
    
    rc = connect(sfd, res->ai_addr, res->ai_addrlen);
    if (rc < 0) {
		cursor_pos(1, term_rows-1);
        sprintf(line, "Could not connect socket: %s" ERASE_TO_END "%n", strerror(errno), &len);
        write(STDOUT_FILENO, line, len);
        goto err_close_socket;
    }
    
    //This is a pretty long-running thread, so free the memory ASAP
    freeaddrinfo(res);
    //Don't forget to mark as NULL so cleanup function can deal with it
    res = NULL;
    
    //Spin up egress thread
    pthread_create(&args->tx_thread, NULL, net_tx, args);
    pthread_cleanup_push(get_net_tx_cleanup, args);
    
    char buf[64];
    int len;
    while ((len = read(sfd, buf, 64)) > 0) {
        rc = queue_write(q, buf, len);
        if (rc < 0) {
            break;
        }
    }
    
    pthread_cleanup_pop(1);
err_close_socket:
    pthread_cleanup_pop(1);
err_free_addrinfo:
    pthread_cleanup_pop(1);
err_close_tx_thread:
    pthread_cleanup_pop(1);
    return NULL;
}

fpga_connection_info *f = NULL;

queue *net_egress = NULL;

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
		}
		
        add_history(str);
        dbg_guv *g = f->guvs[cmd.dbg_guv_addr];
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
			g->need_redraw = 1;
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
    }
    
    free(str);
}

enum {
    NORMAL,
    READLINE
};

int mode = NORMAL;

int main(int argc, char **argv) {    
    atexit(clean_screen);
    term_init();
    
    init_readline(got_rl_line);
    
    queue q = QUEUE_INITIALIZER;
    q.num_producers++;
    q.num_consumers++;    
    
    pthread_t prod;
    
    pthread_create(&prod, NULL, producer, &q);
    
    queue net_rx = QUEUE_INITIALIZER;
    queue net_tx = QUEUE_INITIALIZER;
    net_rx.num_producers++;
    net_rx.num_consumers++;
    net_tx.num_producers++;
    net_tx.num_consumers++;
    pthread_t net_mgr;
    
    net_egress = &net_tx;
    
    get_net_args args = {
        .node = (argc > 1) ? argv[1] : "localhost",
        .serv = (argc > 2) ? argv[2] : "5555",
        .ingress = &net_rx,
        .egress = &net_tx
    };
    
    pthread_create(&net_mgr, NULL, get_net, &args);
    
    f = new_fpga_connection(NULL, NULL);
    
    dbg_guv *g = f->guvs[0];
    g->x = 1;
    g->y = 7;
    g->w = 30;
    g->h = 7;
    g->parent = f;
    g->addr = 0;
    dbg_guv_set_name(g, "FIZZCNT");
    
    dbg_guv *h = f->guvs[1];
    h->x = 32;
    h->y = 7;
    h->w = 30;
    h->h = 7;
    h->parent = f;
    h->addr = 1;
    dbg_guv_set_name(h, "FIZZBUZZ");
        
    char line[1024];
    int len = 0;
    
    textio_input in;
    
    int status_drawn = 0;
    //Draw "status bar"
    cursor_pos(1, term_rows);
    sprintf(line, "Soyez la bienvenue à la timonerie" ERASE_TO_END "%n", &len);
    write(1, line, len);
    status_drawn = 1;
    
    while(1) {
        int rc;
        char c;
        unsigned msg[2];        
        
        //Get network data
        while(nb_dequeue_n(&net_rx, (char*) msg, sizeof(msg)) == 0) {
            unsigned addr = msg[0];
            char *log_msg = malloc(32);
            sprintf(log_msg, "0x%08x (%u)", msg[1], msg[1]);
            
            char *old = append_log(f, addr, log_msg);
            if (addr == 0) g->need_redraw = 1;
            else if (addr == 1) h->need_redraw = 1;
            if (old != NULL) free(old);
        }   
        
        len = draw_dbg_guv(g, line);
        if (len > 0) {
            write(1, line, len);
            if (mode == READLINE) {
                //Put cursor in the right place
                place_readline_cursor();
            }
        }
        
        len = draw_dbg_guv(h, line);
        if (len > 0) {
            write(1, line, len);
            if (mode == READLINE) {
                //Put cursor in the right place
                place_readline_cursor();
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
            q.num_consumers--;
            pthread_mutex_unlock(&q.mutex);
            
            pthread_mutex_lock(&net_tx.mutex);
            net_tx.num_producers--;
            pthread_mutex_unlock(&net_tx.mutex);
            
            pthread_mutex_lock(&net_rx.mutex);
            net_rx.num_consumers--;
            pthread_mutex_unlock(&net_rx.mutex);
            
            //This is just desperate...
            pthread_cond_broadcast(&q.can_prod);
            pthread_cond_broadcast(&q.can_cons);
            pthread_cond_broadcast(&net_rx.can_prod);
            pthread_cond_broadcast(&net_rx.can_cons);
            pthread_cond_broadcast(&net_tx.can_prod);
            pthread_cond_broadcast(&net_tx.can_cons);
            
            usleep(1000);
            
            //We gave peace a chance, but one of the threads may be blocked in
            //a read call. We have no choice but to cancel them; I should find
            //a way to fix this...
            pthread_cancel(net_mgr);
            pthread_cancel(prod);
            
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
                case TEXTIO_GETCH_MOUSE:
                    //Just for fun: use scrollwheel inside dbg_guv
                    if (in.btn == TEXTIO_WUP) {
                        if (in.x >= g->x && in.x < g->x + g->w && in.y >= g->y && in.y < g->y + g->h) {
                            if (g->buf_offset < SCROLLBACK - g->h - 1) g->buf_offset++;
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
                            if (h->buf_offset < SCROLLBACK - h->h - 1) h->buf_offset++;
                            h->need_redraw = 1;
                        }
                    } else if (in.btn == TEXTIO_WDN) {
                        if (in.x >= h->x && in.x < h->x + h->w && in.y >= h->y && in.y < h->y + h->h) {
                            if (h->buf_offset > 0) h->buf_offset--;
                            h->need_redraw = 1;
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
    
    pthread_join(net_mgr, NULL);
#ifdef DEBUG_ON
    fprintf(stderr, "Joined net_mgr\n");
#endif
    pthread_join(prod, NULL);
#ifdef DEBUG_ON
    fprintf(stderr, "Joined prod\n");
#endif
    
    del_fpga_connection(f);
    clean_screen();
    return 0;
}
