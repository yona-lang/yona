/*
 * Windows networking — overlapped Winsock + IOCP submit/await integration.
 */

#ifndef _WIN32
#error "net_windows.c is for Windows builds only"
#endif

#include "yona/runtime/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>

extern void* yona_rt_rc_alloc_string(size_t bytes);
extern void yona_rt_rc_inc(void* ptr);
extern void yona_rt_rc_dec(void* ptr);
extern void* rc_alloc(int64_t type_tag, size_t payload_bytes);
extern int64_t yona_rt_io_await(int64_t uring_id);
extern int64_t yona_io_register_direct_result(void* result);
extern int64_t yona_win_register_io_ctx(void* ctx);
extern const char* yona_Std_Http__buildRequest(const char* method, const char* host,
					       const char* path, const char* body);
extern int64_t* yona_Std_Http__parseUrl(const char* url);

#define RC_TYPE_STRING 6
#define RC_TYPE_BYTE_ARRAY 8

static INIT_ONCE wsa_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK wsa_init_cb(PINIT_ONCE o, PVOID p, PVOID* c) {
	WSADATA w;
	(void)o;
	(void)p;
	(void)c;
	return WSAStartup(MAKEWORD(2, 2), &w) == 0;
}

static void net_ensure_wsa(void) {
	InitOnceExecuteOnce(&wsa_once, wsa_init_cb, NULL, NULL);
}

static SOCKET sock_from_i64(int64_t fd) { return (SOCKET)(intptr_t)fd; }

/* Must match file_windows.c io_context_t layout. */
typedef struct {
	int type;
	int fd;
	char* buf;
	size_t buf_size;
	int close_fd;
} io_context_t;

#define IO_OP_DIRECT_RESULT 99

typedef enum {
	NET_IO_SEND_STR = 1,
	NET_IO_RECV_STR = 2,
	NET_IO_SEND_BYTES = 3,
	NET_IO_RECV_BYTES = 4,
	NET_IO_CONNECT = 7,
	NET_IO_ACCEPT = 8,
} net_iocp_kind_t;

typedef struct {
	OVERLAPPED ov;
	HANDLE hDone;
	SOCKET s;
	WSABUF wbuf;
	struct sockaddr_in addr;
	char accept_addr_buf[(sizeof(struct sockaddr_in) + 16) * 2];
	int addr_len;
	io_context_t* ctx;
	net_iocp_kind_t kind;
	void* pinned_obj;
	void* io_buf;
} yona_win_net_op_t;

static HANDLE net_iocp_port;
static HANDLE net_iocp_thread;
static INIT_ONCE net_iocp_once = INIT_ONCE_STATIC_INIT;
static LPFN_CONNECTEX pConnectEx = NULL;
static LPFN_ACCEPTEX pAcceptEx = NULL;

