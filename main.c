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

typedef struct {
    char *name;
    int x, y; 
    int w, h; //Minimum: 6 by 6?
    
    //TODO: some nice way of storing the data from the network
    
    //Mirror registers in hardware
    unsigned keep_pausing;
    unsigned keep_logging;
    unsigned log_cnt;
    unsigned keep_dropping;
    unsigned drop_cnt;
    unsigned inj_TDATA;
    unsigned inj_TVALID;
    unsigned inj_TKEEP;
    unsigned inj_TLAST;
    unsigned inj_TDEST;
    unsigned inj_TID;
} dbg_guv;

dbg_guv* new_dbg_guv(char *name) {
    dbg_guv *ret = malloc(sizeof(dbg_guv));
    memset(ret, 0, sizeof(dbg_guv));
    
    if (name != NULL) {
        ret->name = strdup(name);
    }
    ret->w = 6;
    ret->h = 6;
    
    return ret;
}

void del_dbg_guv(dbg_guv *d) {
    if (d != NULL) {
        if (d->name != NULL) free(d->name);
        free(d);
    }
}

//Returns number of bytes added into buf.
int redraw_dbg_guv(dbg_guv *g, char *buf) {
    int bytes = 0;
    
    if (g->x < 0 || g->y < 0) { //TODO: screen sizes
        return bytes;
    }
    
    if (g->w < 6) {
        g->w = 6;
    }
    
    if (g->h < 6) {
        g->h = 6;
    }
    
    int inc;
    int i;
    
    //Top bar
    inc = cursor_pos_cmd(buf, g->x, g->y);
    buf += inc;
    //*buf++ = BOX_TL;
    *buf++ = 0b11100010;
    *buf++ = 0b10010100;
    *buf++ = 0b10001100;
    bytes += inc + 3;
    for (i = 1; i < g->w - 1; i++) {
        //*buf++ = BOX_HORZ;
        *buf++ = 0b11100010;
        *buf++ = 0b10010100;
        *buf++ = 0b10000000;
        bytes += 3;
    }
    //*buf++ = BOX_TR;
    *buf++ = 0b11100010;
    *buf++ = 0b10010100;
    *buf++ = 0b10010000;
    bytes+=3;
    
    //Vertical bars
    for (i = 1; i < g->h - 1; i++) {
        inc = cursor_pos_cmd(buf, g->x, g->y + i);
        buf += inc;
        //*buf++ = BOX_VERT;
        *buf++ = 0b11100010;
        *buf++ = 0b10010100;
        *buf++ = 0b10000010;
        bytes += inc + 3;
        
        inc = cursor_pos_cmd(buf, g->x + g->w - 1, g->y + i);
        buf += inc;
        //*buf++ = BOX_VERT;
        *buf++ = 0b11100010;
        *buf++ = 0b10010100;
        *buf++ = 0b10000010;
        bytes += inc + 3;
    }
    
    //Bottom bar
    inc = cursor_pos_cmd(buf, g->x, g->y + g->h - 1);
    buf += inc;
    //*buf++ = BOX_BL;
    *buf++ = 0b11100010;
    *buf++ = 0b10010100;
    *buf++ = 0b10010100;
    bytes += inc + 3;
    for (i = 1; i < g->w - 1; i++) {
        //*buf++ = BOX_HORZ;
        *buf++ = 0b11100010;
        *buf++ = 0b10010100;
        *buf++ = 0b10000000;
        bytes += 3;
    }
    //*buf++ = BOX_BR;
    *buf++ = 0b11100010;
    *buf++ = 0b10010100;
    *buf++ = 0b10011000;
    bytes+=3;
    
    return bytes;
}

int main(int argc, char **argv) {    
    atexit(clean_screen);
    term_init();
    
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
    char net_data[NET_DATA_MAX];
    int net_data_pos = 0;
    
    dbg_guv *g = new_dbg_guv(NULL);
    g->x = 5;
    g->y = 7;
    
    textio_input in;
    
    while(1) {
        int rc;
        char c;
        
        char line[256];
        int len = 0;
        
        len = redraw_dbg_guv(g, line);
        write(1, line, len);
        
        while(nb_dequeue_single(&netq, &c) == 0) {
            net_data[net_data_pos++] = c;
            if (c == '\n' || net_data_pos == NET_DATA_MAX - 1) {
                cursor_pos(0,5);
                write(1, net_data, net_data_pos);
                net_data_pos = 0;
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
        
        //If user pressed ~ (but not as part of escape sequence) we can quit
        if (c == '~') {
            pthread_cancel(prod);
            pthread_cancel(net_prod);
            break;
        } /*else if (c == '/') {
            pthread_mutex_lock(&q.mutex);
            char *l = readline("Enter a line: ");
            pthread_mutex_unlock(&q.mutex);
            cursor_pos(1,4);
            sprintf(line, "You entered: %s" ERASE_TO_END "%n", l, &len);  
            write(1, line, len);  
        } */ else {
            int rc = textio_getch_cr(c, &in);
            if (rc == 0) {
                cursor_pos(1,1);
                switch(in.type) {
                case TEXTIO_GETCH_PLAIN:
                    sprintf(line, "You entered: 0x%02x" ERASE_TO_END "%n", in.c & 0xFF, &len);
                    break;
                case TEXTIO_GETCH_UNICODE:
                    sprintf(line, "You entered: %s" ERASE_TO_END "%n", in.wc, &len);
                    break;
                case TEXTIO_GETCH_FN_KEY:
                    sprintf(line, "You entered: %s" ERASE_TO_END "%n", FN_KEY_NAMES[in.key], &len);
                    break;
                case TEXTIO_GETCH_ESCSEQ:
                    sprintf(line, "You entered some kind of escape sequence ending in %c" ERASE_TO_END "%n", in.code, &len);
                    break;
                case TEXTIO_GETCH_MOUSE:
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
                cursor_pos(1,5);
                write(1, line, len);
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
    clean_screen();
    return 0;
}
