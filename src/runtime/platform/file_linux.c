/*
 * Linux file I/O — submit-and-return via io_uring.
 *
 * Each async function submits to io_uring and returns the user_data ID
 * immediately. Completion is handled by yona_rt_io_await() which does
 * type-specific post-processing (close fd, null-terminate buffer, etc).
 */

#include "yona/runtime/platform.h"
#include "yona/runtime/uring.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>

extern void* yona_rt_rc_alloc_string(size_t bytes);
extern void yona_rt_rc_inc(void* ptr);
extern void yona_rt_rc_dec(void* ptr);
extern int64_t* yona_rt_seq_alloc(int64_t count);

/* ===== Generic io_uring completer ===== */

/* Sentinel: when io_uring is unavailable, IO functions store the direct
 * result in the io_ctx table with a special type. io_await returns it. */
#define IO_OP_DIRECT_RESULT 99
static _Atomic uint64_t direct_result_id = 0x80000000ULL; /* high offset avoids uring ID collision */

/* Register a direct (blocking fallback) result for io_await. */
static int64_t io_register_direct_result(void* result) {
    uint64_t id = atomic_fetch_add(&direct_result_id, 1);
    io_context_t* ctx = (io_context_t*)malloc(sizeof(io_context_t));
    ctx->type = IO_OP_DIRECT_RESULT;
    ctx->fd = -1;
    ctx->buf = (char*)result;
    ctx->buf_size = 0;
    ctx->close_fd = 0;
    io_ctx_put(id, ctx);
    return (int64_t)id;
}

/* Exported for compiled_runtime.c wrapper functions */
int64_t yona_io_register_direct_result(void* result) {
    return io_register_direct_result(result);
}

int64_t yona_rt_io_await(int64_t uring_id) {
    if (uring_id <= 0) return 0;
    io_context_t* ctx = io_ctx_take((uint64_t)uring_id);
    if (ctx && ctx->type == IO_OP_DIRECT_RESULT) {
        /* Blocking fallback: result is stored in buf pointer */
        int64_t result = (int64_t)(intptr_t)ctx->buf;
        free(ctx);
        return result;
    }
    if (ctx) io_ctx_put((uint64_t)uring_id, ctx); /* put it back for real await */

    int32_t res = ring_await((uint64_t)uring_id);

    /* Handle cancellation: clean up context and raise */
    if (res == -125 /* ECANCELED */) {
        ctx = io_ctx_take((uint64_t)uring_id);
        if (ctx) {
            if (ctx->buf) free(ctx->buf);
            if (ctx->close_fd && ctx->fd >= 0) close(ctx->fd);
            free(ctx);
        }
        return 0; /* Cancelled — caller checks group error */
    }

    ctx = io_ctx_take((uint64_t)uring_id);
    if (!ctx) return (int64_t)res;

    int64_t result;
    switch (ctx->type) {
        case IO_OP_READ_FILE:
            if (res >= 0) ctx->buf[res] = '\0';
            else ctx->buf[0] = '\0';
            if (ctx->close_fd) close(ctx->fd);
            result = (int64_t)(intptr_t)ctx->buf;
            break;
        case IO_OP_WRITE_FILE:
            if (ctx->close_fd) close(ctx->fd);
            /* Unpin the content buffer (was rc_inc'd at submit) */
            if (ctx->buf) yona_rt_rc_dec(ctx->buf);
            result = (res == (int32_t)ctx->buf_size) ? 1 : 0;
            break;
        case IO_OP_ACCEPT:
            free(ctx->buf);
            result = (res >= 0) ? (int64_t)res : -1;
            break;
        case IO_OP_CONNECT:
            free(ctx->buf);
            result = (res >= 0) ? (int64_t)ctx->fd : -1;
            if (res < 0) close(ctx->fd);
            break;
        case IO_OP_SEND:
            /* Unpin the send buffer (was rc_inc'd at submit) */
            if (ctx->buf) yona_rt_rc_dec(ctx->buf);
            result = (int64_t)res;
            break;
        case IO_OP_RECV:
            if (res > 0) ctx->buf[res] = '\0';
            else ctx->buf[0] = '\0';
            result = (int64_t)(intptr_t)ctx->buf;
            break;
        case IO_OP_RECV_BYTES: {
            /* Bytes: set length field, return the Bytes buffer */
            int64_t* bytes_buf = (int64_t*)(intptr_t)ctx->buf;
            bytes_buf[0] = (res > 0) ? (int64_t)res : 0;
            result = (int64_t)(intptr_t)bytes_buf;
            break;
        }
        case IO_OP_READ_FILE_BYTES: {
            int64_t* bytes_buf = (int64_t*)(intptr_t)ctx->buf;
            bytes_buf[0] = (res > 0) ? (int64_t)res : 0;
            if (ctx->close_fd) close(ctx->fd);
            result = (int64_t)(intptr_t)bytes_buf;
            break;
        }
        case IO_OP_READ_FD_BYTES: {
            int64_t* bytes_buf = (int64_t*)(intptr_t)ctx->buf;
            bytes_buf[0] = (res > 0) ? (int64_t)res : 0;
            /* Don't close fd — caller owns the handle */
            result = (int64_t)(intptr_t)bytes_buf;
            break;
        }
        case IO_OP_WRITE_FD_BYTES: {
            /* Unpin the write buffer */
            if (ctx->buf) yona_rt_rc_dec(ctx->buf);
            /* Don't close fd — caller owns the handle */
            result = (res >= 0) ? (int64_t)res : -1;
            break;
        }
        case IO_OP_WRITE_FD_STR: {
            /* Caller allocated the concatenated string via malloc. Free
             * it now that the kernel has consumed it. */
            if (ctx->buf) free(ctx->buf);
            result = (res >= 0) ? (int64_t)res : -1;
            break;
        }
        default:
            result = (int64_t)res;
            break;
    }
    free(ctx);
    return result;
}

