#ifndef TIMONIER_H
#define TIMONIER_H 1

//This file collects some different dbg_guv managers

#include "dbg_cmd.h"
#include "twm.h"
#include "dbg_guv.h"

//Global list of fast and slow callbacks, plus methods to modify it
typedef struct _guv_list {
    dbg_guv *d;
    struct _guv_list *next;
    struct _guv_list *prev;
} guv_list;

extern guv_list fast_update_head;
extern guv_list slow_update_head;

void register_fast_update(dbg_guv *d);
void register_slow_update(dbg_guv *d);
void deregister_fast_update(dbg_guv *d);
void deregister_slow_update(dbg_guv *d);

//Default manager: defaults to parsing a "set" command and has no associated
//state struct
extern guv_operations const default_guv_ops;

//File transmission manager
typedef struct _file_tx_timonier {
    int fd;
    //etc, I'll finish this once I have the basic stuff up and running
} file_tx_manager;

extern guv_operations const fio_guv_ops;

#endif
