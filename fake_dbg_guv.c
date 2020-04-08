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
#define addr_latch         15

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
    
    //I told myself I would be okay with ugly hacks, so here is one:
    unsigned extra_val;
    unsigned extra_vld;
    
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
        
        
        //I told myself I would be okay with ugly hacks, so here is one:
        unsigned extra_local_copy;
        unsigned extra_vld_copy;
        
        pthread_mutex_lock(&args->mutex);
        //Save the value(s) that the user is trying to send
        local_copy = args->to_send;
        fd = args->fd;
        
        //I told myself I would be okay with ugly hacks, so here is one:
        extra_local_copy = args->extra_val;
        extra_vld_copy = args->extra_vld;
        
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
        int saved_errno;
        int rc;
        //For extra safety, write this write in a loop. Probably unnecessary
        //since we're no longer living in the 1960s
        while (num_written < sizeof(local_copy)) {
            rc = write(fd, ((char*)(&local_copy)) + num_written, sizeof(local_copy) - num_written);
            saved_errno = errno;
            if (rc <= 0) break;
            num_written += rc;
        }
        
        int extra_rc;
        int extra_saved_errno;
        //I told myself I would be okay with ugly hacks, so here is one:
        if (extra_vld_copy) {
            int extra_num_written = 0;
            //For extra safety, write this write in a loop. Probably unnecessary
            //since we're no longer living in the 1960s
            while (extra_num_written < sizeof(local_copy)) {
                extra_rc = write(fd, ((char*)(&extra_local_copy)) + extra_num_written, sizeof(extra_local_copy) - extra_num_written);
                extra_saved_errno = errno;
                if (extra_rc <= 0) break;
            }
        }
        
        //Check for write error and set values
        if (rc < 0) {
            char const *err = strerror(saved_errno);
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
        
        //I told myself I would be okay with ugly hacks, so here is one:
        if (extra_rc < 0) {
            char const *err = strerror(extra_saved_errno);
            pthread_mutex_lock(&args->mutex);
            args->error_str = err;
            args->stop = 1;
            pthread_mutex_unlock(&args->mutex);
            break;
        } else {
            pthread_mutex_lock(&args->mutex);
            args->error_str = SUCC;
            args->extra_vld = 0;
            pthread_mutex_unlock(&args->mutex);
        }
    }
    
    pthread_exit(NULL);
}