/* ===== Submit-and-return functions ===== */

/* Blocking fallback: read entire file synchronously.
 * Used when io_uring is unavailable (ENOMEM, old kernel, container). */
static int64_t read_file_blocking(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        char* empty = (char*)yona_rt_rc_alloc_string(1);
        empty[0] = '\0';
        return (int64_t)(intptr_t)empty;
    }
    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); char* e = (char*)yona_rt_rc_alloc_string(1); e[0]='\0'; return (int64_t)(intptr_t)e; }
    size_t size = (size_t)st.st_size;
    extern void* yona_rt_rc_alloc_string_len(size_t bytes, size_t str_len);
    char* buf = (char*)yona_rt_rc_alloc_string_len(size + 1, size);
    ssize_t n = read(fd, buf, size);
    if (n >= 0) buf[n] = '\0'; else buf[0] = '\0';
    close(fd);
    return (int64_t)(intptr_t)buf;
}

/* readFile: open, allocate buffer, submit uring read, return ID.
 * Uses rc_alloc_string_len to store the file size in the RC header,
 * enabling O(1) string length instead of O(n) strlen. */
int64_t yona_platform_read_file_submit(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return 0; }
    size_t size = (size_t)st.st_size;
    extern void* yona_rt_rc_alloc_string_len(size_t bytes, size_t str_len);
    char* buf = (char*)yona_rt_rc_alloc_string_len(size + 1, size);

    io_context_t* ctx = (io_context_t*)malloc(sizeof(io_context_t));
    ctx->type = IO_OP_READ_FILE;
    ctx->fd = fd;
    ctx->buf = buf;
    ctx->buf_size = size;
    ctx->close_fd = 1;

    struct io_uring_sqe sqe;
    memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_READ;
    sqe.fd = fd;
    sqe.addr = (unsigned long)buf;
    sqe.len = (unsigned)size;
    sqe.off = 0;

    uint64_t id = ring_submit_sqe(&sqe);
    if (id == 0) {
        /* io_uring unavailable — fall back to blocking read */
        close(fd);
        free(ctx);
        return 0;
    }
    io_ctx_put(id, ctx);
    return (int64_t)id;
}

