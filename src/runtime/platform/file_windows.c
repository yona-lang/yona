/*
 * Windows file I/O — Phase 2 (incremental): whole-file read uses an I/O completion
 * port + worker thread for overlapped ReadFile; other submits still use direct-result
 * IDs. Falls back to synchronous read if IOCP setup or overlapped open fails.
 */

#ifndef _WIN32
#error "file_windows.c is for Windows builds only"
#endif

#ifndef _CRT_DECLARE_NONSTDC_NAMES
#define _CRT_DECLARE_NONSTDC_NAMES 1
#endif

#include "yona/runtime/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <io.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <windows.h>

extern void* yona_rt_rc_alloc_string(size_t bytes);
extern void* yona_rt_byte_array_from_string(const char* s);
extern void yona_rt_rc_inc(void* ptr);
extern void yona_rt_rc_dec(void* ptr);
extern int64_t* yona_rt_seq_alloc(int64_t count);
extern void yona_rt_seq_set(int64_t* seq, int64_t index, int64_t value);
extern void yona_rt_seq_set_heap(int64_t* seq, int64_t flag);
extern void* yona_rt_rc_alloc_string_len(size_t bytes, size_t str_len);
extern void* rc_alloc(int64_t type_tag, size_t payload_bytes);

#define IO_OP_DIRECT_RESULT 99

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
	/* Whole-file async read: ctx lives inside yona_win_read_op; close_fd == 2 marker */
	IO_OP_READ_FILE_IOCP_PENDING,
	/* Generic blocking op offload: ctx lives inside yona_win_blocking_op. */
	IO_OP_WIN32_BLOCKING_PENDING,
	/* Net overlapped/IOCP ops: ctx lives inside yona_win_net_op (net_windows.c). */
	IO_OP_NET_IOCP_PENDING,
};

typedef struct {
	enum io_op_type type;
	int fd;
	char* buf;
	size_t buf_size;
	int close_fd;
} io_context_t;

#define IO_CTX_TABLE_SIZE 1024
static struct {
	uint64_t id;
	io_context_t* ctx;
} io_ctx_table[IO_CTX_TABLE_SIZE];
static CRITICAL_SECTION io_ctx_mutex;
static INIT_ONCE io_ctx_init_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK io_ctx_init_cb(PINIT_ONCE o, PVOID p, PVOID* c) {
	(void)o;
	(void)p;
	(void)c;
	InitializeCriticalSection(&io_ctx_mutex);
	return TRUE;
}

static void io_ctx_ensure_init(void) {
	InitOnceExecuteOnce(&io_ctx_init_once, io_ctx_init_cb, NULL, NULL);
}

static void io_ctx_put(uint64_t id, io_context_t* ctx) {
	io_ctx_ensure_init();
	EnterCriticalSection(&io_ctx_mutex);
	unsigned idx = (unsigned)(id % IO_CTX_TABLE_SIZE);
	for (unsigned i = 0; i < IO_CTX_TABLE_SIZE; i++) {
		unsigned slot = (idx + i) % IO_CTX_TABLE_SIZE;
		if (io_ctx_table[slot].id == 0) {
			io_ctx_table[slot].id = id;
			io_ctx_table[slot].ctx = ctx;
			LeaveCriticalSection(&io_ctx_mutex);
			return;
		}
	}
	LeaveCriticalSection(&io_ctx_mutex);
}

static io_context_t* io_ctx_take(uint64_t id) {
	io_ctx_ensure_init();
	EnterCriticalSection(&io_ctx_mutex);
	unsigned idx = (unsigned)(id % IO_CTX_TABLE_SIZE);
	for (unsigned i = 0; i < IO_CTX_TABLE_SIZE; i++) {
		unsigned slot = (idx + i) % IO_CTX_TABLE_SIZE;
		if (io_ctx_table[slot].id == id) {
			io_context_t* ctx = io_ctx_table[slot].ctx;
			io_ctx_table[slot].id = 0;
			io_ctx_table[slot].ctx = NULL;
			LeaveCriticalSection(&io_ctx_mutex);
			return ctx;
		}
		if (io_ctx_table[slot].id == 0) break;
	}
	LeaveCriticalSection(&io_ctx_mutex);
	return NULL;
}

static volatile LONG64 direct_result_id_seq = (LONG64)0x80000000LL;