int main(int argc, char **argv) {
    //Check command line args
    if (argc != 4 && argc != 5) {
        fprintf(stderr, "Usage: fake_dbg_guv GUV_ADDR CMD_IN_PIPE LOG_OUT_PIPE [CMD_OUT_PIPE]\n");
        return -1;
    }
    
    //Parse guv address
    unsigned guv_addr = 0;
    int rc = sscanf(argv[1], "%u", &guv_addr);
    if (rc < 1) {
        fprintf(stderr, "Could not parse dbg_guv address [%s]\n", argv[1]);
        return -1;
    } else if (guv_addr > 15) {
        fprintf(stderr, "dbg_guv address [%u] is out of range\n", guv_addr);
        return -1;
    }
    
    //Try opening the named pipes
    int cmd_in_fd = open(argv[2], O_RDONLY);
    if (cmd_in_fd < 0) {
        char line[80];
        sprintf(line, "Could not open command input pipe [%.32s]", argv[2]);
        perror(line);
        return -1;
    }
    
    int log_out_fd = open(argv[3], O_WRONLY);
    if (log_out_fd < 0) {
        char line[80];
        sprintf(line, "Could not open log output pipe [%.32s]", argv[3]);
        perror(line);
        close(cmd_in_fd);
        return -1;
    }
    
    int cmd_out_fd = -1;
    if (argc == 5) {
        cmd_out_fd = open(argv[4], O_WRONLY);
        if (log_out_fd < 0) {
            char line[80];
            sprintf(line, "Could not open command output pipe [%32s]", argv[4]);
            perror(line);
            close(cmd_in_fd);
            close(log_out_fd);
            return -1;
        }
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
    
    pthread_t out_tid, log_tid, cmd_out_tid;
    egress_args out_args = {
        .fd = STDOUT_FILENO,
        .vld = 0,
        .stop = 0,
        .error_str = SUCC,
        .mutex = PTHREAD_MUTEX_INITIALIZER
    };
    egress_args log_args = {
        .fd = log_out_fd,
        .vld = 0,
        .val_sent = 0,
        .stop = 0,
        .error_str = SUCC,
        .mutex = PTHREAD_MUTEX_INITIALIZER
    };
    egress_args cmd_out_args = {
        .fd = cmd_out_fd,
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
    if (cmd_out_fd != -1) {
        pthread_create(&cmd_out_tid, NULL, word_writer, &cmd_out_args);
        pthread_setname_np(cmd_out_tid, "dbg_guv cmd_out");
    }
    
    //When we get a latch signal, we need to save the receipt hdr and data
    //in case the log is not currently ready
    unsigned receipt_hdr;
    unsigned receipt_data;
    unsigned receipt_vld = 0;
    
    //Ugly business because we may only read partial inputs/commands from 
    //the read() system call
    char input_buf[4];
    int input_buf_pos = 0;
    char cmd_buf[8];
    int cmd_buf_pos = 0;
    
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
        int input_valid;
        
        if (input_buf_pos == 4) {
            //Buffer contains complete input flit
            input_valid = 1;
        } else {
            //Need to read more bytes
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
                    //Unfortunate: no guarantee that we will read an entire
                    //input flit (4 bytes) so we need to do this awful
                    //buffering trick. There must be an easier way...
                    //Note to self: try googling "Linux: wait for n bytes to
                    //read" and look more carefully
                    int rc = read(STDIN_FILENO, input_buf + input_buf_pos, 4 - input_buf_pos);
                    if (rc < 0) {
                        perror("Could not read from input");
                        break;
                    }
                    
                    input_buf_pos += rc;
                    //If buffer contains complete input flit
                    input_valid = (input_buf_pos == 4);
                }
            }
        }
        
        //We have an input, now what should we do with it?
        if (input_valid) {
            unsigned input_val = *(unsigned*)input_buf;
            
            //Look at pause/drop/log/flags to figure out if we should read 
            //from the input
            int copy_to_log = (d.log_cnt > 0 || d.keep_logging);
            int copy_to_out = (d.log_cnt == 0 && d.keep_dropping == 0);
            
            int ready_for_input = 
                (!copy_to_out || output_ready) &&
                (!copy_to_log || log_ready) &&
                !d.keep_pausing
            ;
            
            if (ready_for_input) {
                if (copy_to_out) {
                    pthread_mutex_lock(&out_args.mutex);
                    out_args.to_send = input_val;
                    out_args.vld = 1;
                    pthread_mutex_unlock(&out_args.mutex);
                } else {
                    //If we're not copying to the output, it's because we're dropping!
                    if (d.drop_cnt > 0) d.drop_cnt--;
                }
                
                if (copy_to_log) {
                    //Send log header
                    //TODO: BUILD HEADER
                    pthread_mutex_lock(&log_args.mutex);
                    log_args.to_send = guv_addr; //Good enough for simple testing
                    log_args.vld = 1;
                    log_args.val_sent = 0;
                    pthread_mutex_unlock(&log_args.mutex);
                    
                    //Queue up second flit, and mark it as log
                    d.in_log_packet = 1;
                    d.log_data = input_val;
                    d.is_receipt = 0;
                }
                
                //Done reading this input from the buffer
                input_buf_pos = 0;
            }
        } //END if (input_valid)
        
        
        //Read a command 
        
        //Again, we have to do our ugly buffering since there's no guarantee
        //that we read an entire command each time
        struct pollfd can_read_command = {
            .fd = cmd_in_fd,
            .events = POLLIN | POLLHUP
        };
        
        rc = poll(&can_read_command, 1, 0);
        if (rc < 0) {
            perror("Could not issue command reading poll call");
            break;
        } else if (rc > 0) {
            if (can_read_command.revents & POLLHUP) {
                fprintf(stderr, "Command pipe disconnected\n");
                break;
            } else if (can_read_command.revents & POLLIN) {
                //Unfortunate: no guarantee that we will read an entire
                //command (8 bytes) so we need to do this awful
                //buffering trick. 
                int rc = read(cmd_in_fd, cmd_buf + cmd_buf_pos, 8 - cmd_buf_pos);
                if (rc < 0) {
                    perror("Could not read from command stream");
                    break;
                }
                
                cmd_buf_pos += rc;
            }
        }
        
        //If cmd_buf contains full command, parse it and act accordingly
        //More ugly hacking: latch command is a special case
        int is_latch = (cmd_buf_pos >= 4) && (((((unsigned *)cmd_buf)[0]) & 0xF) == 0xF);
        
        if (cmd_buf_pos == 8 || is_latch) {            
            unsigned cmd_addr_raw = ((unsigned *)cmd_buf)[0];
            unsigned cmd_val = ((unsigned *)cmd_buf)[1];
            
            unsigned reg_addr = cmd_addr_raw & 0xF;
            unsigned targeted_guv = (cmd_addr_raw >> 4) & 0xF;
            
            //If this message is not for us, we don't have to do anything
            if (targeted_guv != guv_addr) {
                if (cmd_out_fd != -1) {
                    //If the user daisy-chained the command output
                    pthread_mutex_lock(&cmd_out_args.mutex);
                    //We just overwrite the old command.
                    //I said I would be okay with ugly hacks...
                    cmd_out_args.to_send = cmd_addr_raw;
                    cmd_out_args.vld = 1;
                    cmd_out_args.extra_val = cmd_val;
                    cmd_out_args.extra_vld = is_latch ? 0 : 1;
                    pthread_mutex_unlock(&cmd_out_args.mutex);
                }
                
                //I AM A TERRIBLE PERSON
                if (is_latch) {
                    cmd_buf_pos -= 4;
                    ((unsigned *)cmd_buf)[0] = ((unsigned *)cmd_buf)[1];
                } else {
                    cmd_buf_pos = 0;
                }
                
                sched_yield(); //Not sure if this is really needed, but whatever
                continue;
            }
            
            switch (reg_addr) {
            //Registers currently supported by this software model
            case addr_keep_dropping:
                d.keep_dropping_r = (cmd_val & 1); //Matches what real hardware does
                break;
            case addr_drop_cnt:
                d.drop_cnt_r = (cmd_val & 0x3FF); //Matches what real hardware does
                break;
            case addr_keep_logging:
                d.keep_logging_r = (cmd_val & 1); //Matches what real hardware does
                break;
            case addr_log_cnt:
                d.log_cnt_r = (cmd_val & 0x3FF); //Matches what real hardware does
                break;
            case addr_keep_pausing:
                d.keep_pausing_r = (cmd_val & 1); //Matches what real hardware does
                break;
            case addr_inj_TVALID:
                d.inj_TVALID_r = (cmd_val & 1); //Matches what real hardware does
                break;
            case addr_inj_TDATA:
                d.inj_TDATA_r = cmd_val;
                break;
            }
            
            //This could have been in the above switch, but I thought it 
            //looked cleaner inside in if statement
            if (reg_addr == addr_latch) {
                int inj_failed = 0;
                //Update registers (at least, the ones supported by this model)
                d.keep_dropping = d.keep_dropping_r;
                d.drop_cnt      = d.drop_cnt_r;
                d.keep_logging  = d.keep_logging_r;
                d.log_cnt       = d.log_cnt_r;
                d.keep_pausing  = d.keep_pausing_r;
                
                inj_failed = d.inj_TVALID;
                //Real hardware does not clobber inject values on failure,
                //unless new inj_TVALID is 0
                if (!inj_failed || d.inj_TVALID == 0) {
                    d.inj_TVALID    = d.inj_TVALID_r;
                    d.inj_TDATA     = d.inj_TDATA_r;
                }
                
                //Put command receipt together. Matches my (misguided?)
                //choice in hardware to send back information about the
                //"scratch" registers instead of the real ones
                
                //TODO: proper header needed, but this is good enough for now
                receipt_hdr = 0b10000 | guv_addr;
                receipt_data = 
                    (d.keep_pausing_r  ?        1 : 0) |
                    (d.keep_logging_r  ? (1 << 1) : 0) |
                    (d.keep_dropping_r ? (1 << 2) : 0) |
                    (d.log_cnt_r       ? (1 << 3) : 0) |
                    (d.drop_cnt_r      ? (1 << 4) : 0) |
                    (d.inj_TVALID_r    ? (1 << 5) : 0) |
                    //Skip dut_reset since we don't model it here
                    (inj_failed        ? (1 << 7) : 0)
                    //Skip dout_not_ready_cnt since we don't model it here
                ;
                receipt_vld = 1;
            }
            
            //I AM A TERRIBLE PERSON
            if (is_latch) {
                cmd_buf_pos -= 4;
                ((unsigned *)cmd_buf)[0] = ((unsigned *)cmd_buf)[1];
            } else {
                cmd_buf_pos = 0;
            }
        } //END if (cmd_buf_pos == 8 || is_latch)
            
        sched_yield();
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
    
    if (cmd_in_fd != -1) close(cmd_in_fd);
    if (log_out_fd != -1) close(log_out_fd);
    if (cmd_out_fd != -1) close(cmd_out_fd);
    
    return 0;
}