/* writeFile: open, submit uring write, return ID */
int64_t yona_platform_write_file_submit(const char* path, const char* content) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;
    size_t len = strlen(content);

    /* Copy content to RC-managed buffer so it survives until I/O completes.
     * Cannot rc_inc the original — it may be a string constant without RC header. */
    char* pinned = (char*)yona_rt_rc_alloc_string(len + 1);
    memcpy(pinned, content, len + 1);

    io_context_t* ctx = (io_context_t*)malloc(sizeof(io_context_t));
    ctx->type = IO_OP_WRITE_FILE;
    ctx->fd = fd;
    ctx->buf = pinned; /* rc_dec'd in completer */
    ctx->buf_size = len;
    ctx->close_fd = 1;

    struct io_uring_sqe sqe;
    memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_WRITE;
    sqe.fd = fd;
    sqe.addr = (unsigned long)pinned; /* use RC-managed copy */
    sqe.len = (unsigned)len;
    sqe.off = 0;

    uint64_t id = ring_submit_sqe(&sqe);
    if (id == 0) { close(fd); yona_rt_rc_dec(pinned); free(ctx); return 0; }
    io_ctx_put(id, ctx);
    return (int64_t)id;
}

/* readFileBytes: like readFile but returns Bytes instead of String */
int64_t yona_platform_read_file_bytes_submit(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return 0; }
    size_t size = (size_t)st.st_size;

    extern void* rc_alloc(int64_t type_tag, size_t payload_bytes);
    int64_t* buf = (int64_t*)rc_alloc(8 /* RC_TYPE_BYTE_ARRAY */, sizeof(int64_t) + size);
    buf[0] = 0; /* length set by completer */

    io_context_t* ctx = (io_context_t*)malloc(sizeof(io_context_t));
    ctx->type = IO_OP_READ_FILE_BYTES;
    ctx->fd = fd;
    ctx->buf = (char*)buf;
    ctx->buf_size = size;
    ctx->close_fd = 1;

    struct io_uring_sqe sqe;
    memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_READ;
    sqe.fd = fd;
    sqe.addr = (unsigned long)(uint8_t*)(buf + 1);
    sqe.len = (unsigned)size;
    sqe.off = 0;

    uint64_t id = ring_submit_sqe(&sqe);
    if (id == 0) { close(fd); free(ctx); return 0; }
    io_ctx_put(id, ctx);
    return (int64_t)id;
}

/* ===== Handle-based binary I/O submit functions ===== */

/* pread from open fd at given offset. Does NOT close fd. */
int64_t yona_platform_read_fd_bytes_submit(int fd, int64_t count, int64_t offset) {
    extern void* rc_alloc(int64_t type_tag, size_t payload_bytes);
    int64_t* buf = (int64_t*)rc_alloc(8 /* RC_TYPE_BYTE_ARRAY */, sizeof(int64_t) + (size_t)count);
    buf[0] = 0; /* length set by completer */

    io_context_t* ctx = (io_context_t*)malloc(sizeof(io_context_t));
    ctx->type = IO_OP_READ_FD_BYTES;
    ctx->fd = fd;
    ctx->buf = (char*)buf;
    ctx->buf_size = (size_t)count;
    ctx->close_fd = 0; /* caller owns the handle */

    struct io_uring_sqe sqe;
    memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_READ;
    sqe.fd = fd;
    sqe.addr = (unsigned long)(uint8_t*)(buf + 1);
    sqe.len = (unsigned)count;
    sqe.off = (uint64_t)offset; /* pread semantics */

    uint64_t id = ring_submit_sqe(&sqe);
    if (id == 0) {
        /* Fallback: blocking pread */
        ssize_t n = pread(fd, (uint8_t*)(buf + 1), (size_t)count, (off_t)offset);
        buf[0] = (n > 0) ? n : 0;
        return io_register_direct_result(buf);
    }
    io_ctx_put(id, ctx);
    return (int64_t)id;
}