static int64_t io_register_direct_result(void* result) {
	uint64_t id = (uint64_t)InterlockedExchangeAdd64(&direct_result_id_seq, 1);
	io_context_t* ctx = (io_context_t*)malloc(sizeof(io_context_t));
	ctx->type = (enum io_op_type)IO_OP_DIRECT_RESULT;
	ctx->fd = -1;
	ctx->buf = (char*)result;
	ctx->buf_size = 0;
	ctx->close_fd = 0;
	io_ctx_put(id, ctx);
	return (int64_t)id;
}

int64_t yona_io_register_direct_result(void* result) {
	return io_register_direct_result(result);
}

typedef struct yona_win_read_op {
	OVERLAPPED ov;
	HANDLE hFile;
	HANDLE hDone;
	io_context_t ctx;
} yona_win_read_op;

#define YONA_WIN_READ_OP_FROM_CTX(c) \
	((yona_win_read_op*)((uintptr_t)(c)-offsetof(yona_win_read_op, ctx)))

typedef int64_t (*yona_win_blocking_fn_t)(void* arg);
typedef void (*yona_win_blocking_cleanup_fn_t)(void* arg);

typedef struct yona_win_blocking_op {
	HANDLE hDone;
	HANDLE hThread;
	void* arg;
	yona_win_blocking_fn_t fn;
	yona_win_blocking_cleanup_fn_t cleanup;
	io_context_t ctx;
} yona_win_blocking_op;

#define YONA_WIN_BLOCKING_OP_FROM_CTX(c) \
	((yona_win_blocking_op*)((uintptr_t)(c)-offsetof(yona_win_blocking_op, ctx)))

static DWORD WINAPI yona_win_blocking_worker(void* p) {
	yona_win_blocking_op* op = (yona_win_blocking_op*)p;
	int64_t result = 0;
	if (op && op->fn)
		result = op->fn(op->arg);
	if (op && op->cleanup)
		op->cleanup(op->arg);
	if (op) {
		op->ctx.buf = (char*)(intptr_t)result;
		op->ctx.type = (enum io_op_type)IO_OP_DIRECT_RESULT;
		SetEvent(op->hDone);
	}
	return 0;
}

static HANDLE iocp_port;
static HANDLE iocp_thread;
static INIT_ONCE iocp_once = INIT_ONCE_STATIC_INIT;

static DWORD WINAPI yona_iocp_worker(void* unused) {
	(void)unused;
	for (;;) {
		DWORD nbytes = 0;
		ULONG_PTR key = 0;
		LPOVERLAPPED ov = NULL;
		BOOL gqc = GetQueuedCompletionStatus(iocp_port, &nbytes, &key, &ov, INFINITE);
		(void)gqc;
		if (!key || !ov)
			continue;
		yona_win_read_op* op = (yona_win_read_op*)key;
		if (ov != &op->ov)
			continue;
		DWORD xfer = 0;
		if (!GetOverlappedResult(op->hFile, ov, &xfer, FALSE))
			xfer = 0;
		if (op->ctx.buf) {
			if (xfer > 0 && xfer <= op->ctx.buf_size)
				op->ctx.buf[xfer] = '\0';
			else
				op->ctx.buf[0] = '\0';
		}
		op->ctx.type = IO_OP_READ_FILE;
		SetEvent(op->hDone);
	}
}

static BOOL CALLBACK yona_iocp_init_cb(PINIT_ONCE o, PVOID p, PVOID* c) {
	(void)o;
	(void)p;
	(void)c;
	iocp_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (!iocp_port)
		return TRUE;
	iocp_thread = CreateThread(NULL, 0, yona_iocp_worker, NULL, 0, NULL);
	if (!iocp_thread) {
		CloseHandle(iocp_port);
		iocp_port = NULL;
	}
	return TRUE;
}

static void yona_iocp_ensure(void) {
	InitOnceExecuteOnce(&iocp_once, yona_iocp_init_cb, NULL, NULL);
}

/* Shared id registration hook for other Windows platform TUs (net_windows.c). */
int64_t yona_win_register_io_ctx(io_context_t* ctx) {
	if (!ctx) return 0;
	uint64_t id = (uint64_t)InterlockedExchangeAdd64(&direct_result_id_seq, 1);
	io_ctx_put(id, ctx);
	return (int64_t)id;
}

static int io_ctx_find_slot_locked(uint64_t id) {
	unsigned idx = (unsigned)(id % IO_CTX_TABLE_SIZE);
	for (unsigned i = 0; i < IO_CTX_TABLE_SIZE; i++) {
		unsigned slot = (idx + i) % IO_CTX_TABLE_SIZE;
		if (io_ctx_table[slot].id == id)
			return (int)slot;
		if (io_ctx_table[slot].id == 0)
			break;
	}
	return -1;
}