static DWORD WINAPI yona_net_iocp_worker(void* unused) {
	(void)unused;
	for (;;) {
		DWORD nbytes = 0;
		ULONG_PTR key = 0;
		LPOVERLAPPED ov = NULL;
		BOOL ok = GetQueuedCompletionStatus(net_iocp_port, &nbytes, &key, &ov, INFINITE);
		(void)key;
		if (!ov) continue;
		yona_win_net_op_t* op =
			(yona_win_net_op_t*)((char*)ov - offsetof(yona_win_net_op_t, ov));
		if (!op || !op->ctx) continue;

		if (op->kind == NET_IO_SEND_STR || op->kind == NET_IO_SEND_BYTES) {
			if (op->pinned_obj) {
				yona_rt_rc_dec(op->pinned_obj);
				op->pinned_obj = NULL;
			}
			op->ctx->buf = (char*)(intptr_t)(ok ? (int64_t)nbytes : -1);
		} else if (op->kind == NET_IO_RECV_STR) {
			char* buf = (char*)op->io_buf;
			if (buf) {
				if (ok && nbytes > 0 && nbytes < op->wbuf.len) buf[nbytes] = '\0';
				else buf[0] = '\0';
			}
			op->ctx->buf = (char*)(intptr_t)buf;
		} else if (op->kind == NET_IO_RECV_BYTES) {
			int64_t* b = (int64_t*)op->io_buf;
			if (b) b[0] = (ok && nbytes > 0) ? (int64_t)nbytes : 0;
			op->ctx->buf = (char*)(intptr_t)b;
		} else if (op->kind == NET_IO_CONNECT) {
			if (ok) {
				setsockopt(op->s, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
				op->ctx->buf = (char*)(intptr_t)op->s;
			} else {
				closesocket(op->s);
				op->ctx->buf = (char*)(intptr_t)0;
			}
		} else if (op->kind == NET_IO_ACCEPT) {
			SOCKET accepted = (SOCKET)(uintptr_t)op->io_buf;
			if (ok) {
				setsockopt(accepted, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
					   (const char*)&op->s, sizeof(op->s));
				op->ctx->buf = (char*)(intptr_t)accepted;
			} else {
				closesocket(accepted);
				op->ctx->buf = (char*)(intptr_t)0;
			}
		} else {
			op->ctx->buf = (char*)(intptr_t)-1;
		}
		op->ctx->type = IO_OP_DIRECT_RESULT;
		SetEvent(op->hDone);
		free(op);
	}
}

static BOOL CALLBACK net_iocp_init_cb(PINIT_ONCE o, PVOID p, PVOID* c) {
	(void)o;
	(void)p;
	(void)c;
	net_iocp_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	if (!net_iocp_port) return TRUE;
	net_iocp_thread = CreateThread(NULL, 0, yona_net_iocp_worker, NULL, 0, NULL);
	if (!net_iocp_thread) {
		CloseHandle(net_iocp_port);
		net_iocp_port = NULL;
	}
	/* Resolve Winsock extension entry points once (ConnectEx/AcceptEx). */
	SOCKET probe = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (probe != INVALID_SOCKET) {
		DWORD bytes = 0;
		GUID guidConnectEx = WSAID_CONNECTEX;
		GUID guidAcceptEx = WSAID_ACCEPTEX;
		(void)WSAIoctl(probe, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidConnectEx,
			       sizeof(guidConnectEx), &pConnectEx, sizeof(pConnectEx), &bytes, NULL, NULL);
		(void)WSAIoctl(probe, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidAcceptEx,
			       sizeof(guidAcceptEx), &pAcceptEx, sizeof(pAcceptEx), &bytes, NULL, NULL);
		closesocket(probe);
	}
	return TRUE;
}

static void net_iocp_ensure(void) {
	InitOnceExecuteOnce(&net_iocp_once, net_iocp_init_cb, NULL, NULL);
}

static int64_t net_submit_iocp(SOCKET s, net_iocp_kind_t kind,
			       void* wire_buf, size_t len,
			       void* pinned_obj, void* io_owner) {
	net_iocp_ensure();
	if (!net_iocp_port) {
		if (pinned_obj) yona_rt_rc_dec(pinned_obj);
		return yona_io_register_direct_result((void*)(intptr_t)-1);
	}
	if (!CreateIoCompletionPort((HANDLE)s, net_iocp_port, 0, 0)) {
		if (pinned_obj) yona_rt_rc_dec(pinned_obj);
		return yona_io_register_direct_result((void*)(intptr_t)-1);
	}

	io_context_t* ctx = (io_context_t*)calloc(1, sizeof(io_context_t));
	yona_win_net_op_t* op = (yona_win_net_op_t*)calloc(1, sizeof(yona_win_net_op_t));
	if (!ctx || !op) {
		if (ctx) free(ctx);
		if (op) free(op);
		if (pinned_obj) yona_rt_rc_dec(pinned_obj);
		return yona_io_register_direct_result((void*)(intptr_t)-1);
	}

	op->hDone = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!op->hDone) {
		if (pinned_obj) yona_rt_rc_dec(pinned_obj);
		free(op);
		free(ctx);
		return yona_io_register_direct_result((void*)(intptr_t)-1);
	}
	op->s = s;
	op->ctx = ctx;
	op->kind = kind;
	op->pinned_obj = pinned_obj;
	op->io_buf = io_owner ? io_owner : wire_buf;
	op->wbuf.buf = (CHAR*)wire_buf;
	op->wbuf.len = (ULONG)len;
	op->addr_len = sizeof(op->addr);

	ctx->type = 0; /* pending */
	ctx->fd = -1;
	ctx->buf = NULL;
	ctx->buf_size = (size_t)(uintptr_t)op->hDone;
	ctx->close_fd = 7; /* file_windows.c treats this as wait-handle pending */
	int64_t id = yona_win_register_io_ctx(ctx);
	if (id <= 0) {
		if (pinned_obj) yona_rt_rc_dec(pinned_obj);
		if (op->hDone) CloseHandle(op->hDone);
		free(op);
		free(ctx);
		return yona_io_register_direct_result((void*)(intptr_t)-1);
	}

	DWORD flags = 0, sent = 0;
	int rc;
	if (kind == NET_IO_SEND_STR || kind == NET_IO_SEND_BYTES)
		rc = WSASend(s, &op->wbuf, 1, &sent, 0, &op->ov, NULL);
	else
		rc = WSARecv(s, &op->wbuf, 1, &sent, &flags, &op->ov, NULL);
	if (rc == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
		if (pinned_obj) {
			yona_rt_rc_dec(pinned_obj);
			op->pinned_obj = NULL;
		}
		ctx->buf = (char*)(intptr_t)-1;
		ctx->type = IO_OP_DIRECT_RESULT;
		SetEvent(op->hDone);
		free(op);
	}
	return id;
}

