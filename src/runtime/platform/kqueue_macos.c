/*
 * Single kqueue + worker pool + io_ctx table for all macOS platform TUs.
 */

#ifndef __APPLE__
#error "kqueue_macos.c is for macOS builds only"
#endif

#include "yona/runtime/kqueue.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define KQ_PENDING_MAX 256
#define KQ_WORKER_COUNT 8
#define KQ_OP_TABLE 256

enum kq_kind {
	KQ_KIND_READ,
	KQ_KIND_WRITE,
	KQ_KIND_CONNECT,
	KQ_KIND_ACCEPT,
	KQ_KIND_SEND,
	KQ_KIND_RECV,
	KQ_KIND_NOP,
};

typedef struct kq_work {
	struct kq_work* next;
	uint64_t id;
	enum kq_kind kind;
	int fd;
	void* buf;
	size_t len;
	off_t off;
	struct sockaddr_storage addr;
	socklen_t addrlen;
	socklen_t* addrlen_ptr;
	int cancelled;
} kq_work_t;

static int yona_kq = -1;
static int yona_wake_r = -1;
static int yona_wake_w = -1;
static int yona_kq_ready;
static atomic_uint_fast64_t yona_next_id;
static pthread_mutex_t yona_kq_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct {
	uint64_t id;
	int32_t res;
	int used;
} kq_pending[KQ_PENDING_MAX];

static kq_work_t* kq_ops[KQ_OP_TABLE];

static kq_work_t* work_head;
static kq_work_t* work_tail;
static pthread_mutex_t work_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t work_cond = PTHREAD_COND_INITIALIZER;
static pthread_t workers[KQ_WORKER_COUNT];
static int workers_started;

static int pending_put(uint64_t id, int32_t res) {
	for (int i = 0; i < KQ_PENDING_MAX; i++) {
		if (!kq_pending[i].used) {
			kq_pending[i].id = id;
			kq_pending[i].res = res;
			kq_pending[i].used = 1;
			return 0;
		}
	}
	return -1;
}

static int pending_take(uint64_t id, int32_t* out) {
	for (int i = 0; i < KQ_PENDING_MAX; i++) {
		if (kq_pending[i].used && kq_pending[i].id == id) {
			*out = kq_pending[i].res;
			kq_pending[i].used = 0;
			return 1;
		}
	}
	return 0;
}

static void op_store(kq_work_t* w) {
	for (int i = 0; i < KQ_OP_TABLE; i++) {
		if (!kq_ops[i]) {
			kq_ops[i] = w;
			return;
		}
	}
}

static kq_work_t* op_find(uint64_t id) {
	for (int i = 0; i < KQ_OP_TABLE; i++) {
		if (kq_ops[i] && kq_ops[i]->id == id)
			return kq_ops[i];
	}
	return NULL;
}

static kq_work_t* op_take(uint64_t id) {
	for (int i = 0; i < KQ_OP_TABLE; i++) {
		if (kq_ops[i] && kq_ops[i]->id == id) {
			kq_work_t* w = kq_ops[i];
			kq_ops[i] = NULL;
			return w;
		}
	}
	return NULL;
}

static void wake_awaiters(void) {
	char c = 1;
	(void)write(yona_wake_w, &c, 1);
}

static void complete_locked(uint64_t id, int32_t res) {
	kq_work_t* w = op_take(id);
	if (w)
		free(w);
	(void)pending_put(id, res);
	wake_awaiters();
}

static void set_nonblock(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0)
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int32_t do_file_io(kq_work_t* w) {
	ssize_t n;
	if (w->kind == KQ_KIND_READ) {
		if (w->off == (off_t)-1)
			n = read(w->fd, w->buf, w->len);
		else
			n = pread(w->fd, w->buf, w->len, w->off);
	} else {
		if (w->off == (off_t)-1)
			n = write(w->fd, w->buf, w->len);
		else
			n = pwrite(w->fd, w->buf, w->len, w->off);
	}
	if (n < 0)
		return (int32_t)-errno;
	return (int32_t)n;
}

static void queue_file_work(kq_work_t* w) {
	pthread_mutex_lock(&work_mutex);
	w->next = NULL;
	if (work_tail)
		work_tail->next = w;
	else
		work_head = w;
	work_tail = w;
	pthread_cond_signal(&work_cond);
	pthread_mutex_unlock(&work_mutex);
}

