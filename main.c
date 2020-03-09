#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <termios.h>
#include <string.h>
#include <unistd.h>

#define ESC "\x1b"
#define CSI "\x1b["

#define ALT_BUF_ON "\x1b[?1049h"
#define LEN_ALT_BUF_ON 8

#define ALT_BUF_OFF "\x1b[?1049l"
#define LEN_ALT_BUF_OFF 8

#define ERASE_ALL "\x1b[2J"
#define LEN_ERASE_ALL 4

#define REPORT_CURSOR_ON "\x1b[?1003h"
#define LEN_REPORT_CURSOR_ON 8

#define REPORT_CURSOR_OFF "\x1b[?1003l"
#define LEN_REPORT_CURSOR_OFF 8

int changed = 0;
struct termios old;

void cursor_pos(int x, int y) {
    char line[80];
    int len;
    sprintf(line, CSI "%d;%dH%n", y, x, &len);
    write(1, line, len);
}

void term_init() {
    //Get current TTY attributes and save in old
    tcgetattr(0, &old);
    //Keep track of the fact that we are changing the tty settings
    changed = 1;
    //Copy the old settings and modify to turn off "cooked" mode and echoing
    struct termios mod = old;
    mod.c_lflag &= (~ECHO & ~ICANON);
    tcsetattr(0, TCSANOW, &mod);
    
    //Use the standard ANSI escape sequences to switch to the terminal's 
    //alternate buffer (so that we don't disturb the regular text output
    write(1, ALT_BUF_ON, LEN_ALT_BUF_ON);
    //Clear the screen
    write(1, ERASE_ALL, LEN_ERASE_ALL);
    //Turn on mouse reporting
    write(1, REPORT_CURSOR_ON, LEN_REPORT_CURSOR_ON);
    
    cursor_pos(0,0);
}

void clean_screen() {
    if (changed) {
        write(1, ALT_BUF_OFF, LEN_ALT_BUF_OFF);
        write(1, REPORT_CURSOR_OFF, LEN_REPORT_CURSOR_OFF);
        tcsetattr(0, TCSANOW, &old);
        changed = 0;
    }
}

#define BUF_SIZE 64
typedef struct {
    char buf[BUF_SIZE];
    int wr_pos, rd_pos;
    int empty;
    int full;
    pthread_mutex_t mutex;
    pthread_cond_t can_prod;
    pthread_cond_t can_cons;
    int num_producers;
} queue;

//Reads a char from queue q in a thread-safe way. Returns 0 on successful read,
//negative on error
int dequeue(queue *q, char *c) {
    pthread_mutex_lock(&q->mutex);
    while (q->empty && q->num_producers > 0) pthread_cond_wait(&q->can_cons, &q->mutex);
    if (q->num_producers <= 0) {
        pthread_mutex_unlock(&q->mutex);
        return -1;
    }
    
    *c = q->buf[q->rd_pos++];
    if (q->rd_pos >= BUF_SIZE) q->rd_pos = 0;
    if (q->rd_pos == q->wr_pos) q->empty = 1;
    q->full = 0;
    pthread_mutex_unlock(&q->mutex);
    
    pthread_cond_signal(&q->can_prod);
    return 0;
}

#define QUEUE_INITIALIZER {\
    .wr_pos = 0,\
    .rd_pos = 0,\
    .empty = 1,\
    .full = 0,\
    .mutex = PTHREAD_MUTEX_INITIALIZER,\
    .can_prod = PTHREAD_COND_INITIALIZER,\
    .can_cons = PTHREAD_COND_INITIALIZER,\
    .num_producers = 0\
}

