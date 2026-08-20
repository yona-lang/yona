/*
 * Linux OS operations — console I/O, process, environment.
 *
 * Process module provides both blocking (legacy) and non-blocking APIs:
 *   - exec/execStatus: blocking, but marked AFN for thread-pool async
 *   - spawn: fork/execvp with pipe setup, returns RC-managed process handle
 *   - readLine/readAll: io_uring async reads on subprocess stdout pipe
 *   - wait: thread-pool async waitpid for exit status
 *   - kill: send signal to subprocess
 *   - writeStdin: write to subprocess stdin pipe
 */

#include "yona/runtime/platform.h"
#include "yona/runtime/uring.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

extern void* rc_alloc(int64_t type_tag, size_t bytes);
extern void* yona_rt_rc_alloc_string(size_t bytes);
extern void yona_rt_rc_inc(void* ptr);
extern void yona_rt_rc_dec(void* ptr);
extern int64_t* yona_rt_seq_alloc(int64_t count);
extern int64_t yona_rt_seq_get(int64_t* seq, int64_t index);
extern int64_t yona_rt_seq_length(int64_t* seq);

#define RC_TYPE_PROCESS 17

/* ===== Process handle ===== */

typedef struct {
    int64_t pid;
    int stdin_fd;    /* write end of stdin pipe (-1 if closed) */
    int stdout_fd;   /* read end of stdout pipe (-1 if closed) */
    int stderr_fd;   /* read end of stderr pipe (-1 if closed) */
    int exited;      /* 1 if waitpid has been called */
    int exit_code;   /* exit status (valid if exited==1) */
} yona_process_t;

/* ===== Console I/O ===== */

char* yona_platform_read_line(void) {
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) {
        char* r = (char*)yona_rt_rc_alloc_string(1);
        r[0] = '\0';
        return r;
    }
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
    if (len > 0 && buf[len - 1] == '\r') buf[--len] = '\0';
    char* r = (char*)yona_rt_rc_alloc_string(len + 1);
    memcpy(r, buf, len + 1);
    return r;
}

/* ===== Environment ===== */

char* yona_platform_getenv(const char* name) {
    const char* val = getenv(name);
    if (!val) val = "";
    size_t len = strlen(val);
    char* r = (char*)yona_rt_rc_alloc_string(len + 1);
    memcpy(r, val, len + 1);
    return r;
}

char* yona_platform_getcwd(void) {
    char buf[4096];
    if (!getcwd(buf, sizeof(buf))) buf[0] = '\0';
    size_t len = strlen(buf);
    char* r = (char*)yona_rt_rc_alloc_string(len + 1);
    memcpy(r, buf, len + 1);
    return r;
}

int64_t yona_platform_setenv(const char* name, const char* value) {
    return setenv(name, value, 1) == 0 ? 1 : 0;
}

char* yona_platform_hostname(void) {
    char buf[256];
    if (gethostname(buf, sizeof(buf)) != 0) buf[0] = '\0';
    size_t len = strlen(buf);
    char* r = (char*)yona_rt_rc_alloc_string(len + 1);
    memcpy(r, buf, len + 1);
    return r;
}

/* ===== Legacy exec (blocking, but used via AFN for async) ===== */

char* yona_platform_exec(const char* cmd) {
    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        char* r = (char*)yona_rt_rc_alloc_string(1);
        r[0] = '\0';
        return r;
    }
    size_t capacity = 4096, len = 0;
    char* buf = (char*)malloc(capacity);
    while (1) {
        size_t n = fread(buf + len, 1, capacity - len, pipe);
        if (n == 0) break;
        len += n;
        if (len >= capacity) { capacity *= 2; buf = (char*)realloc(buf, capacity); }
    }
    pclose(pipe);
    if (len > 0 && buf[len - 1] == '\n') len--;
    if (len > 0 && buf[len - 1] == '\r') len--;
    char* r = (char*)yona_rt_rc_alloc_string(len + 1);
    memcpy(r, buf, len);
    r[len] = '\0';
    free(buf);
    return r;
}

int64_t yona_platform_exec_status(const char* cmd) {
    int status = system(cmd);
    if (status == -1) return -1;
    return WEXITSTATUS(status);
}