/* pwrite to open fd at given offset. Does NOT close fd. */
int64_t yona_platform_write_fd_bytes_submit(int fd, void* bytes, int64_t offset) {
    int64_t* b = (int64_t*)bytes;
    int64_t len = b[0];
    uint8_t* data = (uint8_t*)(b + 1);

    /* Pin the bytes buffer — rc_inc so it stays alive during async I/O */
    yona_rt_rc_inc(bytes);

    io_context_t* ctx = (io_context_t*)malloc(sizeof(io_context_t));
    ctx->type = IO_OP_WRITE_FD_BYTES;
    ctx->fd = fd;
    ctx->buf = (char*)bytes; /* rc_dec'd in completer */
    ctx->buf_size = (size_t)len;
    ctx->close_fd = 0;

    struct io_uring_sqe sqe;
    memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_WRITE;
    sqe.fd = fd;
    sqe.addr = (unsigned long)data;
    sqe.len = (unsigned)len;
    sqe.off = (uint64_t)offset; /* pwrite semantics */

    uint64_t id = ring_submit_sqe(&sqe);
    if (id == 0) {
        /* Fallback: blocking pwrite */
        ssize_t n = pwrite(fd, data, (size_t)len, (off_t)offset);
        yona_rt_rc_dec(bytes);
        return io_register_direct_result((void*)(intptr_t)(n >= 0 ? n : -1));
    }
    io_ctx_put(id, ctx);
    return (int64_t)id;
}

/* ===== Synchronous fallbacks ===== */

char* yona_platform_read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { char* r = (char*)yona_rt_rc_alloc_string(1); r[0] = '\0'; return r; }
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size < 0) size = 0;
    char* buf = (char*)yona_rt_rc_alloc_string((size_t)size + 1);
    size_t rd = fread(buf, 1, (size_t)size, f);
    buf[rd] = '\0'; fclose(f); return buf;
}

int yona_platform_write_file(const char* path, const char* content) {
    FILE* f = fopen(path, "wb"); if (!f) return -1;
    size_t len = strlen(content);
    size_t w = fwrite(content, 1, len, f); fclose(f);
    return (w == len) ? 0 : -1;
}

int yona_platform_append_file(const char* path, const char* content) {
    FILE* f = fopen(path, "ab"); if (!f) return -1;
    size_t len = strlen(content);
    size_t w = fwrite(content, 1, len, f); fclose(f);
    return (w == len) ? 0 : -1;
}

int yona_platform_file_exists(const char* path) {
    struct stat st; return (stat(path, &st) == 0) ? 1 : 0;
}

int yona_platform_remove_file(const char* path) { return (remove(path) == 0) ? 0 : -1; }

int64_t yona_platform_file_size(const char* path) {
    struct stat st; if (stat(path, &st) != 0) return -1; return (int64_t)st.st_size;
}

int64_t yona_platform_open_file_handle(const char* path, int64_t mode_tag) {
    int flags = O_RDONLY;
    if (mode_tag == 1) flags = O_WRONLY | O_CREAT | O_TRUNC;       /* Write */
    else if (mode_tag == 2) flags = O_RDWR | O_CREAT;              /* ReadWrite */
    else if (mode_tag == 3) flags = O_WRONLY | O_CREAT | O_APPEND; /* Append */
    return (int64_t)open(path, flags, 0644);
}

int64_t yona_platform_close_file_handle(int fd) {
    return (int64_t)close(fd);
}

int64_t yona_platform_seek_file_handle(int fd, int64_t offset, int64_t whence_tag) {
    int whence = SEEK_SET;
    if (whence_tag == 1) whence = SEEK_CUR;
    else if (whence_tag == 2) whence = SEEK_END;
    return (int64_t)lseek(fd, (off_t)offset, whence);
}

int64_t yona_platform_tell_file_handle(int fd) {
    return (int64_t)lseek(fd, 0, SEEK_CUR);
}

int64_t yona_platform_advance_file_handle(int fd, int64_t delta) {
    return (int64_t)lseek(fd, (off_t)delta, SEEK_CUR);
}

int64_t yona_platform_flush_file_handle(int fd) {
    return fsync(fd) == 0 ? 1 : 0;
}

int64_t yona_platform_truncate_file_handle(int fd, int64_t length) {
    return ftruncate(fd, (off_t)length) == 0 ? 1 : 0;
}

int64_t* yona_platform_list_dir(const char* path) {
    DIR* dir = opendir(path);
    if (!dir) return yona_rt_seq_alloc(0);
    int64_t count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) continue;
        count++;
    }
    rewinddir(dir);
    int64_t* seq = yona_rt_seq_alloc(count);
    int64_t i = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) continue;
        size_t len = strlen(entry->d_name);
        char* name = (char*)yona_rt_rc_alloc_string(len + 1);
        memcpy(name, entry->d_name, len + 1);
        seq[1 + i] = (int64_t)(intptr_t)name;
        i++;
    }
    closedir(dir); return seq;
}

