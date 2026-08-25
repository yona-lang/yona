/*
 * Linux networking — submit-and-return via io_uring.
 *
 * Socket creation/bind/listen use POSIX (fast, no I/O wait).
 * Data transfer (accept, connect, send, recv) submits to io_uring
 * and returns the uring ID immediately. Completion via yona_rt_io_await().
 */

#include "yona/runtime/platform.h"
#include "yona/runtime/uring.h"
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

extern void yona_rt_rc_inc(void* ptr);
extern void yona_rt_rc_dec(void* ptr);
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>

extern void* yona_rt_rc_alloc_string(size_t bytes);

/* ===== TCP ===== */

int64_t yona_Std_Net__tcpConnect(const char* host, int64_t port) {
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[8]; snprintf(port_str, sizeof(port_str), "%" PRId64, port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0) return 0;
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return 0; }

    struct sockaddr_storage* addr_copy = (struct sockaddr_storage*)malloc(sizeof(struct sockaddr_storage));
    memcpy(addr_copy, res->ai_addr, res->ai_addrlen);
    socklen_t addr_len = res->ai_addrlen;
    freeaddrinfo(res);

    io_context_t* ctx = (io_context_t*)malloc(sizeof(io_context_t));
    ctx->type = IO_OP_CONNECT;
    ctx->fd = fd;
    ctx->buf = (char*)addr_copy;
    ctx->buf_size = 0;
    ctx->close_fd = 0;

    struct io_uring_sqe sqe;
    memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_CONNECT;
    sqe.fd = fd;
    sqe.addr = (unsigned long)addr_copy;
    sqe.off = (uint64_t)addr_len;

    uint64_t id = ring_submit_sqe(&sqe);
    if (id == 0) { close(fd); free(addr_copy); free(ctx); return 0; }
    io_ctx_put(id, ctx);
    return (int64_t)id;
}

int64_t yona_Std_Net__tcpListen(const char* host, int64_t port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (host && host[0] != '\0') inet_pton(AF_INET, host, &addr.sin_addr);
    else addr.sin_addr.s_addr = INADDR_ANY;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    if (listen(fd, 128) < 0) { close(fd); return -1; }
    return (int64_t)fd;
}

int64_t yona_Std_Net__tcpAccept(int64_t listener_fd) {
    struct sockaddr_in* client_addr = (struct sockaddr_in*)calloc(1, sizeof(struct sockaddr_in) + sizeof(socklen_t));
    socklen_t* addr_len_ptr = (socklen_t*)((char*)client_addr + sizeof(struct sockaddr_in));
    *addr_len_ptr = sizeof(struct sockaddr_in);

    io_context_t* ctx = (io_context_t*)malloc(sizeof(io_context_t));
    ctx->type = IO_OP_ACCEPT;
    ctx->fd = (int)listener_fd;
    ctx->buf = (char*)client_addr;
    ctx->buf_size = 0;
    ctx->close_fd = 0;

    struct io_uring_sqe sqe;
    memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_ACCEPT;
    sqe.fd = (int)listener_fd;
    /* Kernel/liburing use addr = sockaddr *, off = socklen_t * (NOT addr2). */
    sqe.addr = (unsigned long)client_addr;
    sqe.off = (uint64_t)(unsigned long)addr_len_ptr;

    uint64_t id = ring_submit_sqe(&sqe);
    if (id == 0) { free(client_addr); free(ctx); return 0; }
    io_ctx_put(id, ctx);
    return (int64_t)id;
}

int64_t yona_Std_Net__send(int64_t fd, const char* data) {
    size_t len = strlen(data);

    /* Copy to RC-managed buffer so it survives until I/O completes */
    extern void* yona_rt_rc_alloc_string(size_t bytes);
    char* pinned = (char*)yona_rt_rc_alloc_string(len + 1);
    memcpy(pinned, data, len + 1);

    io_context_t* ctx = (io_context_t*)malloc(sizeof(io_context_t));
    ctx->type = IO_OP_SEND;
    ctx->fd = (int)fd;
    ctx->buf = pinned; /* rc_dec'd in completer */
    ctx->buf_size = len;
    ctx->close_fd = 0;

    struct io_uring_sqe sqe;
    memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_SEND;
    sqe.fd = (int)fd;
    sqe.addr = (unsigned long)pinned;
    sqe.len = (unsigned)len;

    uint64_t id = ring_submit_sqe(&sqe);
    if (id == 0) { yona_rt_rc_dec(pinned); free(ctx); return 0; }
    io_ctx_put(id, ctx);
    return (int64_t)id;
}

