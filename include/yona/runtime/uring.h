/*
 * io_uring shared infrastructure — used by file_linux.c, net_linux.c, etc.
 *
 * Raw syscall interface (no liburing dependency). Lazily initialized,
 * thread-safe via mutex. Supports submit-and-wait pattern via user_data IDs.
 *
 * Ring state and the io_ctx table live in src/runtime/platform/uring_linux.c
 * so every platform TU shares one ring. (A header-static singleton used to
 * give file/net/os each their own ring — net submit + file await SIGSEGV.)
 *
 * Header lives under include/yona/runtime/; implementations consume it from
 * src/runtime/platform/ (Linux .c files only).
 */

#ifndef YONA_URING_H
#define YONA_URING_H

#include <stddef.h>
#include <stdint.h>
#include <linux/io_uring.h>

enum io_op_type {
    IO_OP_READ_FILE,
    IO_OP_WRITE_FILE,
    IO_OP_ACCEPT,
    IO_OP_CONNECT,
    IO_OP_SEND,
    IO_OP_RECV,
    IO_OP_RECV_BYTES,
    IO_OP_READ_FILE_BYTES,
    IO_OP_READ_FD_BYTES,
    IO_OP_WRITE_FD_BYTES,
    IO_OP_WRITE_FD_STR,
};

typedef struct {
    enum io_op_type type;
    int fd;
    char* buf;
    size_t buf_size;
    int close_fd;
} io_context_t;

#define IO_CTX_TABLE_SIZE 1024

uint64_t ring_submit_sqe(struct io_uring_sqe *sqe_template);
int32_t ring_await(uint64_t id);
void ring_cancel(uint64_t target_id);
void ring_cancel_group_ios(uint64_t* io_ids, int count);

void io_ctx_put(uint64_t id, io_context_t* ctx);
io_context_t* io_ctx_take(uint64_t id);

#endif /* YONA_URING_H */
