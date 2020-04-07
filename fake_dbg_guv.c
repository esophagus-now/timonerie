//Need this #define to use pthread_setname_np
#define _GNU_SOURCE 

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <errno.h>

//This is a quick and dirty dbg_guv simulator for use between Linux 
//processes. I am forcing myself to not make it super good because I don't
//want to spend too much time

#define addr_drop_cnt      0 
#define addr_log_cnt       1 
#define addr_inj_TDATA     2 
#define addr_inj_TVALID    3 
#define addr_inj_TLAST     4 
#define addr_inj_TKEEP     5 
#define addr_inj_TDEST     6 
#define addr_inj_TID       7 
#define addr_keep_pausing  8 
#define addr_keep_logging  9 
#define addr_keep_dropping 10
#define addr_dut_reset     11

typedef struct _fake_dbg_guv {    
    //S1 registers
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
    unsigned dut_reset;
    
    //S2 registers
    unsigned keep_pausing_r;
    unsigned keep_logging_r;
    unsigned log_cnt_r;
    unsigned keep_dropping_r;
    unsigned drop_cnt_r;
    unsigned inj_TDATA_r;
    unsigned inj_TVALID_r;
    unsigned inj_TKEEP_r;
    unsigned inj_TLAST_r;
    unsigned inj_TDEST_r;
    unsigned inj_TID_r;
    unsigned dut_reset_r;
    
    //emulate dbg_guv log behaviour
    unsigned in_log_packet;
    unsigned log_data;
    unsigned is_receipt;
} fake_dbg_guv;