static void* kq_worker_main(void* unused) {
	(void)unused;
	for (;;) {
		pthread_mutex_lock(&work_mutex);
		while (!work_head)
			pthread_cond_wait(&work_cond, &work_mutex);
		kq_work_t* w = work_head;
		work_head = w->next;
		if (!work_head)
			work_tail = NULL;
		pthread_mutex_unlock(&work_mutex);

		pthread_mutex_lock(&yona_kq_mutex);
		int cancelled = w->cancelled;
		uint64_t id = w->id;
		pthread_mutex_unlock(&yona_kq_mutex);
		if (cancelled) {
			pthread_mutex_lock(&yona_kq_mutex);
			complete_locked(id, -ECANCELED);
			pthread_mutex_unlock(&yona_kq_mutex);
			continue;
		}

		int32_t res = do_file_io(w);
		pthread_mutex_lock(&yona_kq_mutex);
		if (w->cancelled)
			res = -ECANCELED;
		complete_locked(id, res);
		pthread_mutex_unlock(&yona_kq_mutex);
	}
	return NULL;
}

static int32_t finish_connect(kq_work_t* w) {
	int err = 0;
	socklen_t len = sizeof(err);
	if (getsockopt(w->fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0)
		return (int32_t)-errno;
	return err ? (int32_t)-err : 0;
}

static int32_t finish_accept(kq_work_t* w) {
	socklen_t* alen = w->addrlen_ptr;
	int nfd = accept(w->fd, (struct sockaddr*)w->buf, alen);
	if (nfd < 0)
		return (int32_t)-errno;
	return (int32_t)nfd;
}

static int32_t finish_send(kq_work_t* w) {
	ssize_t n = send(w->fd, w->buf, w->len, 0);
	if (n < 0)
		return (int32_t)-errno;
	return (int32_t)n;
}

static int32_t finish_recv(kq_work_t* w) {
	ssize_t n = recv(w->fd, w->buf, w->len, 0);
	if (n < 0)
		return (int32_t)-errno;
	return (int32_t)n;
}

static void handle_socket_event(int fd, int16_t filter) {
	(void)filter;
	for (int i = 0; i < KQ_OP_TABLE; i++) {
		kq_work_t* w = kq_ops[i];
		if (!w || w->fd != fd)
			continue;
		if (w->kind != KQ_KIND_CONNECT && w->kind != KQ_KIND_ACCEPT &&
		    w->kind != KQ_KIND_SEND && w->kind != KQ_KIND_RECV)
			continue;
		int32_t res;
		if (w->cancelled)
			res = -ECANCELED;
		else if (w->kind == KQ_KIND_CONNECT)
			res = finish_connect(w);
		else if (w->kind == KQ_KIND_ACCEPT)
			res = finish_accept(w);
		else if (w->kind == KQ_KIND_SEND)
			res = finish_send(w);
		else
			res = finish_recv(w);
		if ((res == -EAGAIN || res == -EWOULDBLOCK) && !w->cancelled)
			return;
		complete_locked(w->id, res);
		return;
	}
}

static void drain_wakeup(void) {
	char buf[64];
	while (read(yona_wake_r, buf, sizeof(buf)) > 0) {
	}
}

static int yona_kq_init(void) {
	if (yona_kq_ready)
		return 0;
	yona_kq = kqueue();
	if (yona_kq < 0)
		return -1;
	int fds[2];
	if (pipe(fds) < 0) {
		close(yona_kq);
		yona_kq = -1;
		return -1;
	}
	yona_wake_r = fds[0];
	yona_wake_w = fds[1];
	fcntl(yona_wake_r, F_SETFL, O_NONBLOCK);
	fcntl(yona_wake_w, F_SETFL, O_NONBLOCK);
	struct kevent ev;
	EV_SET(&ev, (uintptr_t)yona_wake_r, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, NULL);
	if (kevent(yona_kq, &ev, 1, NULL, 0, NULL) < 0) {
		close(yona_wake_r);
		close(yona_wake_w);
		close(yona_kq);
		yona_kq = -1;
		return -1;
	}
	atomic_init(&yona_next_id, 1);
	if (!workers_started) {
		for (int i = 0; i < KQ_WORKER_COUNT; i++)
			pthread_create(&workers[i], NULL, kq_worker_main, NULL);
		workers_started = 1;
	}
	yona_kq_ready = 1;
	return 0;
}

static uint64_t alloc_id(void) {
	return atomic_fetch_add(&yona_next_id, 1);
}

static int arm_socket(kq_work_t* w, int16_t filter) {
	struct kevent ev;
	EV_SET(&ev, (uintptr_t)w->fd, filter, EV_ADD | EV_ONESHOT, 0, 0, (void*)(uintptr_t)w->id);
	if (kevent(yona_kq, &ev, 1, NULL, 0, NULL) < 0)
		return -1;
	return 0;
}

static uint64_t submit_file(enum kq_kind kind, int fd, void* buf, size_t len, off_t off) {
	pthread_mutex_lock(&yona_kq_mutex);
	if (!yona_kq_ready && yona_kq_init() != 0) {
		pthread_mutex_unlock(&yona_kq_mutex);
		return 0;
	}
	kq_work_t* w = (kq_work_t*)calloc(1, sizeof(kq_work_t));
	if (!w) {
		pthread_mutex_unlock(&yona_kq_mutex);
		return 0;
	}
	w->id = alloc_id();
	w->kind = kind;
	w->fd = fd;
	w->buf = buf;
	w->len = len;
	w->off = off;
	op_store(w);
	uint64_t id = w->id;
	pthread_mutex_unlock(&yona_kq_mutex);
	queue_file_work(w);
	return id;
}

uint64_t kq_submit_read(int fd, void* buf, size_t len, off_t off) {
	return submit_file(KQ_KIND_READ, fd, buf, len, off);
}

uint64_t kq_submit_write(int fd, const void* buf, size_t len, off_t off) {
	return submit_file(KQ_KIND_WRITE, fd, (void*)buf, len, off);
}

uint64_t kq_submit_connect(int fd, const void* addr, socklen_t addrlen) {
	pthread_mutex_lock(&yona_kq_mutex);
	if (!yona_kq_ready && yona_kq_init() != 0) {
		pthread_mutex_unlock(&yona_kq_mutex);
		return 0;
	}
	set_nonblock(fd);
	kq_work_t* w = (kq_work_t*)calloc(1, sizeof(kq_work_t));
	if (!w) {
		pthread_mutex_unlock(&yona_kq_mutex);
		return 0;
	}
	w->id = alloc_id();
	w->kind = KQ_KIND_CONNECT;
	w->fd = fd;
	if (addr && addrlen > 0) {
		if (addrlen > sizeof(w->addr))
			addrlen = sizeof(w->addr);
		memcpy(&w->addr, addr, addrlen);
		w->addrlen = addrlen;
		w->buf = &w->addr;
	}
	int rc = connect(fd, (const struct sockaddr*)addr, addrlen);
	if (rc == 0) {
		uint64_t id = w->id;
		free(w);
		(void)pending_put(id, 0);
		pthread_mutex_unlock(&yona_kq_mutex);
		return id;
	}
	if (errno != EINPROGRESS) {
		uint64_t id = w->id;
		int32_t err = (int32_t)-errno;
		free(w);
		(void)pending_put(id, err);
		pthread_mutex_unlock(&yona_kq_mutex);
		return id;
	}
	if (arm_socket(w, EVFILT_WRITE) < 0) {
		free(w);
		pthread_mutex_unlock(&yona_kq_mutex);
		return 0;
	}
	op_store(w);
	uint64_t id = w->id;
	pthread_mutex_unlock(&yona_kq_mutex);
	return id;
}

uint64_t kq_submit_accept(int fd, void* addr, socklen_t* addrlen) {
	pthread_mutex_lock(&yona_kq_mutex);
	if (!yona_kq_ready && yona_kq_init() != 0) {
		pthread_mutex_unlock(&yona_kq_mutex);
		return 0;
	}
	set_nonblock(fd);
	kq_work_t* w = (kq_work_t*)calloc(1, sizeof(kq_work_t));
	if (!w) {
		pthread_mutex_unlock(&yona_kq_mutex);
		return 0;
	}
	w->id = alloc_id();
	w->kind = KQ_KIND_ACCEPT;
	w->fd = fd;
	w->buf = addr;
	w->addrlen_ptr = addrlen;
	int nfd = accept(fd, (struct sockaddr*)addr, addrlen);
	if (nfd >= 0) {
		uint64_t id = w->id;
		free(w);
		(void)pending_put(id, (int32_t)nfd);
		pthread_mutex_unlock(&yona_kq_mutex);
		return id;
	}
	if (errno != EAGAIN && errno != EWOULDBLOCK) {
		uint64_t id = w->id;
		int32_t err = (int32_t)-errno;
		free(w);
		(void)pending_put(id, err);
		pthread_mutex_unlock(&yona_kq_mutex);
		return id;
	}
	if (arm_socket(w, EVFILT_READ) < 0) {
		free(w);
		pthread_mutex_unlock(&yona_kq_mutex);
		return 0;
	}
	op_store(w);
	uint64_t id = w->id;
	pthread_mutex_unlock(&yona_kq_mutex);
	return id;
}

uint64_t kq_submit_send(int fd, const void* buf, size_t len) {
	pthread_mutex_lock(&yona_kq_mutex);
	if (!yona_kq_ready && yona_kq_init() != 0) {
		pthread_mutex_unlock(&yona_kq_mutex);
		return 0;
	}
	set_nonblock(fd);
	kq_work_t* w = (kq_work_t*)calloc(1, sizeof(kq_work_t));
	if (!w) {
		pthread_mutex_unlock(&yona_kq_mutex);
		return 0;
	}
	w->id = alloc_id();
	w->kind = KQ_KIND_SEND;
	w->fd = fd;
	w->buf = (void*)buf;
	w->len = len;
	ssize_t n = send(fd, buf, len, 0);
	if (n >= 0) {
		uint64_t id = w->id;
		free(w);
		(void)pending_put(id, (int32_t)n);
		pthread_mutex_unlock(&yona_kq_mutex);
		return id;
	}
	if (errno != EAGAIN && errno != EWOULDBLOCK) {
		uint64_t id = w->id;
		int32_t err = (int32_t)-errno;
		free(w);
		(void)pending_put(id, err);
		pthread_mutex_unlock(&yona_kq_mutex);
		return id;
	}
	if (arm_socket(w, EVFILT_WRITE) < 0) {
		free(w);
		pthread_mutex_unlock(&yona_kq_mutex);
		return 0;
	}
	op_store(w);
	uint64_t id = w->id;
	pthread_mutex_unlock(&yona_kq_mutex);
	return id;
}

uint64_t kq_submit_recv(int fd, void* buf, size_t len) {
	pthread_mutex_lock(&yona_kq_mutex);
	if (!yona_kq_ready && yona_kq_init() != 0) {
		pthread_mutex_unlock(&yona_kq_mutex);
		return 0;
	}
	set_nonblock(fd);
	kq_work_t* w = (kq_work_t*)calloc(1, sizeof(kq_work_t));
	if (!w) {
		pthread_mutex_unlock(&yona_kq_mutex);
		return 0;
	}
	w->id = alloc_id();
	w->kind = KQ_KIND_RECV;
	w->fd = fd;
	w->buf = buf;
	w->len = len;
	ssize_t n = recv(fd, buf, len, 0);
	if (n >= 0) {
		uint64_t id = w->id;
		free(w);
		(void)pending_put(id, (int32_t)n);
		pthread_mutex_unlock(&yona_kq_mutex);
		return id;
	}
	if (errno != EAGAIN && errno != EWOULDBLOCK) {
		uint64_t id = w->id;
		int32_t err = (int32_t)-errno;
		free(w);
		(void)pending_put(id, err);
		pthread_mutex_unlock(&yona_kq_mutex);
		return id;
	}
	if (arm_socket(w, EVFILT_READ) < 0) {
		free(w);
		pthread_mutex_unlock(&yona_kq_mutex);
		return 0;
	}
	op_store(w);
	uint64_t id = w->id;
	pthread_mutex_unlock(&yona_kq_mutex);
	return id;
}

uint64_t kq_submit_nop(void) {
	pthread_mutex_lock(&yona_kq_mutex);
	if (!yona_kq_ready && yona_kq_init() != 0) {
		pthread_mutex_unlock(&yona_kq_mutex);
		return 0;
	}
	uint64_t id = alloc_id();
	(void)pending_put(id, 0);
	pthread_mutex_unlock(&yona_kq_mutex);
	return id;
}

int32_t kq_await(uint64_t id) {
	for (;;) {
		pthread_mutex_lock(&yona_kq_mutex);
		if (!yona_kq_ready && yona_kq_init() != 0) {
			pthread_mutex_unlock(&yona_kq_mutex);
			return -1;
		}
		int32_t stashed = 0;
		if (pending_take(id, &stashed)) {
			pthread_mutex_unlock(&yona_kq_mutex);
			return stashed;
		}
		int kq = yona_kq;
		pthread_mutex_unlock(&yona_kq_mutex);

		struct kevent ev;
		int n = kevent(kq, NULL, 0, &ev, 1, NULL);

		pthread_mutex_lock(&yona_kq_mutex);
		if (n > 0) {
			if ((int)ev.ident == yona_wake_r && ev.filter == EVFILT_READ)
				drain_wakeup();
			else
				handle_socket_event((int)ev.ident, ev.filter);
		}
		if (pending_take(id, &stashed)) {
			pthread_mutex_unlock(&yona_kq_mutex);
			return stashed;
		}
		pthread_mutex_unlock(&yona_kq_mutex);
	}
}

void kq_cancel(uint64_t target_id) {
	pthread_mutex_lock(&yona_kq_mutex);
	if (!yona_kq_ready) {
		pthread_mutex_unlock(&yona_kq_mutex);
		return;
	}
	kq_work_t* w = op_find(target_id);
	if (!w) {
		pthread_mutex_unlock(&yona_kq_mutex);
		return;
	}
	w->cancelled = 1;
	if (w->kind == KQ_KIND_CONNECT || w->kind == KQ_KIND_ACCEPT ||
	    w->kind == KQ_KIND_SEND || w->kind == KQ_KIND_RECV) {
		struct kevent ev;
		int16_t filt = (w->kind == KQ_KIND_RECV || w->kind == KQ_KIND_ACCEPT)
			? EVFILT_READ : EVFILT_WRITE;
		EV_SET(&ev, (uintptr_t)w->fd, filt, EV_DELETE, 0, 0, NULL);
		(void)kevent(yona_kq, &ev, 1, NULL, 0, NULL);
		complete_locked(target_id, -ECANCELED);
	}
	pthread_mutex_unlock(&yona_kq_mutex);
}

void kq_cancel_group_ios(uint64_t* io_ids, int count) {
	for (int i = 0; i < count; i++)
		kq_cancel(io_ids[i]);
}

static struct {
	uint64_t id;
	io_context_t* ctx;
} io_ctx_table[IO_CTX_TABLE_SIZE];

static pthread_mutex_t io_ctx_mutex = PTHREAD_MUTEX_INITIALIZER;

void io_ctx_put(uint64_t id, io_context_t* ctx) {
	pthread_mutex_lock(&io_ctx_mutex);
	unsigned idx = (unsigned)(id % IO_CTX_TABLE_SIZE);
	for (unsigned i = 0; i < IO_CTX_TABLE_SIZE; i++) {
		unsigned slot = (idx + i) % IO_CTX_TABLE_SIZE;
		if (io_ctx_table[slot].id == 0) {
			io_ctx_table[slot].id = id;
			io_ctx_table[slot].ctx = ctx;
			pthread_mutex_unlock(&io_ctx_mutex);
			return;
		}
	}
	pthread_mutex_unlock(&io_ctx_mutex);
}

io_context_t* io_ctx_take(uint64_t id) {
	pthread_mutex_lock(&io_ctx_mutex);
	unsigned idx = (unsigned)(id % IO_CTX_TABLE_SIZE);
	for (unsigned i = 0; i < IO_CTX_TABLE_SIZE; i++) {
		unsigned slot = (idx + i) % IO_CTX_TABLE_SIZE;
		if (io_ctx_table[slot].id == id) {
			io_context_t* ctx = io_ctx_table[slot].ctx;
			io_ctx_table[slot].id = 0;
			io_ctx_table[slot].ctx = NULL;
			pthread_mutex_unlock(&io_ctx_mutex);
			return ctx;
		}
		if (io_ctx_table[slot].id == 0)
			break;
	}
	pthread_mutex_unlock(&io_ctx_mutex);
	return NULL;
}