static int64_t net_submit_connect_iocp(const char* host, int64_t port) {
	net_iocp_ensure();
	if (!net_iocp_port || !pConnectEx || !host)
		return yona_io_register_direct_result((void*)(intptr_t)0);

	struct addrinfo hints, *res = NULL;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	char port_str[16];
	snprintf(port_str, sizeof(port_str), "%lld", (long long)port);
	if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res)
		return yona_io_register_direct_result((void*)(intptr_t)0);

	SOCKET s = WSASocket(res->ai_family, res->ai_socktype, res->ai_protocol, NULL, 0,
			     WSA_FLAG_OVERLAPPED);
	if (s == INVALID_SOCKET) {
		freeaddrinfo(res);
		return yona_io_register_direct_result((void*)(intptr_t)0);
	}
	if (!CreateIoCompletionPort((HANDLE)s, net_iocp_port, 0, 0)) {
		closesocket(s);
		freeaddrinfo(res);
		return yona_io_register_direct_result((void*)(intptr_t)0);
	}

	struct sockaddr_in local;
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_port = 0;
	if (bind(s, (SOCKADDR*)&local, sizeof(local)) == SOCKET_ERROR) {
		closesocket(s);
		freeaddrinfo(res);
		return yona_io_register_direct_result((void*)(intptr_t)0);
	}

	io_context_t* ctx = (io_context_t*)calloc(1, sizeof(io_context_t));
	yona_win_net_op_t* op = (yona_win_net_op_t*)calloc(1, sizeof(yona_win_net_op_t));
	if (!ctx || !op) {
		if (ctx) free(ctx);
		if (op) free(op);
		closesocket(s);
		freeaddrinfo(res);
		return yona_io_register_direct_result((void*)(intptr_t)0);
	}
	op->hDone = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!op->hDone) {
		free(op);
		free(ctx);
		closesocket(s);
		freeaddrinfo(res);
		return yona_io_register_direct_result((void*)(intptr_t)0);
	}
	op->s = s;
	op->ctx = ctx;
	op->kind = NET_IO_CONNECT;
	ctx->type = 0;
	ctx->fd = -1;
	ctx->buf = NULL;
	ctx->buf_size = (size_t)(uintptr_t)op->hDone;
	ctx->close_fd = 7;
	int64_t id = yona_win_register_io_ctx(ctx);
	if (id <= 0) {
		if (op->hDone) CloseHandle(op->hDone);
		free(op);
		free(ctx);
		closesocket(s);
		freeaddrinfo(res);
		return yona_io_register_direct_result((void*)(intptr_t)0);
	}
	BOOL ok = pConnectEx(s, res->ai_addr, (int)res->ai_addrlen, NULL, 0, NULL, &op->ov);
	freeaddrinfo(res);
	if (!ok && WSAGetLastError() != ERROR_IO_PENDING) {
		ctx->buf = (char*)(intptr_t)0;
		ctx->type = IO_OP_DIRECT_RESULT;
		SetEvent(op->hDone);
		free(op);
	}
	return id;
}