int64_t yona_platform_exit_process(int64_t code) {
    exit((int)code);
    return 0; /* unreachable */
}

/* ===== spawn: fork/exec with pipe setup ===== */

void* yona_Std_Process__spawn(const char* cmd) {
    int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdin_pipe) < 0) return NULL;
    if (pipe(stdout_pipe) < 0) { close(stdin_pipe[0]); close(stdin_pipe[1]); return NULL; }
    if (pipe(stderr_pipe) < 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return NULL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return NULL;
    }

    if (pid == 0) {
        /* Child: wire up pipes */
        close(stdin_pipe[1]);   /* close write end of stdin */
        close(stdout_pipe[0]);  /* close read end of stdout */
        close(stderr_pipe[0]);  /* close read end of stderr */
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char*)NULL);
        _exit(127); /* exec failed */
    }

    /* Parent: close child ends */
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    /* Set stdout pipe to non-blocking for io_uring */
    fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

    yona_process_t* proc = (yona_process_t*)rc_alloc(RC_TYPE_PROCESS, sizeof(yona_process_t));
    proc->pid = (int64_t)pid;
    proc->stdin_fd = stdin_pipe[1];
    proc->stdout_fd = stdout_pipe[0];
    proc->stderr_fd = stderr_pipe[0];
    proc->exited = 0;
    proc->exit_code = -1;
    return proc;
}

/* ===== readLine: read one line from subprocess stdout ===== */

char* yona_Std_Process__readLine(void* proc_handle) {
    if (!proc_handle) {
        char* r = (char*)yona_rt_rc_alloc_string(1);
        r[0] = '\0';
        return r;
    }
    yona_process_t* proc = (yona_process_t*)proc_handle;
    if (proc->stdout_fd < 0) {
        char* r = (char*)yona_rt_rc_alloc_string(1);
        r[0] = '\0';
        return r;
    }

    /* Read one byte at a time until newline or EOF.
     * Use blocking read (remove O_NONBLOCK temporarily). */
    int flags = fcntl(proc->stdout_fd, F_GETFL);
    fcntl(proc->stdout_fd, F_SETFL, flags & ~O_NONBLOCK);

    size_t cap = 256, len = 0;
    char* buf = (char*)malloc(cap);
    while (1) {
        char c;
        ssize_t n = read(proc->stdout_fd, &c, 1);
        if (n <= 0) break; /* EOF or error */
        if (c == '\n') break;
        if (len >= cap - 1) { cap *= 2; buf = (char*)realloc(buf, cap); }
        buf[len++] = c;
    }

    /* Restore non-blocking */
    fcntl(proc->stdout_fd, F_SETFL, flags);

    if (len > 0 && buf[len - 1] == '\r') len--;
    char* r = (char*)yona_rt_rc_alloc_string(len + 1);
    memcpy(r, buf, len);
    r[len] = '\0';
    free(buf);
    return r;
}

/* ===== readAll: read all remaining stdout via io_uring ===== */