int64_t yona_Std_Net__recv(int64_t fd, int64_t max_bytes) {
    if (max_bytes <= 0) max_bytes = 4096;
    char* buf = (char*)yona_rt_rc_alloc_string((size_t)max_bytes + 1);

    io_context_t* ctx = (io_context_t*)malloc(sizeof(io_context_t));
    ctx->type = IO_OP_RECV;
    ctx->fd = (int)fd;
    ctx->buf = buf;
    ctx->buf_size = (size_t)max_bytes;
    ctx->close_fd = 0;

    struct io_uring_sqe sqe;
    memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_RECV;
    sqe.fd = (int)fd;
    sqe.addr = (unsigned long)buf;
    sqe.len = (unsigned)max_bytes;

    uint64_t id = ring_submit_sqe(&sqe);
    if (id == 0) { free(ctx); buf[0] = '\0'; return 0; }
    io_ctx_put(id, ctx);
    return (int64_t)id;
}

/* sendBytes: send Bytes buffer via io_uring */
int64_t yona_Std_Net__sendBytes(int64_t fd, void* bytes) {
    int64_t* b = (int64_t*)bytes;
    int64_t len = b[0];
    uint8_t* data = (uint8_t*)(b + 1);
    /* Pin the Bytes buffer until I/O completes */
    yona_rt_rc_inc(bytes);

    io_context_t* ctx = (io_context_t*)malloc(sizeof(io_context_t));
    ctx->type = IO_OP_SEND;
    ctx->fd = (int)fd;
    ctx->buf = (char*)bytes; /* store for rc_dec in completer */
    ctx->buf_size = (size_t)len;
    ctx->close_fd = 0;

    struct io_uring_sqe sqe;
    memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_SEND;
    sqe.fd = (int)fd;
    sqe.addr = (unsigned long)data;
    sqe.len = (unsigned)len;

    uint64_t id = ring_submit_sqe(&sqe);
    if (id == 0) { yona_rt_rc_dec(bytes); free(ctx); return 0; }
    io_ctx_put(id, ctx);
    return (int64_t)id;
}

/* recvBytes: receive into Bytes buffer via io_uring */
int64_t yona_Std_Net__recvBytes(int64_t fd, int64_t max_bytes) {
    if (max_bytes <= 0) max_bytes = 4096;
    /* Allocate Bytes buffer: [length][data...] with RC header */
    extern void* rc_alloc(int64_t type_tag, size_t payload_bytes);
    int64_t* buf = (int64_t*)rc_alloc(8 /* RC_TYPE_BYTE_ARRAY */, sizeof(int64_t) + (size_t)max_bytes);
    buf[0] = 0; /* length set by completer */

    io_context_t* ctx = (io_context_t*)malloc(sizeof(io_context_t));
    ctx->type = IO_OP_RECV_BYTES;
    ctx->fd = (int)fd;
    ctx->buf = (char*)buf;
    ctx->buf_size = (size_t)max_bytes;
    ctx->close_fd = 0;

    struct io_uring_sqe sqe;
    memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_RECV;
    sqe.fd = (int)fd;
    sqe.addr = (unsigned long)(uint8_t*)(buf + 1); /* data starts after length */
    sqe.len = (unsigned)max_bytes;

    uint64_t id = ring_submit_sqe(&sqe);
    if (id == 0) { free(ctx); return 0; }
    io_ctx_put(id, ctx);
    return (int64_t)id;
}

int64_t yona_Std_Net__close(int64_t fd) { close((int)fd); return 0; }

/* ===== HTTP GET via io_uring ===== */

extern const char* yona_Std_Http__buildRequest(const char* method, const char* host,
                                                const char* path, const char* body);
extern int64_t* yona_Std_Http__parseUrl(const char* url);
extern int64_t yona_rt_adt_get_field(void* node, int64_t index);
extern void* rc_alloc(int64_t type_tag, size_t payload_bytes);
#define RC_TYPE_STRING 6