static int64_t net_submit_accept_iocp(SOCKET listener) {
	net_iocp_ensure();
	if (!net_iocp_port || !pAcceptEx || listener == INVALID_SOCKET)
		return yona_io_register_direct_result((void*)(intptr_t)0);
	SOCKET accepted = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (accepted == INVALID_SOCKET)
		return yona_io_register_direct_result((void*)(intptr_t)0);
	if (!CreateIoCompletionPort((HANDLE)listener, net_iocp_port, 0, 0) ||
	    !CreateIoCompletionPort((HANDLE)accepted, net_iocp_port, 0, 0)) {
		closesocket(accepted);
		return yona_io_register_direct_result((void*)(intptr_t)0);
	}

	io_context_t* ctx = (io_context_t*)calloc(1, sizeof(io_context_t));
	yona_win_net_op_t* op = (yona_win_net_op_t*)calloc(1, sizeof(yona_win_net_op_t));
	if (!ctx || !op) {
		if (ctx) free(ctx);
		if (op) free(op);
		closesocket(accepted);
		return yona_io_register_direct_result((void*)(intptr_t)0);
	}
	op->hDone = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!op->hDone) {
		free(op);
		free(ctx);
		closesocket(accepted);
		return yona_io_register_direct_result((void*)(intptr_t)0);
	}
	op->s = listener;
	op->ctx = ctx;
	op->kind = NET_IO_ACCEPT;
	op->io_buf = (void*)(uintptr_t)accepted;
	ctx->type = 0;
	ctx->fd = -1;
	ctx->buf = NULL;
	ctx->buf_size = (size_t)(uintptr_t)op->hDone;
	ctx->close_fd = 7;
	int64_t id = yona_win_register_io_ctx(ctx);
	if (id <= 0) {
		if (op->hDone) CloseHandle(op->hDone);
		free(op);
		free(ctx);
		closesocket(accepted);
		return yona_io_register_direct_result((void*)(intptr_t)0);
	}
	DWORD bytes = 0;
	BOOL ok = pAcceptEx(listener, accepted, op->accept_addr_buf, 0,
			    sizeof(struct sockaddr_in) + 16,
			    sizeof(struct sockaddr_in) + 16, &bytes, &op->ov);
	if (!ok && WSAGetLastError() != ERROR_IO_PENDING) {
		ctx->buf = (char*)(intptr_t)0;
		ctx->type = IO_OP_DIRECT_RESULT;
		SetEvent(op->hDone);
		free(op);
	}
	return id;
}

int64_t yona_Std_Net__tcpConnect(const char* host, int64_t port) {
	net_ensure_wsa();
	return net_submit_connect_iocp(host, port);
}