int64_t yona_Std_Process__readAll_submit(void* proc_handle) {
    if (!proc_handle) return 0;
    yona_process_t* proc = (yona_process_t*)proc_handle;
    if (proc->stdout_fd < 0) return 0;

    /* For readAll, we need to read until EOF. io_uring IORING_OP_READ
     * with a single buffer won't know the size. Use a large buffer and
     * loop. For simplicity, use blocking read in a thread instead. */

    /* Blocking approach: read all data from stdout pipe */
    int fd = proc->stdout_fd;
    /* Make blocking for the read */
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    size_t cap = 4096, len = 0;
    char* buf = (char*)malloc(cap);
    while (1) {
        ssize_t n = read(fd, buf + len, cap - len);
        if (n <= 0) break;
        len += n;
        if (len >= cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
    }
    fcntl(fd, F_SETFL, flags);

    if (len > 0 && buf[len - 1] == '\n') len--;
    if (len > 0 && buf[len - 1] == '\r') len--;
    char* r = (char*)yona_rt_rc_alloc_string(len + 1);
    memcpy(r, buf, len);
    r[len] = '\0';
    free(buf);
    return (int64_t)(intptr_t)r;
}

/* Wrapper for AFN (thread pool) */
char* yona_Std_Process__readAll(void* proc_handle) {
    return (char*)(intptr_t)yona_Std_Process__readAll_submit(proc_handle);
}

/* ===== wait: wait for subprocess to exit, return exit code ===== */

int64_t yona_Std_Process__wait(void* proc_handle) {
    if (!proc_handle) return -1;
    yona_process_t* proc = (yona_process_t*)proc_handle;
    if (proc->exited) return proc->exit_code;

    int status;
    pid_t ret = waitpid((pid_t)proc->pid, &status, 0);
    if (ret < 0) return -1;

    proc->exited = 1;
    if (WIFEXITED(status))
        proc->exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        proc->exit_code = -WTERMSIG(status);
    else
        proc->exit_code = -1;

    return proc->exit_code;
}

/* ===== kill: send signal to subprocess ===== */

int64_t yona_Std_Process__kill(void* proc_handle, int64_t signal) {
    if (!proc_handle) return -1;
    yona_process_t* proc = (yona_process_t*)proc_handle;
    return kill((pid_t)proc->pid, (int)signal) == 0 ? 1 : 0;
}

/* ===== writeStdin: write data to subprocess stdin pipe ===== */

int64_t yona_Std_Process__writeStdin(void* proc_handle, const char* data) {
    if (!proc_handle || !data) return 0;
    yona_process_t* proc = (yona_process_t*)proc_handle;
    if (proc->stdin_fd < 0) return 0;

    size_t len = strlen(data);
    ssize_t written = write(proc->stdin_fd, data, len);
    return (written == (ssize_t)len) ? 1 : 0;
}

/* ===== closeStdin: close subprocess stdin pipe (signal EOF) ===== */

int64_t yona_Std_Process__closeStdin(void* proc_handle) {
    if (!proc_handle) return 0;
    yona_process_t* proc = (yona_process_t*)proc_handle;
    if (proc->stdin_fd >= 0) {
        close(proc->stdin_fd);
        proc->stdin_fd = -1;
    }
    return 1;
}

/* ===== pid: get subprocess PID ===== */

int64_t yona_Std_Process__pid(void* proc_handle) {
    if (!proc_handle) return -1;
    return ((yona_process_t*)proc_handle)->pid;
}

/* ===== Process handle destructor (called from rc_dec) ===== */

void yona_process_destroy(void* proc_handle) {
    if (!proc_handle) return;
    yona_process_t* proc = (yona_process_t*)proc_handle;
    if (proc->stdin_fd >= 0) close(proc->stdin_fd);
    if (proc->stdout_fd >= 0) close(proc->stdout_fd);
    if (proc->stderr_fd >= 0) close(proc->stderr_fd);
    /* Reap zombie if not already waited */
    if (!proc->exited) {
        int status;
        waitpid((pid_t)proc->pid, &status, WNOHANG);
    }
}

static char* yona_copy_cstr(const char* src) {
    if (!src) src = "";
    size_t n = strlen(src);
    char* r = (char*)yona_rt_rc_alloc_string(n + 1);
    memcpy(r, src, n + 1);
    return r;
}

static int yona_prefix_ok(const char* prefix) {
    if (!prefix || !prefix[0]) return 0;
    for (const char* p = prefix; *p; p++) {
        if (*p == '/' || *p == '\\') return 0;
    }
    return 1;
}

static char** yona_seq_to_argv(int64_t* argv_seq) {
    int64_t n = argv_seq ? yona_rt_seq_length(argv_seq) : 0;
    if (n < 0) n = 0;
    char** argv = (char**)calloc((size_t)n + 1, sizeof(char*));
    if (!argv) return NULL;
    for (int64_t i = 0; i < n; i++)
        argv[i] = (char*)(intptr_t)yona_rt_seq_get(argv_seq, i);
    return argv;
}

char* yona_Std_Process__executablePath(void) {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return yona_copy_cstr("");
    buf[n] = '\0';
    return yona_copy_cstr(buf);
}

char* yona_Std_Process__tempDir(void) {
    const char* d = getenv("TMPDIR");
    if (!d || !d[0]) d = getenv("TMP");
    if (!d || !d[0]) d = getenv("TEMP");
    if (!d || !d[0]) d = "/tmp";
    return yona_copy_cstr(d);
}

char* yona_Std_Process__tempFile(const char* prefix, const char* suffix) {
    if (!yona_prefix_ok(prefix)) prefix = "yona";
    if (!suffix) suffix = "";
    char* dir = yona_Std_Process__tempDir();
    size_t need = strlen(dir) + 1 + strlen(prefix) + 6 + strlen(suffix) + 1;
    char* tmpl = (char*)malloc(need);
    if (!tmpl) {
        yona_rt_rc_dec(dir);
        return yona_copy_cstr("");
    }
    snprintf(tmpl, need, "%s/%sXXXXXX", dir, prefix);
    yona_rt_rc_dec(dir);
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        free(tmpl);
        return yona_copy_cstr("");
    }
    close(fd);
    if (suffix[0]) {
        size_t flen = strlen(tmpl) + strlen(suffix) + 1;
        char* named = (char*)malloc(flen);
        if (!named) {
            unlink(tmpl);
            free(tmpl);
            return yona_copy_cstr("");
        }
        snprintf(named, flen, "%s%s", tmpl, suffix);
        if (rename(tmpl, named) != 0) {
            unlink(tmpl);
            free(named);
            free(tmpl);
            return yona_copy_cstr("");
        }
        char* r = yona_copy_cstr(named);
        free(named);
        free(tmpl);
        return r;
    }
    char* r = yona_copy_cstr(tmpl);
    free(tmpl);
    return r;
}

