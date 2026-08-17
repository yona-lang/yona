/*
 * kqueue async I/O — macOS counterpart to include/yona/runtime/uring.h.
 *
 * File ops run on a small worker pool and wake awaiters via a kqueue
 * EVFILT_READ pipe (submit-and-return, same ABI as io_uring IDs).
 * Socket ops use EVFILT_READ / EVFILT_WRITE readiness, then the syscall.
 *
 * Ring state and the io_ctx table live in src/runtime/platform/kqueue_macos.c
 * so file/net/os TUs share one kqueue.
 */

#ifndef YONA_KQUEUE_H
#define YONA_KQUEUE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>

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

/* off == (off_t)-1 means the fd's current position (read/write, not pread/pwrite). */
uint64_t kq_submit_read(int fd, void* buf, size_t len, off_t off);
uint64_t kq_submit_write(int fd, const void* buf, size_t len, off_t off);
uint64_t kq_submit_connect(int fd, const void* addr, socklen_t addrlen);
uint64_t kq_submit_accept(int fd, void* addr, socklen_t* addrlen);
uint64_t kq_submit_send(int fd, const void* buf, size_t len);
uint64_t kq_submit_recv(int fd, void* buf, size_t len);
uint64_t kq_submit_nop(void);

int32_t kq_await(uint64_t id);
void kq_cancel(uint64_t target_id);
void kq_cancel_group_ios(uint64_t* io_ids, int count);

void io_ctx_put(uint64_t id, io_context_t* ctx);
io_context_t* io_ctx_take(uint64_t id);

#endif /* YONA_KQUEUE_H */