/* ===== Streaming File Line Iterator ===== */
/* Reads a file line-by-line with 64KB buffered I/O.
 * Returns an Iterator (closure that yields Option String).
 * Memory: O(64KB buffer + one line) instead of O(file_size). */

#define LINE_ITER_BUF_SIZE 65536

/* Forward declarations for runtime functions used by the iterator */
extern void* rc_alloc(int64_t type_tag, size_t payload_bytes);

typedef struct {
    int fd;
    char* buf;
    size_t buf_pos;
    size_t buf_len;
    int eof;
} line_iter_state_t;

/* Read the next line from the buffered file iterator.
 * Returns an Option: Some(line_string) or None (as heap ADT).
 * Option layout: [rc, tag_encoded, tag_i64, value_i64] where tag=0 is Some, tag=1 is None. */
static int64_t line_iter_next(int64_t* closure_env) {
    /* closure_env layout: [fn_ptr, ret_tag, arity, num_caps, heap_mask, cap0]
     * cap0 = pointer to line_iter_state_t */
    line_iter_state_t* st = (line_iter_state_t*)(intptr_t)closure_env[5];
    extern void* yona_rt_rc_alloc_string_len(size_t bytes, size_t str_len);

    if (st->eof) {
        /* Return None: ADT layout [tag, num_fields, heap_mask] */
        int64_t* adt = (int64_t*)rc_alloc(4 /* RC_TYPE_ADT */, 3 * sizeof(int64_t));
        adt[0] = 1;  /* tag = None */
        adt[1] = 0;  /* num_fields = 0 */
        adt[2] = 0;  /* heap_mask = 0 */
        return (int64_t)(intptr_t)adt;
    }

    /* Scan buffer for newline, refill if needed */
    char line_buf[8192];
    size_t line_len = 0;

    while (1) {
        /* Refill buffer if empty */
        if (st->buf_pos >= st->buf_len) {
            ssize_t n = read(st->fd, st->buf, LINE_ITER_BUF_SIZE);
            if (n <= 0) {
                st->eof = 1;
                if (line_len == 0) {
                    /* EOF with no partial line — return None */
                    int64_t* adt = (int64_t*)rc_alloc(4, 3 * sizeof(int64_t));
                    adt[0] = 1; adt[1] = 0; adt[2] = 0;
                    return (int64_t)(intptr_t)adt;
                }
                break;  /* Return the partial line */
            }
            st->buf_len = (size_t)n;
            st->buf_pos = 0;
        }

        /* Scan for newline in current buffer */
        while (st->buf_pos < st->buf_len) {
            char c = st->buf[st->buf_pos++];
            if (c == '\n') goto line_complete;
            if (line_len < sizeof(line_buf) - 1)
                line_buf[line_len++] = c;
        }
    }

line_complete:
    line_buf[line_len] = '\0';

    /* Allocate RC string for the line */
    char* line_str = (char*)yona_rt_rc_alloc_string_len(line_len + 1, line_len);
    memcpy(line_str, line_buf, line_len + 1);

    /* Return Some(line_str) — ADT layout [tag, num_fields, heap_mask, field0] */
    int64_t* adt = (int64_t*)rc_alloc(4 /* RC_TYPE_ADT */, 4 * sizeof(int64_t));
    adt[0] = 0;  /* tag = Some */
    adt[1] = 1;  /* num_fields = 1 */
    adt[2] = 1;  /* heap_mask = bit 0 (field 0 is heap-allocated string) */
    adt[3] = (int64_t)(intptr_t)line_str;
    return (int64_t)(intptr_t)adt;
}

/* Create a streaming line iterator for a file.
 * Returns an Iterator ADT wrapping the next-line closure. */
