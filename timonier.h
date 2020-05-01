#ifndef TIMONIER_H
#define TIMONIER_H 1

//This file collects some different dbg_guv managers

#include "twm.h"
#include "dbg_guv.h"

//Default manager: defaults to parsing a "set" command and has no associated
//state struct
extern guv_operations const default_guv_ops;

//File I/O manager
extern guv_operations const fio_guv_ops;

extern char const * const FIO_SUCCESS;// = "success";
extern char const * const FIO_NONE_OPEN;// = "no open file";
extern char const * const FIO_OVERFLOW;// = "buffer overflowed";
extern char const * const FIO_INJ_TIMEOUT;// = "inject timeout";
extern char const * const FIO_WOKE_EARLY;// = "got an unexpected event while paused";
extern char const * const FIO_IMPOSSIBLE;// = "code reached a location that Marco thought was impossible";
extern char const * const FIO_STRAGGLERS;// = "got EOF, but partial message leftover";
extern char const * const FIO_TOO_FEW_BYTES;// = "got EOF, but not enough bytes for complete message";
extern char const * const FIO_OOM;// = "out of memory";
extern char const * const FIO_BAD_CMD;// = "no such command";
extern char const * const FIO_NULL_ARG;// = "NULL argument";
extern char const * const FIO_BAD_STATE;// = "unexpected signal";
extern char const * const FIO_BAD_DEVELOPER;// = "not implemented";
extern char const * const FIO_ALREADY_SENDING;// = "already sending";

#endif