int64_t yona_Std_Net__tcpListen(const char* host, int64_t port) {
	net_ensure_wsa();
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	if (host && host[0] != '\0') {
		if (InetPtonA(AF_INET, host, &addr.sin_addr) != 1) return -1;
	} else {
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
	}
	SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd == INVALID_SOCKET) return -1;
	BOOL opt = TRUE;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
	if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
		closesocket(fd);
		return -1;
	}
	if (listen(fd, SOMAXCONN) != 0) {
		closesocket(fd);
		return -1;
	}
	return (int64_t)(intptr_t)fd;
}

int64_t yona_Std_Net__tcpAccept(int64_t listener_fd) {
	net_ensure_wsa();
	return net_submit_accept_iocp(sock_from_i64(listener_fd));
}

int64_t yona_Std_Net__send(int64_t fd, const char* data) {
	net_ensure_wsa();
	if (!data) data = "";
	size_t len = strlen(data);
	char* pinned = (char*)yona_rt_rc_alloc_string(len + 1);
	memcpy(pinned, data, len + 1);
	return net_submit_iocp(sock_from_i64(fd), NET_IO_SEND_STR, pinned, len, pinned, pinned);
}

int64_t yona_Std_Net__recv(int64_t fd, int64_t max_bytes) {
	net_ensure_wsa();
	if (max_bytes <= 0) max_bytes = 4096;
	char* buf = (char*)yona_rt_rc_alloc_string((size_t)max_bytes + 1);
	return net_submit_iocp(sock_from_i64(fd), NET_IO_RECV_STR, buf, (size_t)max_bytes, NULL, buf);
}

int64_t yona_Std_Net__sendBytes(int64_t fd, void* bytes) {
	net_ensure_wsa();
	if (!bytes) return yona_io_register_direct_result((void*)(intptr_t)-1);
	int64_t* b = (int64_t*)bytes;
	int64_t len = b[0] < 0 ? 0 : b[0];
	yona_rt_rc_inc(bytes);
	uint8_t* data = (uint8_t*)(b + 1);
	return net_submit_iocp(sock_from_i64(fd), NET_IO_SEND_BYTES, data, (size_t)len, bytes, bytes);
}

int64_t yona_Std_Net__recvBytes(int64_t fd, int64_t max_bytes) {
	net_ensure_wsa();
	if (max_bytes <= 0) max_bytes = 4096;
	int64_t* buf = (int64_t*)rc_alloc(RC_TYPE_BYTE_ARRAY, sizeof(int64_t) + (size_t)max_bytes);
	buf[0] = 0;
	return net_submit_iocp(sock_from_i64(fd), NET_IO_RECV_BYTES, (void*)(buf + 1), (size_t)max_bytes, NULL, buf);
}

int64_t yona_Std_Net__close(int64_t fd) {
	net_ensure_wsa();
	closesocket(sock_from_i64(fd));
	return 0;
}

int64_t yona_Std_Http__httpGet(const char* url) {
	net_ensure_wsa();
	if (!url) return yona_io_register_direct_result((void*)(intptr_t)0);
	int64_t* parsed = yona_Std_Http__parseUrl(url);
	const char* host = (const char*)(intptr_t)parsed[1];
	int64_t port = parsed[2];
	const char* path = (const char*)(intptr_t)parsed[3];
	if (!host || !path) return yona_io_register_direct_result((void*)(intptr_t)0);

	int64_t connect_id = yona_Std_Net__tcpConnect(host, port);
	SOCKET sock = (SOCKET)(intptr_t)yona_rt_io_await(connect_id);
	if (sock == INVALID_SOCKET || sock == 0) {
		return yona_io_register_direct_result((void*)(intptr_t)0);
	}

	const char* req = yona_Std_Http__buildRequest("GET", host, path, NULL);
	int64_t send_id = yona_Std_Net__send((int64_t)(intptr_t)sock, req);
	int64_t sent = yona_rt_io_await(send_id);
	if (sent <= 0) {
		closesocket(sock);
		return yona_io_register_direct_result((void*)(intptr_t)0);
	}

	size_t buf_size = 16384;
	size_t total = 0;
	char* buf = (char*)rc_alloc(RC_TYPE_STRING, buf_size);
	for (;;) {
		int64_t recv_id = yona_Std_Net__recv((int64_t)(intptr_t)sock, 4096);
		char* chunk = (char*)(intptr_t)yona_rt_io_await(recv_id);
		if (!chunk || chunk[0] == '\0') {
			if (chunk) yona_rt_rc_dec(chunk);
			break;
		}
		size_t n = strlen(chunk);
		if (n == 0) {
			yona_rt_rc_dec(chunk);
			break;
		}
		if (total + n + 1 > buf_size) {
			size_t new_size = buf_size;
			while (total + n + 1 > new_size) new_size *= 2;
			char* new_buf = (char*)rc_alloc(RC_TYPE_STRING, new_size);
			memcpy(new_buf, buf, total);
			buf = new_buf;
			buf_size = new_size;
		}
		memcpy(buf + total, chunk, n);
		total += n;
		yona_rt_rc_dec(chunk);
	}
	buf[total] = '\0';
	closesocket(sock);
	return yona_io_register_direct_result((void*)(intptr_t)buf);
}