//Returns 0 if x and y are valid, -1 otherwise
//Pass NULL for either int to ignore that parameter
int parse_mouse(queue *q, char *btn_info, int *x, int *y) {
    int pos = 0;
    
    char c;
    int rc;
    
    rc = dequeue(q, &c);
    if (rc < 0) {
        sprintf(btn_info, "Dequeue error");
        return rc;
    } else if (c != '[') {
        sprintf(btn_info, "Received %02x instead of \'[\'(%02x)", c, '[');
        return -1;
    }
    
    rc = dequeue(q, &c);
    if (rc < 0) {
        sprintf(btn_info, "Dequeue error");
        return rc;
    } else if (c != 'M') {
        sprintf(btn_info, "Received %02x instead of \'M\'(%02x)", c, 'M');
        return -1;
    }
    
    rc = dequeue(q, &c);
    if (rc < 0) {
        sprintf(btn_info, "Dequeue error");
        return rc;
    }
    //C contains the button code
    if (c < 32) {
        sprintf(btn_info, "Invalid button code (<32)");
        return -1;
    }
    int tmp;
    if (c >= 64 && c < 96) {
        sprintf(btn_info + pos, "(motion)%n", &tmp);
        pos += tmp;
    }
    
    if (c & 0b100) {
        sprintf(btn_info + pos, "(shift)%n", &tmp);
        pos += tmp;
    }
    
    if (c & 0b1000) {
        sprintf(btn_info + pos, "(meta)%n", &tmp);
        pos += tmp;
    }
    
    if (c & 0b10000) {
        sprintf(btn_info + pos, "(ctrl)%n", &tmp);
        pos += tmp;
    }
    
    if (c < 92) {
        switch(c&11) {
        case 0b00:
            sprintf(btn_info + pos, "LMB%n", &tmp);
            pos += tmp;
            break;
        case 0b01:
            sprintf(btn_info + pos, "MMB%n", &tmp);
            pos += tmp;
            break;
        case 0b10:
            sprintf(btn_info + pos, "RMB%n", &tmp);
            pos += tmp;
            break;
        case 0b11:
            sprintf(btn_info + pos, "noB%n", &tmp);
            pos += tmp;
            break;
        }
    } else {
        switch (c & 0b01100001) {
        case 0b1100000:
            sprintf(btn_info + pos, "WUP%n", &tmp);
            pos += tmp;
            break;
        case 0b1100001:
            sprintf(btn_info + pos, "WDN%n", &tmp);
            pos += tmp;
            break;
        }
    }
    
    //Read x and y    
    rc = dequeue(q, &c);
    if (rc < 0) {
        sprintf(btn_info, "Dequeue error");
        return rc;
    } else if (c < 32) {
        sprintf(btn_info, "Invalid X code (<32)");
        return -1;
    }
    if (x != NULL) *x = c - 32;
    
    rc = dequeue(q, &c);
    if (rc < 0) {
        sprintf(btn_info, "Dequeue error");
        return rc;
    } else if (c < 32) {
        sprintf(btn_info, "Invalid Y code (<32)");
        return -1;
    }
    if (y != NULL) *y = c - 32;
    
    return 0;
}

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
    pthread_mutex_lock(&q->mutex);
    q->num_producers++;
    pthread_mutex_unlock(&q->mutex);
    
    pthread_cleanup_push(producer_cleanup, q);
    
    //Read a single character from stdin until EOF
    while (read(0, &c, 1) > 0) {
        //Lock mutex before we try adding c to the queue
        pthread_mutex_lock(&q->mutex);
        //Wait until there is space
        while(q->full) {
            pthread_cond_wait(&q->can_prod, &q->mutex);
        }
        
        //Add c to the buffer, making sure to signal to everyone else that they
        //can read
        q->buf[q->wr_pos++] = c;
        if (q->wr_pos >= BUF_SIZE) q->wr_pos = 0;
        if (q->wr_pos == q->rd_pos) q->full = 1;
        q->empty = 0;
        
        pthread_mutex_unlock(&q->mutex);
        
        pthread_cond_signal(&q->can_cons);
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
    do {
        while(q.empty) pthread_cond_wait(&q.can_cons, &q.mutex);
        
        //A little slow but we'll read one character at a time, guarding each
        //one with the mutexes. For more speed, we should read them out in a 
        //loop
        char c = q.buf[q.rd_pos++];
        if (q.rd_pos >= BUF_SIZE) q.rd_pos = 0;
        if (q.rd_pos == q.wr_pos) q.empty = 1;
        q.full = 0;
        pthread_mutex_unlock(&q.mutex);
        
        pthread_cond_signal(&q.can_prod);
        
        char line[80];
        int len;
        
        //If user pressed ~ we cam quit
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
                sprintf(line, "%s at %d,%d                            %n", btn, x, y, &len);
                write(1, line, len);
            }
        }
        
        cursor_pos(0,0);
        sprintf(line, "You pressed 0x%02x%n", c & 0xFF, &len);
        write(1, line, len);
        fflush(stdout);
        
        pthread_mutex_lock(&q.mutex);
    } while(q.num_producers > 0 || !q.empty) ;
    
    pthread_mutex_unlock(&q.mutex);
    
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