int64_t yona_rt_io_await(int64_t uring_id) {
	if (uring_id <= 0) return 0;
	for (;;) {
		EnterCriticalSection(&io_ctx_mutex);
		int slot = io_ctx_find_slot_locked((uint64_t)uring_id);
		if (slot < 0) {
			LeaveCriticalSection(&io_ctx_mutex);
			return 0;
		}
		io_context_t* ctx = io_ctx_table[slot].ctx;
		if (ctx->type == IO_OP_READ_FILE_IOCP_PENDING) {
			LeaveCriticalSection(&io_ctx_mutex);
			yona_win_read_op* op = YONA_WIN_READ_OP_FROM_CTX(ctx);
			WaitForSingleObject(op->hDone, INFINITE);
			continue;
		}
		if (ctx->type == IO_OP_WIN32_BLOCKING_PENDING) {
			LeaveCriticalSection(&io_ctx_mutex);
			yona_win_blocking_op* op = YONA_WIN_BLOCKING_OP_FROM_CTX(ctx);
			WaitForSingleObject(op->hDone, INFINITE);
			continue;
		}
		if (ctx->close_fd == 7 && ctx->type != (enum io_op_type)IO_OP_DIRECT_RESULT) {
			LeaveCriticalSection(&io_ctx_mutex);
			/* net_windows.c stores HANDLE wait-event in buf_size while pending. */
			HANDLE h = (HANDLE)(uintptr_t)ctx->buf_size;
			if (h) {
				WaitForSingleObject(h, INFINITE);
			} else {
				/* Defensive fallback: avoid spinning forever on malformed ctx. */
				return 0;
			}
			continue;
		}
		io_ctx_table[slot].id = 0;
		io_ctx_table[slot].ctx = NULL;
		LeaveCriticalSection(&io_ctx_mutex);

		if (ctx->type == (enum io_op_type)IO_OP_DIRECT_RESULT) {
			int64_t result = (int64_t)(intptr_t)ctx->buf;
			if (ctx->close_fd == 3) {
				yona_win_blocking_op* op = YONA_WIN_BLOCKING_OP_FROM_CTX(ctx);
				if (op->hThread) CloseHandle(op->hThread);
				if (op->hDone) CloseHandle(op->hDone);
				free(op);
				return result;
			}
			if (ctx->close_fd == 7) {
				HANDLE h = (HANDLE)(uintptr_t)ctx->buf_size;
				if (h) CloseHandle(h);
			}
			free(ctx);
			return result;
		}
		if (ctx->type == IO_OP_READ_FILE && ctx->close_fd == 2) {
			yona_win_read_op* op = YONA_WIN_READ_OP_FROM_CTX(ctx);
			int64_t r = (int64_t)(intptr_t)ctx->buf;
			CloseHandle(op->hFile);
			CloseHandle(op->hDone);
			free(op);
			return r;
		}
		if (ctx->buf) free(ctx->buf);
		if (ctx->close_fd == 1 && ctx->fd >= 0) _close(ctx->fd);
		free(ctx);
		return 0;
	}
}

/* Generic offload for blocking operations that still return through io_await IDs. */
int64_t yona_win_submit_blocking(yona_win_blocking_fn_t fn, void* arg,
				 yona_win_blocking_cleanup_fn_t cleanup) {
	if (!fn) {
		if (cleanup) cleanup(arg);
		return io_register_direct_result((void*)(intptr_t)0);
	}
	uint64_t id = (uint64_t)InterlockedExchangeAdd64(&direct_result_id_seq, 1);
	yona_win_blocking_op* op = (yona_win_blocking_op*)calloc(1, sizeof(yona_win_blocking_op));
	if (!op) {
		if (cleanup) cleanup(arg);
		return io_register_direct_result((void*)(intptr_t)0);
	}
	op->hDone = CreateEvent(NULL, TRUE, FALSE, NULL);
	op->arg = arg;
	op->fn = fn;
	op->cleanup = cleanup;
	op->ctx.type = IO_OP_WIN32_BLOCKING_PENDING;
	op->ctx.fd = -1;
	op->ctx.buf = NULL;
	op->ctx.buf_size = 0;
	op->ctx.close_fd = 3; /* embedded ctx in yona_win_blocking_op */
	io_ctx_put(id, &op->ctx);
	op->hThread = CreateThread(NULL, 0, yona_win_blocking_worker, op, 0, NULL);
	if (!op->hThread) {
		if (cleanup) cleanup(arg);
		op->ctx.buf = (char*)(intptr_t)0;
		op->ctx.type = (enum io_op_type)IO_OP_DIRECT_RESULT;
		if (op->hDone) SetEvent(op->hDone);
	}
	return (int64_t)id;
}

