#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
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
    char c;
    
    queue *q = (queue*)v;
    
    pthread_cleanup_push(producer_cleanup, q);
    
    //Read a single character from stdin until EOF
    while (read(0, &c, 1) > 0) {
        enqueue_single(q, c);
    }
    
    pthread_cleanup_pop(1);
    
    return NULL;
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
    
    while(1) {
        char line[80];
        int len = 0;
        
        //A little slow but we'll read one character at a time, guarding each
        //one with the mutexes. For more speed, we should read them out in a 
        //loop
        char c;
        int rc = dequeue_single(&q, &c);
        if (rc < 0) {
            cursor_pos(0,0);
            sprintf(line, "Error reading from queue. Quitting...%n", &len);
            write(1, line, len);
            fflush(stdout); //Is this necessary?
            pthread_cancel(prod);
            break;
        }
        
        
        //If user pressed ~ we can quit
        if (c == '~') {
            pthread_cancel(prod);
            break;
        } else if (c == '\x1b') {
            char btn[32];
            int x, y;
            int rc = parse_mouse(&q, btn, &x, &y);
            cursor_pos(0,2);
            if (rc < 0) {
                sprintf(line, "Could not parse mouse: %s%n", btn, &len);
                write(1, line, len);
            } else {
                sprintf(line, "%s at %d,%d                                         %n", btn, x, y, &len);
                write(1, line, len);
            }
        } else {
            cursor_pos(0,0);
            int tmp;
            sprintf(line, "You pressed 0x%02x%n", c & 0xFF, &tmp);
            len += tmp;
            if (isprint(c)) {
                sprintf(line + len, " = '%c'%n", c, &tmp);
                len += tmp;
            } else {
                sprintf(line + len, "       %n", &tmp);
                len += tmp;
            }
            write(1, line, len);
        }
    }
    
    pthread_join(prod, NULL);
    
    /*if (argc > 1) {
        //Use argv[1] for hostname lookup
        struct addrinfo *res;
        char *serv = NULL;
        if (argc > 2) serv = argv[2];
        
        struct addrinfo hint = {
            .ai_family = AF_INET,
            .ai_socktype = SOCK_STREAM,
            .ai_protocol = 0
        };
        
        int rc = getaddrinfo(argv[1], serv, &hint, &res);
        if (rc < 0) {
            printf("Could not resolve [%s]: %s\n", argv[1], gai_strerror(rc));
        } else {
            struct addrinfo *cur;
            for (cur = res; cur; cur = cur->ai_next) {
                printf("addrinfo:\n");
                printf("\tai_flags = 0x%08x\n", cur->ai_flags);
                printf("\tai_family = 0x%08x\n", cur->ai_family);
                printf("\tai_socktype = 0x%08x\n", cur->ai_socktype);
                printf("\tai_protocol = 0x%08x\n", cur->ai_protocol);
                printf("\tai_addrlen = %d\n", cur->ai_addrlen);
                printf("\tai_addr = \n");
                printf("\t\tsa_family = %04x\n", cur->ai_addr->sa_family);
                printf("\t\tsa_data = 0x ");
                int i;
                for (i = 0; i < cur->ai_addrlen - sizeof(cur->ai_addr->sa_family); i++) {
                    printf("%02x ", cur->ai_addr->sa_data[i] & 0xFF);
                }
                printf("\n\tai_canonname = %s\n", cur->ai_canonname);
            }
            freeaddrinfo(res);
        }
    }*/
    
    clean_screen();
    return 0;
}