int64_t yona_Std_Process__run(const char* file, int64_t* argv_seq) {
    if (!file || !file[0]) return -1;
    char** argv = yona_seq_to_argv(argv_seq);
    if (!argv) return -1;
    pid_t pid = fork();
    if (pid < 0) {
        free(argv);
        return -1;
    }
    if (pid == 0) {
        execvp(file, argv);
        _exit(127);
    }
    free(argv);
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return -(int64_t)WTERMSIG(status);
    return -1;
}

int64_t yona_Std_Process__execArgs(const char* file, int64_t* argv_seq) {
    if (!file || !file[0]) return 127;
    char** argv = yona_seq_to_argv(argv_seq);
    if (!argv) return 127;
    execvp(file, argv);
    free(argv);
    return 127;
}

/* ===== Platform constants ===== */
/* Constants exposed to Std\Constants\Platform. These are read once
 * at runtime and returned as Int/String; Yona's CAFs memoize the
 * first read so there's no per-call syscall overhead. */

#include <sys/utsname.h>
#include <limits.h>

int64_t yona_platform_page_size(void) {
    long v = sysconf(_SC_PAGESIZE);
    return v > 0 ? (int64_t)v : 4096;
}

int64_t yona_platform_cache_line_size(void) {
    long v = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    return v > 0 ? (int64_t)v : 64;
}

int64_t yona_platform_path_max(void) {
    long v = pathconf("/", _PC_PATH_MAX);
    return v > 0 ? (int64_t)v : 4096;
}

int64_t yona_platform_name_max(void) {
    long v = pathconf("/", _PC_NAME_MAX);
    return v > 0 ? (int64_t)v : 255;
}

int64_t yona_platform_cpu_count(void) {
    long v = sysconf(_SC_NPROCESSORS_ONLN);
    return v > 0 ? (int64_t)v : 1;
}

/* 1 = little-endian (x86_64, aarch64-le), 0 = big-endian. */
int64_t yona_platform_is_little_endian(void) {
    uint16_t x = 1;
    return (*(uint8_t*)&x) == 1 ? 1 : 0;
}

const char* yona_platform_os_name(void) {
    struct utsname u;
    const char* src = "unknown";
    if (uname(&u) == 0) src = u.sysname;
    size_t n = strlen(src);
    char* r = (char*)yona_rt_rc_alloc_string(n + 1);
    memcpy(r, src, n + 1);
    return r;
}

const char* yona_platform_arch(void) {
    struct utsname u;
    const char* src = "unknown";
    if (uname(&u) == 0) src = u.machine;
    size_t n = strlen(src);
    char* r = (char*)yona_rt_rc_alloc_string(n + 1);
    memcpy(r, src, n + 1);
    return r;
}