static char* read_file_blocking(const char* path) {
	int fd = open(path, O_RDONLY | O_BINARY);
	if (fd < 0) {
		char* empty = (char*)yona_rt_rc_alloc_string(1);
		empty[0] = '\0';
		return empty;
	}
	struct stat st;
	if (fstat(fd, &st) < 0) {
		_close(fd);
		char* e = (char*)yona_rt_rc_alloc_string(1);
		e[0] = '\0';
		return e;
	}
	size_t size = (size_t)st.st_size;
	char* buf = (char*)yona_rt_rc_alloc_string_len(size + 1, size);
	int n = (int)read(fd, buf, size);
	if (n >= 0) buf[n] = '\0';
	else buf[0] = '\0';
	_close(fd);
	return buf;
}

typedef struct {
	char* path;
	char* content;
} win_write_file_req_t;

static void win_write_file_req_cleanup(void* p) {
	win_write_file_req_t* req = (win_write_file_req_t*)p;
	if (!req) return;
	if (req->path) free(req->path);
	if (req->content) free(req->content);
	free(req);
}

static int64_t win_write_file_blocking(void* p) {
	win_write_file_req_t* req = (win_write_file_req_t*)p;
	if (!req || !req->path || !req->content) return 0;
	int ok = yona_platform_write_file(req->path, req->content);
	return (ok == 0) ? 1 : 0;
}

typedef struct {
	char* path;
} win_read_file_req_t;

static void win_read_file_req_cleanup(void* p) {
	win_read_file_req_t* req = (win_read_file_req_t*)p;
	if (!req) return;
	if (req->path) free(req->path);
	free(req);
}

static int64_t win_read_file_bytes_blocking(void* p) {
	win_read_file_req_t* req = (win_read_file_req_t*)p;
	if (!req || !req->path) return 0;
	void* bytes = yona_rt_byte_array_from_string(read_file_blocking(req->path));
	return (int64_t)(intptr_t)bytes;
}

typedef struct {
	void (*finalize)(void*);
	int fd;
	char* buf;
	size_t total;
} win_write_fd_strs_req_t;

static void win_write_fd_strs_req_cleanup(void* p) {
	win_write_fd_strs_req_t* req = (win_write_fd_strs_req_t*)p;
	if (!req) return;
	if (req->buf) free(req->buf);
	free(req);
}

static int64_t win_write_fd_strs_blocking(void* p) {
	win_write_fd_strs_req_t* req = (win_write_fd_strs_req_t*)p;
	if (!req) return -1;
	int n = (int)write(req->fd, req->buf ? req->buf : "", (unsigned)req->total);
	return (n >= 0) ? (int64_t)n : -1;
}

typedef struct {
	int fd;
	int64_t count;
	int64_t offset;
} win_read_fd_bytes_req_t;

static void win_read_fd_bytes_req_cleanup(void* p) { free(p); }

static int64_t win_read_fd_bytes_blocking(void* p) {
	win_read_fd_bytes_req_t* req = (win_read_fd_bytes_req_t*)p;
	if (!req || req->count < 0) return 0;
	if (_lseeki64(req->fd, req->offset, SEEK_SET) < 0) return 0;
	int64_t* bytes_buf = (int64_t*)rc_alloc(8, sizeof(int64_t) + (size_t)req->count);
	bytes_buf[0] = 0;
	int n = (int)read(req->fd, (char*)(bytes_buf + 1), (unsigned)req->count);
	if (n < 0) n = 0;
	bytes_buf[0] = (int64_t)n;
	return (int64_t)(intptr_t)bytes_buf;
}

typedef struct {
	int fd;
	void* bytes;
	int64_t offset;
} win_write_fd_bytes_req_t;

static void win_write_fd_bytes_req_cleanup(void* p) {
	win_write_fd_bytes_req_t* req = (win_write_fd_bytes_req_t*)p;
	if (!req) return;
	if (req->bytes) yona_rt_rc_dec(req->bytes);
	free(req);
}

