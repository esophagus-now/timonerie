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

void producer_cleanup(void *v) {
    queue *q = (queue*)v;
    
    pthread_mutex_lock(&q->mutex);
    q->num_producers--;
    pthread_mutex_unlock(&q->mutex);
    
    pthread_cond_broadcast(&q->can_cons);
}

void* producer(void *v) {
    queue *q = (queue*)v;
    
    pthread_cleanup_push(producer_cleanup, q);
    
    //Read a single character from stdin until EOF
    char c;
    while (read(0, &c, 1) > 0) {
        enqueue_single(q, c);
    }
    
    pthread_cleanup_pop(1);
    
    return NULL;
}

void get_net_cleanup(void *v) {
    queue *q = (queue*)v;
    
    pthread_mutex_lock(&q->mutex);
    q->num_producers--;
    pthread_mutex_unlock(&q->mutex);
    
    pthread_cond_broadcast(&q->can_cons);
}

void get_net_addrinfo_cleanup(void *v) {
    struct addrinfo **res = (struct addrinfo **)v;
    if (*res != NULL) freeaddrinfo(*res);
}

void get_net_socket_cleanup(void *v) {
    int *p = (int*)v;
    close(*p);
}

typedef struct {
    queue *q;
    char *node;
    char *serv;
} get_net_args;

void* get_net(void *v) {
    get_net_args *args = (get_net_args *)v;
    queue *q = args->q;
    
    pthread_cleanup_push(get_net_cleanup, q);
    
    struct addrinfo *res;
    struct addrinfo hint = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = 0 //Not sure if this protocol field needs to be set
    };
    
    int rc = getaddrinfo(args->node, args->serv, &hint, &res);
    if (rc < 0) {
        fprintf(stderr, "Could not resolve [%s]: %s\n", args->node, gai_strerror(rc));
        goto done;
    }
    pthread_cleanup_push(get_net_addrinfo_cleanup, &res);
    
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
        fprintf(stderr, "Could not open socket: %s\n", strerror(errno));
        goto done;
    }
    
    pthread_cleanup_push(get_net_socket_cleanup, &sfd);
    
    rc = connect(sfd, res->ai_addr, res->ai_addrlen);
    if (rc < 0) {
        fprintf(stderr, "Could not connect socket: %s\n", strerror(errno));
        goto done;
    }
    
    //This is a pretty long-running thread, so free the memory ASAP
    freeaddrinfo(res);
    //Don't forget to mark as NULL so cleanup function can deal with it
    res = NULL;
    
    char buf[64];
    int len;
    while ((len = read(sfd, buf, 64)) > 0) {
        rc = queue_write(q, buf, len);
        if (rc < 0) {
            fprintf(stderr, "Could not append network data into queue");
            goto done;
        }
    }
    
    done:
    close(sfd);
    pthread_cleanup_pop(1);
    pthread_cleanup_pop(1);
    pthread_cleanup_pop(1);
    return NULL;
}

fpga_connection_info *f = NULL;
dbg_guv *g = NULL;

void got_rl_line(char *str) {
    cursor_pos(1,2);
    char line[80];
    int len;
    sprintf(line, "Read line: %s" ERASE_TO_END "%n", str, &len);
    write(1, line, len);
    if (f != NULL) append_log(f, 0, str);
    else free(str);
    if (g != NULL) g->need_redraw = 1;
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
    
    pthread_t prod;
    
    pthread_create(&prod, NULL, producer, &q);
    
    pthread_mutex_lock(&q.mutex);
    q.num_producers++;
    pthread_mutex_unlock(&q.mutex);
    
    queue netq = QUEUE_INITIALIZER;
    pthread_t net_prod;
    
    get_net_args args = {
        .q = &netq,
        .node = (argc > 1) ? argv[1] : "localhost",
        .serv = (argc > 2) ? argv[2] : "5555"
    };
    pthread_create(&net_prod, NULL, get_net, &args);
    pthread_mutex_lock(&q.mutex);
    netq.num_producers++;
    pthread_mutex_unlock(&q.mutex);
    
    #define NET_DATA_MAX 80
    char net_data[NET_DATA_MAX + 1];
    int net_data_pos = 0;
    
    f = new_fpga_connection(NULL, NULL);
    
    g = new_dbg_guv(NULL);
    g->x = 5;
    g->y = 7;
    g->w = 25;
    g->h = 7;
    g->parent = f;
    g->addr = 0;
    
    
        
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
        
        while(nb_dequeue_single(&netq, &c) == 0) {
            if (c != '\n') net_data[net_data_pos++] = c;
            if (c == '\n' || net_data_pos == NET_DATA_MAX - 1) {
                net_data[net_data_pos] = 0;
                char *cpy = strdup(net_data);
                char *old = append_log(f, 0, cpy);
                g->need_redraw = 1;
                if (old != NULL) free(old);
                //cursor_pos(0,5);
                //write(1, net_data, net_data_pos);
                //write(1, ERASE_TO_END, sizeof(ERASE_TO_END));
                net_data_pos = 0;
            }
        }   
        
        len = draw_dbg_guv(g, line);
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
            pthread_cancel(prod);
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
            pthread_cancel(prod);
            pthread_cancel(net_prod);
            break;
        } else {
            if (mode == READLINE) {
                forward_to_readline(c);
                if (c == '\n') mode = NORMAL; //Exit readline mode on ESC?
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
    
    pthread_mutex_lock(&netq.mutex);
    if (netq.num_producers > 0) {
        pthread_cancel(net_prod);
    }
    pthread_mutex_unlock(&netq.mutex);
    
    pthread_join(net_prod, NULL);
    pthread_join(prod, NULL);
    
    del_dbg_guv(g);
    del_fpga_connection(f);
    clean_screen();
    return 0;
}