int64_t yona_Std_Net__udpBind(const char* host, int64_t port) {
	net_ensure_wsa();
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	if (host && host[0] != '\0') {
		if (InetPtonA(AF_INET, host, &addr.sin_addr) != 1) return -1;
	} else {
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
	}
	SOCKET fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd == INVALID_SOCKET) return -1;
	if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
		closesocket(fd);
		return -1;
	}
	return (int64_t)(intptr_t)fd;
}

/* UDP is FN (sync) in Net.yonai — same contract as Linux/macOS. Returning
 * an IOCP cookie here made the C test (and Yona callers) treat a completion
 * id as a string pointer and SIGSEGV. */
int64_t yona_Std_Net__udpSendTo(int64_t fd, const char* host, int64_t port, const char* data) {
	net_ensure_wsa();
	if (!host) return -1;
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	if (InetPtonA(AF_INET, host, &addr.sin_addr) != 1) return -1;
	const char* payload = data ? data : "";
	int n = sendto(sock_from_i64(fd), payload, (int)strlen(payload), 0,
		       (struct sockaddr*)&addr, sizeof(addr));
	return (int64_t)n;
}

int64_t yona_Std_Net__udpRecv(int64_t fd, int64_t max_bytes) {
	net_ensure_wsa();
	if (max_bytes <= 0) max_bytes = 4096;
	char* buf = (char*)yona_rt_rc_alloc_string((size_t)max_bytes + 1);
	struct sockaddr_in from;
	int from_len = sizeof(from);
	int n = recvfrom(sock_from_i64(fd), buf, (int)max_bytes, 0,
			 (struct sockaddr*)&from, &from_len);
	if (n <= 0) {
		buf[0] = '\0';
		return (int64_t)(intptr_t)buf;
	}
	buf[n] = '\0';
	return (int64_t)(intptr_t)buf;
}

const char* yona_Std_Net__peerAddress(int64_t fd) {
	net_ensure_wsa();
	struct sockaddr_in addr;
	int len = sizeof(addr);
	SOCKET s = sock_from_i64(fd);
	if (getpeername(s, (struct sockaddr*)&addr, &len) != 0) {
		char* r = (char*)yona_rt_rc_alloc_string(8);
		memcpy(r, "unknown", 8);
		return r;
	}
	char ip[INET_ADDRSTRLEN];
	if (!InetNtopA(AF_INET, &addr.sin_addr, ip, sizeof(ip))) {
		char* r = (char*)yona_rt_rc_alloc_string(8);
		memcpy(r, "unknown", 8);
		return r;
	}
	char* r = (char*)yona_rt_rc_alloc_string(64);
	snprintf(r, 64, "%s:%u", ip, (unsigned)ntohs(addr.sin_port));
	return r;
}