static int64_t win_write_fd_bytes_blocking(void* p) {
	win_write_fd_bytes_req_t* req = (win_write_fd_bytes_req_t*)p;
	if (!req || !req->bytes) return -1;
	int64_t* b = (int64_t*)req->bytes;
	int64_t len = b[0];
	if (len < 0) len = 0;
	uint8_t* data = (uint8_t*)(b + 1);
	if (_lseeki64(req->fd, req->offset, SEEK_SET) < 0) return -1;
	int n = (int)write(req->fd, data, (unsigned)len);
	return (n >= 0) ? (int64_t)n : -1;
}

/* Returns 0 to fall back to synchronous read + direct-result registration. */
static int64_t read_file_submit_try_iocp(const char* path) {
	yona_iocp_ensure();
	if (!iocp_port)
		return 0;

	HANDLE hf = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
				FILE_FLAG_OVERLAPPED, NULL);
	if (hf == INVALID_HANDLE_VALUE)
		return 0;

	LARGE_INTEGER li;
	if (!GetFileSizeEx(hf, &li)) {
		CloseHandle(hf);
		return 0;
	}
	LONGLONG sz64 = li.QuadPart;
	if (sz64 < 0) {
		CloseHandle(hf);
		return 0;
	}
	if (sz64 == 0) {
		CloseHandle(hf);
		return 0;
	}
	size_t size = (size_t)sz64;
	if (size > (size_t)UINT32_MAX) {
		CloseHandle(hf);
		return 0;
	}
	DWORD dwsz = (DWORD)size;

	yona_win_read_op* op = (yona_win_read_op*)calloc(1, sizeof(*op));
	if (!op) {
		CloseHandle(hf);
		return 0;
	}
	op->hFile = hf;
	op->hDone = CreateEventA(NULL, TRUE, FALSE, NULL);
	if (!op->hDone) {
		CloseHandle(hf);
		free(op);
		return 0;
	}
	memset(&op->ov, 0, sizeof(op->ov));
	op->ctx.type = IO_OP_READ_FILE_IOCP_PENDING;
	op->ctx.fd = -1;
	op->ctx.close_fd = 2;
	op->ctx.buf = (char*)yona_rt_rc_alloc_string_len((size_t)dwsz + 1, (size_t)dwsz);
	op->ctx.buf_size = (size_t)dwsz;

	HANDLE assoc = CreateIoCompletionPort(hf, iocp_port, (ULONG_PTR)op, 0);
	if (assoc != iocp_port) {
		CloseHandle(op->hDone);
		CloseHandle(hf);
		free(op);
		return 0;
	}

	uint64_t id = (uint64_t)InterlockedExchangeAdd64(&direct_result_id_seq, 1);
	io_ctx_put(id, &op->ctx);

	DWORD rd = 0;
	BOOL rf = ReadFile(hf, op->ctx.buf, dwsz, &rd, &op->ov);
	if (rf) {
		if (rd > 0 && rd <= dwsz)
			op->ctx.buf[rd] = '\0';
		else
			op->ctx.buf[0] = '\0';
		op->ctx.type = IO_OP_READ_FILE;
		SetEvent(op->hDone);
	} else {
		DWORD err = GetLastError();
		if (err != ERROR_IO_PENDING) {
			(void)io_ctx_take(id);
			CloseHandle(op->hDone);
			CloseHandle(hf);
			if (op->ctx.buf)
				yona_rt_rc_dec(op->ctx.buf);
			free(op);
			return 0;
		}
	}
	return (int64_t)id;
}

int64_t yona_platform_read_file_submit(const char* path) {
	int64_t id = read_file_submit_try_iocp(path);
	if (id > 0)
		return id;
	return io_register_direct_result((void*)(intptr_t)read_file_blocking(path));
}

int64_t yona_platform_write_file_submit(const char* path, const char* content) {
	int ok = yona_platform_write_file(path, content);
	return io_register_direct_result((void*)(intptr_t)(ok == 0 ? 1 : 0));
}

int64_t yona_platform_read_file_bytes_submit(const char* path) {
	void* bytes = yona_rt_byte_array_from_string(read_file_blocking(path));
	return io_register_direct_result(bytes);
}

char* yona_platform_read_file(const char* path) {
	return read_file_blocking(path);
}

int yona_platform_write_file(const char* path, const char* content) {
	FILE* f = fopen(path, "wb");
	if (!f) return -1;
	size_t len = strlen(content);
	size_t w = fwrite(content, 1, len, f);
	fclose(f);
	return (w == len) ? 0 : -1;
}

int yona_platform_append_file(const char* path, const char* content) {
	FILE* f = fopen(path, "ab");
	if (!f) return -1;
	size_t len = strlen(content);
	size_t w = fwrite(content, 1, len, f);
	fclose(f);
	return (w == len) ? 0 : -1;
}