//from https://stackoverflow.com/questions/12340695/how-to-check-if-a-given-file-descriptor-stored-in-a-variable-is-still-valid
int fd_is_valid(int fd) {
    return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

volatile int stop = 0;
void sigpipe_handler(int s) {
    stop = 1;
}

char const *const SUCC = "succ";
char const *const WRITE_ERROR = "write error";
char const *const STOP_SIG = "stop was signaled";

//Once a value is written into to_send, it is considered sent (even if it is
//hasn't actually been sent out to the write system call)
typedef struct {
    int fd;
    unsigned to_send;
    unsigned vld;
    unsigned val_sent; //Only used by log_out
    unsigned stop;
    char const *error_str;
    pthread_mutex_t mutex;
} egress_args;

void *word_writer (void *arg) {
    egress_args *args = (egress_args*)arg;
    
    while (1) {
        unsigned local_copy;
        int fd;
        
        pthread_mutex_lock(&args->mutex);
        //Save the value that the user is trying to send
        local_copy = args->to_send;
        fd = args->fd;
        
        //Check if we should stop
        if (args->stop) {
            args->error_str = STOP_SIG;
            args->stop = 1;
            pthread_mutex_unlock(&args->mutex);
            break;
        }
        
        //Is there anything to send?
        if (!args->vld) {
            //Nothing to send
            pthread_mutex_unlock(&args->mutex);
            sched_yield();
            continue;
        }
        pthread_mutex_unlock(&args->mutex);
        
        //At this point, there is something to send. Issue the write() call
        int num_written = 0;
        int rc;
        //For extra safety, write this write in a loop. Probably unnecessary
        //since we're no longer living in the 1960s
        while (num_written < sizeof(local_copy)) {
            rc = write(fd, ((char*)(&local_copy)) + num_written, sizeof(local_copy) - num_written);
            if (rc <= 0) break;
        }
        
        //Check for write error and set values
        if (rc < 0) {
            char *err = strerror(errno);
            pthread_mutex_lock(&args->mutex);
            args->error_str = err;
            args->stop = 1;
            pthread_mutex_unlock(&args->mutex);
            break;
        } else {
            pthread_mutex_lock(&args->mutex);
            args->error_str = SUCC;
            args->vld = 0;
            args->val_sent = 1;
            pthread_mutex_unlock(&args->mutex);
        }
    }
    
    pthread_exit(NULL);
}

int main() {
    //Try to make sure that the right file redirects were used
    if (!fd_is_valid(3) || !fd_is_valid(4)) {
        fprintf(stderr, "Please redirect pipe output to file 3 and file 4 to pipe input\n");
        fflush(stderr);
        return -1;
    }
    
    fake_dbg_guv d;
    memset(&d, 0, sizeof(d));
    
    //Hook up SIGPIPE handler to stop this program
    struct sigaction sa = {
        .sa_handler = sigpipe_handler,
        .sa_flags = 0
    };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPIPE, &sa, NULL);
    
    pthread_t out_tid, log_tid;
    egress_args out_args = {
        .fd = STDOUT_FILENO,
        .vld = 0,
        .stop = 0,
        .error_str = SUCC,
        .mutex = PTHREAD_MUTEX_INITIALIZER
    };
    egress_args log_args = {
        .fd = 4,
        .vld = 0,
        .val_sent = 0,
        .stop = 0,
        .error_str = SUCC,
        .mutex = PTHREAD_MUTEX_INITIALIZER
    };
    
    pthread_create(&out_tid, NULL, word_writer, &out_args);
    pthread_setname_np(out_tid, "dbg_guv out");
    pthread_create(&log_tid, NULL, word_writer, &log_args);
    pthread_setname_np(log_tid, "dbg_guv log");
    
    //When we get a latch signal, we need to save the receipt hdr and data
    //in case the log is not currently ready
    unsigned receipt_hdr;
    unsigned receipt_data;
    unsigned receipt_vld = 0;
    
    while (!stop) {
        int output_ready = 0;
        int log_ready = 0;
        
        //Check up on output thread
        pthread_mutex_lock(&out_args.mutex);
        if (out_args.stop) {
            fprintf(stderr, "Output thread stopped early: %s\n", out_args.error_str);
            pthread_mutex_unlock(&out_args.mutex);
            break;
        }
        if (!out_args.vld) {
            //See if there is an inject available to write
            if (d.inj_TVALID) {
                out_args.to_send = d.inj_TDATA;
                out_args.vld = 1;
                d.inj_TVALID = 0;
            } else {
                //Later we'll check if anyone is trying to write to the 
                //output, depending on pause/log/drop flags
                output_ready = 1;
            }
        }
        pthread_mutex_unlock(&out_args.mutex);
        
        //Check up on log thread
        pthread_mutex_lock(&log_args.mutex);
        if (log_args.stop) {
            fprintf(stderr, "Log thread stopped early: %s\n", log_args.error_str);
            pthread_mutex_unlock(&log_args.mutex);
            break;
        }
        if (log_args.val_sent) {
            //If a log was sent, decrement log_cnt
            if (!d.is_receipt && !d.in_log_packet) {
                if (d.log_cnt > 0) d.log_cnt--;
            }
            log_args.val_sent = 0;
        }
        if (!log_args.vld) {
            //Check if there is a log in progress; if so, queue up the next
            //flit
            if (d.in_log_packet) {
                log_args.to_send = d.log_data;
                log_args.vld = 1;
                log_args.val_sent = 0; //Unnecessary, but added for clarity (it's set by the if on line 220)
                d.in_log_packet = 0;
            } 
            //Check if there is a command receipt waiting
            else if (receipt_vld) {
                //Send receipt header to log output
                log_args.to_send = receipt_hdr;
                log_args.vld = 1;
                log_args.val_sent = 0;
                
                //Queue up second flit, and mark it as command receipt
                d.log_data = receipt_data;
                d.in_log_packet = 1;
                d.is_receipt = 1;
                
                //We can mark our "receipt registers" as free
                receipt_vld = 0;
            } else {
                //Later we'll check if anyone is trying to write to the 
                //log, depending on pause/log/drop flags
                log_ready = 1;
            }
        }
        pthread_mutex_unlock(&log_args.mutex);
        
        //See if there is any input ready
        int input_valid = 0;
        
        struct pollfd can_read_input = {
            .fd = STDIN_FILENO,
            .events = POLLIN | POLLHUP
        };
        
        int rc = poll(&can_read_input, 1, 0);
        if (rc < 0) {
            perror("Could not issue poll call");
            break;
        } else if (rc == 0) {
            //Timeout
            input_valid = 0;
        } else {
            if (can_read_input.revents & POLLHUP) {
                fprintf(stderr, "Input pipe disconnected\n");
                break;
            } else if (can_read_input.revents & POLLIN) {
                input_valid = 1;
            }
        }
        
        //Look at pause/drop/log/flags to figure out if we should read from
        //the input
        
        
        
        //Read a command input
        
        //If latch command, overwrite receipt registers
    }
    
    //Try to properly close out the threads
    int kill_out = 0, kill_log = 0;
    pthread_mutex_lock(&out_args.mutex);
    if (out_args.stop == 0) {
        out_args.stop = 1;
        kill_out = 1;
    }
    pthread_mutex_unlock(&out_args.mutex);
    
    pthread_mutex_lock(&log_args.mutex);
    if (log_args.stop == 0) {
        log_args.stop = 1;
        kill_log = 1;
    }
    pthread_mutex_unlock(&log_args.mutex);
    
    //Interrupt any running system calls
    if (kill_out) pthread_kill(out_tid, SIGCONT);
    if (kill_log) pthread_kill(log_tid, SIGCONT);
    
    pthread_join(out_tid, NULL);
    pthread_join(log_tid, NULL);
    
    return 0;
}