int64_t yona_rt_file_line_iterator(const char* path) {
    int fd = open(path, O_RDONLY);

    /* Allocate state */
    line_iter_state_t* st = (line_iter_state_t*)malloc(sizeof(line_iter_state_t));
    if (fd < 0) {
        /* File not found — empty iterator */
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
    /* Create a closure that captures the state.
     * Closure layout: [fn_ptr, ret_tag, arity, num_caps, heap_mask, cap0, ...] */
    extern void* yona_rt_closure_create(void* fn_ptr, int64_t ret_tag,
                                        int64_t arity, int64_t num_caps);
    int64_t* closure = (int64_t*)yona_rt_closure_create(
        (void*)line_iter_next,
        0,   /* ret_tag */
        0,   /* arity: 0 explicit args — () -> Option a */
        1    /* num_caps: 1 captured value (the state pointer) */
    );
    /* Set cap0 = state pointer */
    extern void yona_rt_closure_set_cap(void* closure, int64_t idx, int64_t val);
    yona_rt_closure_set_cap(closure, 0, (int64_t)(intptr_t)st);

    /* Wrap in Iterator ADT: [tag=0, num_fields=1, heap_mask=1, closure_ptr] */
    int64_t* iter_adt = (int64_t*)rc_alloc(4 /* RC_TYPE_ADT */, 4 * sizeof(int64_t));
    iter_adt[0] = 0;  /* tag = Iterator */
    iter_adt[1] = 1;  /* num_fields */
    iter_adt[2] = 0;  /* heap_mask=0: closure managed separately */
    iter_adt[3] = (int64_t)(intptr_t)closure;

    return (int64_t)(intptr_t)iter_adt;
}

/* ===== Binary Chunk Iterator ===== */
/* Reads fixed-size chunks from an open fd using pread. */

typedef struct {
    int fd;
    int64_t position;
    int64_t chunk_size;
    int eof;
} chunk_iter_state_t;

static int64_t chunk_iter_next(int64_t* env) {
    chunk_iter_state_t* st = (chunk_iter_state_t*)(intptr_t)env[5];
    if (st->eof) {
        int64_t* none = (int64_t*)rc_alloc(4, 3 * sizeof(int64_t));
        none[0] = 1; none[1] = 0; none[2] = 0;
        return (int64_t)(intptr_t)none;
    }

    /* Submit non-blocking pread via io_uring */
    extern void* rc_alloc(int64_t type_tag, size_t payload_bytes);
    int64_t* buf = (int64_t*)rc_alloc(8 /* RC_TYPE_BYTE_ARRAY */,
                                       sizeof(int64_t) + (size_t)st->chunk_size);
    buf[0] = 0;

    struct io_uring_sqe sqe;
    memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_READ;
    sqe.fd = st->fd;
    sqe.addr = (unsigned long)(uint8_t*)(buf + 1);
    sqe.len = (unsigned)st->chunk_size;
    sqe.off = (uint64_t)st->position;

    uint64_t id = ring_submit_sqe(&sqe);
    ssize_t n;
    if (id == 0) {
        /* Fallback: blocking pread */
        n = pread(st->fd, (uint8_t*)(buf + 1), (size_t)st->chunk_size,
                  (off_t)st->position);
    } else {
        n = (ssize_t)ring_await(id);
    }

    if (n <= 0) {
        st->eof = 1;
        yona_rt_rc_dec(buf);
        int64_t* none = (int64_t*)rc_alloc(4, 3 * sizeof(int64_t));
        none[0] = 1; none[1] = 0; none[2] = 0;
        return (int64_t)(intptr_t)none;
    }
    buf[0] = n;
    st->position += n;

    /* Some(bytes) */
    int64_t* some = (int64_t*)rc_alloc(4, 4 * sizeof(int64_t));
    some[0] = 0; some[1] = 1; some[2] = 1; /* tag=Some, 1 field, heap_mask=1 */
    some[3] = (int64_t)(intptr_t)buf;
    return (int64_t)(intptr_t)some;
}

/* readChunks: create a streaming binary chunk iterator for an open fd.
 * Does NOT close the fd — caller owns the handle. */
int64_t yona_rt_file_chunk_iterator(int64_t fd, int64_t chunk_size) {
    chunk_iter_state_t* st = (chunk_iter_state_t*)malloc(sizeof(chunk_iter_state_t));
    st->fd = (int)fd;
    st->position = 0;
    st->chunk_size = chunk_size;
    st->eof = 0;

    extern void* yona_rt_closure_create(void* fn_ptr, int64_t ret_tag,
                                        int64_t arity, int64_t num_caps);
    int64_t* closure = (int64_t*)yona_rt_closure_create(
        (void*)chunk_iter_next, 0, 0, 1);
    extern void yona_rt_closure_set_cap(void* closure, int64_t idx, int64_t val);
    yona_rt_closure_set_cap(closure, 0, (int64_t)(intptr_t)st);

    int64_t* iter_adt = (int64_t*)rc_alloc(4, 4 * sizeof(int64_t));
    iter_adt[0] = 0;
    iter_adt[1] = 1;
    iter_adt[2] = 0;
    iter_adt[3] = (int64_t)(intptr_t)closure;
    return (int64_t)(intptr_t)iter_adt;
}

/* ===== Std\IO support ===== */

/* Submit a write of one or two string chunks to an open fd. The write
 * goes to the file's current position (no pwrite — writes to TTYs or
 * pipes don't support offsets). We concatenate chunks into one buffer
 * so a single IORING_OP_WRITE covers it; the buffer is free'd by the
 * completer via IO_OP_WRITE_FD_STR. Returns uring user_data ID.
 *
 * Falls back to blocking write() when io_uring is unavailable. */
static int64_t write_fd_strs_submit_impl(int fd, const char* s1, const char* s2) {
    size_t l1 = s1 ? strlen(s1) : 0;
    size_t l2 = s2 ? strlen(s2) : 0;
    size_t total = l1 + l2;
    char* buf = (char*)malloc(total + 1);
    if (l1) memcpy(buf, s1, l1);
    if (l2) memcpy(buf + l1, s2, l2);
    buf[total] = '\0';

    io_context_t* ctx = (io_context_t*)malloc(sizeof(io_context_t));
    ctx->type = IO_OP_WRITE_FD_STR;
    ctx->fd = fd;
    ctx->buf = buf;
    ctx->buf_size = total;
    ctx->close_fd = 0;

    struct io_uring_sqe sqe;
    memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_WRITE;
    sqe.fd = fd;
    sqe.addr = (unsigned long)buf;
    sqe.len = (unsigned)total;
    sqe.off = (uint64_t)-1;  /* -1 = use fd's current position (no pwrite) */

    uint64_t id = ring_submit_sqe(&sqe);
    if (id == 0) {
        /* Fallback: blocking write */
        ssize_t n = write(fd, buf, total);
        free(buf);
        free(ctx);
        return io_register_direct_result((void*)(intptr_t)(n >= 0 ? n : -1));
    }
    io_ctx_put(id, ctx);
    return (int64_t)id;
}

int64_t yona_platform_write_fd_str_submit(int fd, const char* s) {
    return write_fd_strs_submit_impl(fd, s, NULL);
}

int64_t yona_platform_write_fd_strs_submit(int fd, const char* s1, const char* s2) {
    return write_fd_strs_submit_impl(fd, s1, s2);
}

/* Blocking line read. Called from the thread pool (AFN) so the calling
 * task isn't stalled. Returns a heap-allocated string (including the
 * trailing '\n' stripped) on success, NULL on EOF or error. */
extern void* yona_rt_rc_alloc_string(size_t bytes);

const char* yona_platform_read_line_fd(int fd) {
    /* Read up to 8 KB looking for '\n'. For stdin/TTY this is usually
     * one syscall; for pipes it may take several short reads. We grow
     * the buffer when needed. */
    size_t cap = 512;
    size_t len = 0;
    char* buf = (char*)malloc(cap);
    for (;;) {
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (char*)realloc(buf, cap);
        }
        char ch;
        ssize_t n = read(fd, &ch, 1);
        if (n <= 0) {
            if (len == 0) { free(buf); return NULL; }  /* EOF with no data */
            break;
        }
        if (ch == '\n') break;
        if (ch == '\r') continue;  /* swallow CR — CRLF and stray CR */
        buf[len++] = ch;
    }
    char* out = (char*)yona_rt_rc_alloc_string(len + 1);
    memcpy(out, buf, len);
    out[len] = '\0';
    free(buf);
    return out;
}