int yona_platform_file_exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0 ? 1 : 0;
}

int yona_platform_remove_file(const char* path) {
	return remove(path) == 0 ? 0 : -1;
}

int64_t yona_platform_file_size(const char* path) {
	struct stat st;
	if (stat(path, &st) != 0) return -1;
	return (int64_t)st.st_size;
}

int64_t yona_platform_open_file_handle(const char* path, int64_t mode_tag) {
	int flags = O_RDONLY;
	if (mode_tag == 1) flags = O_WRONLY | O_CREAT | O_TRUNC;       /* Write */
	else if (mode_tag == 2) flags = O_RDWR | O_CREAT;              /* ReadWrite */
	else if (mode_tag == 3) flags = O_WRONLY | O_CREAT | O_APPEND; /* Append */
	flags |= O_BINARY;
	return (int64_t)open(path, flags, 0644);
}

int64_t yona_platform_close_file_handle(int fd) {
	return (int64_t)close(fd);
}

int64_t yona_platform_seek_file_handle(int fd, int64_t offset, int64_t whence_tag) {
	int whence = SEEK_SET;
	if (whence_tag == 1) whence = SEEK_CUR;
	else if (whence_tag == 2) whence = SEEK_END;
	return (int64_t)_lseeki64(fd, (__int64)offset, whence);
}

int64_t yona_platform_tell_file_handle(int fd) {
	return (int64_t)_lseeki64(fd, 0, SEEK_CUR);
}

int64_t yona_platform_advance_file_handle(int fd, int64_t delta) {
	return (int64_t)_lseeki64(fd, (__int64)delta, SEEK_CUR);
}

int64_t yona_platform_flush_file_handle(int fd) {
	return _commit(fd) == 0 ? 1 : 0;
}

int64_t yona_platform_truncate_file_handle(int fd, int64_t length) {
	return _chsize_s(fd, (__int64)length) == 0 ? 1 : 0;
}

int64_t* yona_platform_list_dir(const char* path) {
	char pattern[MAX_PATH];
	snprintf(pattern, sizeof(pattern), "%s\\*", path);
	WIN32_FIND_DATAA fd;
	HANDLE h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE) return yona_rt_seq_alloc(0);

	int64_t count = 0;
	do {
		if (fd.cFileName[0] == '.' &&
		    (fd.cFileName[1] == '\0' || (fd.cFileName[1] == '.' && fd.cFileName[2] == '\0')))
			continue;
		count++;
	} while (FindNextFileA(h, &fd));
	FindClose(h);

	int64_t* seq = yona_rt_seq_alloc(count);
	h = FindFirstFileA(pattern, &fd);
	if (h == INVALID_HANDLE_VALUE) {
		/* race */
		return yona_rt_seq_alloc(0);
	}
	int64_t i = 0;
	do {
		if (fd.cFileName[0] == '.' &&
		    (fd.cFileName[1] == '\0' || (fd.cFileName[1] == '.' && fd.cFileName[2] == '\0')))
			continue;
		size_t len = strlen(fd.cFileName);
		char* name = (char*)yona_rt_rc_alloc_string(len + 1);
		memcpy(name, fd.cFileName, len + 1);
		yona_rt_seq_set(seq, i, (int64_t)(intptr_t)name);
		i++;
	} while (FindNextFileA(h, &fd));
	yona_rt_seq_set_heap(seq, 1);
	FindClose(h);
	return seq;
}

#define LINE_ITER_BUF_SIZE 65536

typedef struct {
	int fd;
	char* buf;
	size_t buf_pos;
	size_t buf_len;
	int eof;
} line_iter_state_t;

static void line_iter_finalize(void* raw) {
	line_iter_state_t* st = (line_iter_state_t*)raw;
	if (st->fd >= 0) close(st->fd);
	free(st->buf);
}