int64_t yona_Std_Http__httpGet(const char* url) {
    int64_t* parsed = yona_Std_Http__parseUrl(url);
    const char* host = (const char*)(intptr_t)yona_rt_adt_get_field(parsed, 0);
    int64_t port = yona_rt_adt_get_field(parsed, 1);
    const char* path = (const char*)(intptr_t)yona_rt_adt_get_field(parsed, 2);

    struct addrinfo hints, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[8]; snprintf(port_str, sizeof(port_str), "%" PRId64, port);
    if (getaddrinfo(host, port_str, &hints, &ai) != 0) {
        yona_rt_rc_dec(parsed);
        return 0;
    }
    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(ai);
        yona_rt_rc_dec(parsed);
        return 0;
    }

    struct sockaddr_storage addr_buf;
    memcpy(&addr_buf, ai->ai_addr, ai->ai_addrlen);
    socklen_t addr_len = ai->ai_addrlen;
    freeaddrinfo(ai);
    {
        struct io_uring_sqe sqe; memset(&sqe, 0, sizeof(sqe));
        sqe.opcode = IORING_OP_CONNECT;
        sqe.fd = fd;
        sqe.addr = (unsigned long)&addr_buf;
        sqe.off = (uint64_t)addr_len;
        uint64_t id = ring_submit_sqe(&sqe);
        if (ring_await(id) < 0) {
            close(fd);
            yona_rt_rc_dec(parsed);
            return 0;
        }
    }

    const char* req = yona_Std_Http__buildRequest("GET", host, path, NULL);
    yona_rt_rc_dec(parsed);
    {
        struct io_uring_sqe sqe; memset(&sqe, 0, sizeof(sqe));
        sqe.opcode = IORING_OP_SEND;
        sqe.fd = fd;
        sqe.addr = (unsigned long)req;
        sqe.len = (unsigned)strlen(req);
        uint64_t id = ring_submit_sqe(&sqe);
        ring_await(id);
    }
    yona_rt_rc_dec((void*)req);

    size_t buf_size = 16384;
    size_t total = 0;
    char* buf = (char*)rc_alloc(RC_TYPE_STRING, buf_size);
    for (;;) {
        struct io_uring_sqe sqe; memset(&sqe, 0, sizeof(sqe));
        sqe.opcode = IORING_OP_RECV;
        sqe.fd = fd;
        sqe.addr = (unsigned long)(buf + total);
        sqe.len = (unsigned)(buf_size - total - 1);
        uint64_t id = ring_submit_sqe(&sqe);
        int32_t n = ring_await(id);
        if (n <= 0) break;
        total += (size_t)n;
        if (total >= buf_size - 256) {
            size_t new_size = buf_size * 2;
            char* new_buf = (char*)rc_alloc(RC_TYPE_STRING, new_size);
            memcpy(new_buf, buf, total);
            yona_rt_rc_dec(buf);
            buf = new_buf; buf_size = new_size;
        }
    }
    buf[total] = '\0';
    close(fd);

    io_context_t* ctx = (io_context_t*)malloc(sizeof(io_context_t));
    ctx->type = IO_OP_READ_FILE;
    ctx->fd = -1;
    ctx->buf = buf;
    ctx->buf_size = total;
    ctx->close_fd = 0;
    struct io_uring_sqe sqe; memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_NOP;
    uint64_t nop_id = ring_submit_sqe(&sqe);
    io_ctx_put(nop_id, ctx);
    return (int64_t)nop_id;
}

/* ===== UDP (sync — datagram ops are fast) ===== */

int64_t yona_Std_Net__udpBind(const char* host, int64_t port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (host && host[0] != '\0') inet_pton(AF_INET, host, &addr.sin_addr);
    else addr.sin_addr.s_addr = INADDR_ANY;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    return (int64_t)fd;
}

int64_t yona_Std_Net__udpSendTo(int64_t fd, const char* host, int64_t port, const char* data) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, host, &addr.sin_addr);
    return (int64_t)sendto((int)fd, data, strlen(data), 0, (struct sockaddr*)&addr, sizeof(addr));
}

int64_t yona_Std_Net__udpRecv(int64_t fd, int64_t max_bytes) {
    if (max_bytes <= 0) max_bytes = 4096;
    char* buf = (char*)yona_rt_rc_alloc_string((size_t)max_bytes + 1);
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    ssize_t n = recvfrom((int)fd, buf, (size_t)max_bytes, 0, (struct sockaddr*)&from, &from_len);
    if (n <= 0) { buf[0] = '\0'; return (int64_t)(intptr_t)buf; }
    buf[n] = '\0';
    return (int64_t)(intptr_t)buf;
}

const char* yona_Std_Net__peerAddress(int64_t fd) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getpeername((int)fd, (struct sockaddr*)&addr, &len) < 0) {
        char* r = (char*)yona_rt_rc_alloc_string(8); memcpy(r, "unknown", 8); return r;
    }
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    char* r = (char*)yona_rt_rc_alloc_string(64);
    snprintf(r, 64, "%s:%d", ip, ntohs(addr.sin_port));
    return r;
}