static int64_t line_iter_next(int64_t* closure_env) {
	line_iter_state_t* st = (line_iter_state_t*)(intptr_t)closure_env[6];
	if (st->eof) {
		int64_t* adt = (int64_t*)rc_alloc(4, 3 * sizeof(int64_t));
		adt[0] = 1;
		adt[1] = 0;
		adt[2] = 0;
		return (int64_t)(intptr_t)adt;
	}
	char line_buf[8192];
	size_t line_len = 0;
	for (;;) {
		if (st->buf_pos >= st->buf_len) {
			int n = (int)read(st->fd, st->buf, LINE_ITER_BUF_SIZE);
			if (n <= 0) {
				st->eof = 1;
				if (line_len == 0) {
					int64_t* adt = (int64_t*)rc_alloc(4, 3 * sizeof(int64_t));
					adt[0] = 1;
					adt[1] = 0;
					adt[2] = 0;
					return (int64_t)(intptr_t)adt;
				}
				break;
			}
			st->buf_len = (size_t)n;
			st->buf_pos = 0;
		}
		while (st->buf_pos < st->buf_len) {
			char c = st->buf[st->buf_pos++];
			if (c == '\n') goto line_done;
			if (line_len < sizeof(line_buf) - 1) line_buf[line_len++] = c;
		}
	}
line_done:
	line_buf[line_len] = '\0';
	char* line_str = (char*)yona_rt_rc_alloc_string_len(line_len + 1, line_len);
	memcpy(line_str, line_buf, line_len + 1);
	int64_t* adt = (int64_t*)rc_alloc(4, 4 * sizeof(int64_t));
	adt[0] = 0;
	adt[1] = 1;
	adt[2] = 1;
	adt[3] = (int64_t)(intptr_t)line_str;
	return (int64_t)(intptr_t)adt;
}

int64_t yona_rt_file_line_iterator(const char* path) {
	int fd = open(path, O_RDONLY);
	extern void* yona_rt_native_state_alloc(size_t, void (*)(void*));
	line_iter_state_t* st = (line_iter_state_t*)
		yona_rt_native_state_alloc(sizeof(line_iter_state_t), line_iter_finalize);
	if (fd < 0) {
		st->fd = -1;
		st->buf = NULL;
		st->buf_pos = 0;
		st->buf_len = 0;
		st->eof = 1;
	} else {
		st->fd = fd;
		st->buf = (char*)malloc(LINE_ITER_BUF_SIZE);
		st->buf_pos = 0;
		st->buf_len = 0;
		st->eof = 0;
	}
	extern void* yona_rt_closure_create(void* fn_ptr, int64_t ret_tag, int64_t arity, int64_t num_caps);
	int64_t* closure = (int64_t*)yona_rt_closure_create((void*)line_iter_next, 0, 0, 1);
	extern void yona_rt_closure_set_cap(void* closure, int64_t idx, int64_t val);
	yona_rt_closure_set_cap(closure, 0, (int64_t)(intptr_t)st);
	extern void yona_rt_closure_set_heap_mask(void* closure, int64_t mask);
	yona_rt_closure_set_heap_mask(closure, 1);
	int64_t* iter_adt = (int64_t*)rc_alloc(4, 4 * sizeof(int64_t));
	iter_adt[0] = 0;
	iter_adt[1] = 1;
	iter_adt[2] = 1;
	iter_adt[3] = (int64_t)(intptr_t)closure;
	return (int64_t)(intptr_t)iter_adt;
}

typedef struct {
	void (*finalize)(void*);
	int fd;
	int64_t position;
	int64_t chunk_size;
	int eof;
} chunk_iter_state_t;

static int64_t chunk_iter_next(int64_t* env) {
	chunk_iter_state_t* st = (chunk_iter_state_t*)(intptr_t)env[6];
	if (st->eof) {
		int64_t* none = (int64_t*)rc_alloc(4, 3 * sizeof(int64_t));
		none[0] = 1;
		none[1] = 0;
		none[2] = 0;
		return (int64_t)(intptr_t)none;
	}
	int64_t* buf = (int64_t*)rc_alloc(8, sizeof(int64_t) + (size_t)st->chunk_size);
	buf[0] = 0;
	if (_lseeki64(st->fd, st->position, SEEK_SET) < 0) {
		yona_rt_rc_dec(buf);
		st->eof = 1;
		int64_t* none = (int64_t*)rc_alloc(4, 3 * sizeof(int64_t));
		none[0] = 1;
		none[1] = 0;
		none[2] = 0;
		return (int64_t)(intptr_t)none;
	}
	int n = (int)read(st->fd, (char*)(buf + 1), (unsigned)(st->chunk_size));
	if (n <= 0) {
		st->eof = 1;
		yona_rt_rc_dec(buf);
		int64_t* none = (int64_t*)rc_alloc(4, 3 * sizeof(int64_t));
		none[0] = 1;
		none[1] = 0;
		none[2] = 0;
		return (int64_t)(intptr_t)none;
	}
	buf[0] = n;
	st->position += n;
	int64_t* some = (int64_t*)rc_alloc(4, 4 * sizeof(int64_t));
	some[0] = 0;
	some[1] = 1;
	some[2] = 1;
	some[3] = (int64_t)(intptr_t)buf;
	return (int64_t)(intptr_t)some;
}

int64_t yona_rt_file_chunk_iterator(int64_t fd, int64_t chunk_size) {
	extern void* yona_rt_native_state_alloc(size_t, void (*)(void*));
	chunk_iter_state_t* st = (chunk_iter_state_t*)
		yona_rt_native_state_alloc(sizeof(chunk_iter_state_t), NULL);
	st->fd = (int)fd;
	st->position = 0;
	st->chunk_size = chunk_size;
	st->eof = 0;
	extern void* yona_rt_closure_create(void* fn_ptr, int64_t ret_tag, int64_t arity, int64_t num_caps);
	int64_t* closure = (int64_t*)yona_rt_closure_create((void*)chunk_iter_next, 0, 0, 1);
	extern void yona_rt_closure_set_cap(void* closure, int64_t idx, int64_t val);
	yona_rt_closure_set_cap(closure, 0, (int64_t)(intptr_t)st);
	extern void yona_rt_closure_set_heap_mask(void* closure, int64_t mask);
	yona_rt_closure_set_heap_mask(closure, 1);
	int64_t* iter_adt = (int64_t*)rc_alloc(4, 4 * sizeof(int64_t));
	iter_adt[0] = 0;
	iter_adt[1] = 1;
	iter_adt[2] = 1;
	iter_adt[3] = (int64_t)(intptr_t)closure;
	return (int64_t)(intptr_t)iter_adt;
}

static int64_t write_fd_strs_submit_impl(int fd, const char* s1, const char* s2) {
	size_t l1 = s1 ? strlen(s1) : 0;
	size_t l2 = s2 ? strlen(s2) : 0;
	size_t total = l1 + l2;
	char* buf = (char*)malloc(total + 1);
	if (l1) memcpy(buf, s1, l1);
	if (l2) memcpy(buf + l1, s2, l2);
	buf[total] = '\0';
	int n = (int)write(fd, buf, (unsigned)total);
	free(buf);
	return io_register_direct_result((void*)(intptr_t)(n >= 0 ? (int64_t)n : -1));
}

int64_t yona_platform_write_fd_str_submit(int fd, const char* s) {
	return write_fd_strs_submit_impl(fd, s, NULL);
}

int64_t yona_platform_write_fd_strs_submit(int fd, const char* s1, const char* s2) {
	return write_fd_strs_submit_impl(fd, s1, s2);
}

const char* yona_platform_read_line_fd(int fd) {
	size_t cap = 512;
	size_t len = 0;
	char* b = (char*)malloc(cap);
	for (;;) {
		if (len + 1 >= cap) {
			cap *= 2;
			b = (char*)realloc(b, cap);
		}
		char ch;
		int n = (int)read(fd, &ch, 1);
		if (n <= 0) {
			if (len == 0) {
				free(b);
				return NULL;
			}
			break;
		}
		if (ch == '\n') break;
		if (ch == '\r') continue;
		b[len++] = ch;
	}
	char* out = (char*)yona_rt_rc_alloc_string(len + 1);
	memcpy(out, b, len);
	out[len] = '\0';
	free(b);
	return out;
}

int64_t yona_platform_read_fd_bytes_submit(int fd, int64_t count, int64_t offset) {
	if (_lseeki64(fd, offset, SEEK_SET) < 0) return 0;
	int64_t* bytes_buf = (int64_t*)rc_alloc(8, sizeof(int64_t) + (size_t)count);
	bytes_buf[0] = 0;
	int n = (int)read(fd, (char*)(bytes_buf + 1), (unsigned)count);
	if (n < 0) n = 0;
	bytes_buf[0] = (int64_t)n;
	return io_register_direct_result(bytes_buf);
}

int64_t yona_platform_write_fd_bytes_submit(int fd, void* bytes, int64_t offset) {
	int64_t* b = (int64_t*)bytes;
	int64_t len = b[0];
	uint8_t* data = (uint8_t*)(b + 1);
	if (_lseeki64(fd, offset, SEEK_SET) < 0) return io_register_direct_result((void*)(intptr_t)-1);
	int n = (int)write(fd, data, (unsigned)len);
	yona_rt_rc_dec(bytes);
	return io_register_direct_result((void*)(intptr_t)(n >= 0 ? (int64_t)n : -1));
}
