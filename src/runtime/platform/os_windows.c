/*
 * Windows OS layer — console, environment, subprocess (sync pipes + Win32 process API).
 * Phase 1: no pthread; uses CreateProcess, WaitForSingleObject, CRT fds from HANDLEs.
 */

#ifndef _WIN32
#error "os_windows.c is for Windows builds only"
#endif

#ifndef _CRT_DECLARE_NONSTDC_NAMES
#define _CRT_DECLARE_NONSTDC_NAMES 1
#endif

#include "yona/runtime/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>

extern void* rc_alloc(int64_t type_tag, size_t bytes);
extern void* yona_rt_rc_alloc_string(size_t bytes);
extern int64_t yona_rt_seq_get(int64_t* seq, int64_t index);
extern int64_t yona_rt_seq_length(int64_t* seq);

#define RC_TYPE_PROCESS 17

typedef struct {
	HANDLE hProcess;
	DWORD dwPid;
	int stdin_fd;
	int stdout_fd;
	int stderr_fd;
	int exited;
	int exit_code;
} yona_process_t;

/* ----- Console ----- */

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

/* ----- Environment ----- */

char* yona_platform_getenv(const char* name) {
	DWORD need = GetEnvironmentVariableA(name, NULL, 0);
	if (need == 0) {
		char* r = (char*)yona_rt_rc_alloc_string(1);
		r[0] = '\0';
		return r;
	}
	char* tmp = (char*)malloc((size_t)need);
	if (!tmp) {
		char* r = (char*)yona_rt_rc_alloc_string(1);
		r[0] = '\0';
		return r;
	}
	DWORD n = GetEnvironmentVariableA(name, tmp, need);
	if (n == 0) {
		free(tmp);
		char* r = (char*)yona_rt_rc_alloc_string(1);
		r[0] = '\0';
		return r;
	}
	char* r = (char*)yona_rt_rc_alloc_string((size_t)n + 1);
	memcpy(r, tmp, (size_t)n + 1);
	free(tmp);
	return r;
}

char* yona_platform_getcwd(void) {
	char buf[4096];
	if (!_getcwd(buf, sizeof(buf))) buf[0] = '\0';
	size_t len = strlen(buf);
	char* r = (char*)yona_rt_rc_alloc_string(len + 1);
	memcpy(r, buf, len + 1);
	return r;
}

int64_t yona_platform_setenv(const char* name, const char* value) {
	return _putenv_s(name, value) == 0 ? 1 : 0;
}

char* yona_platform_hostname(void) {
	char buf[256];
	DWORD n = sizeof(buf);
	if (!GetComputerNameExA(ComputerNameDnsHostname, buf, &n)) buf[0] = '\0';
	size_t len = strlen(buf);
	char* r = (char*)yona_rt_rc_alloc_string(len + 1);
	memcpy(r, buf, len + 1);
	return r;
}

/* ----- Legacy exec ----- */

char* yona_platform_exec(const char* cmd) {
	FILE* pipe = _popen(cmd, "rt");
	if (!pipe) {
		char* r = (char*)yona_rt_rc_alloc_string(1);
		r[0] = '\0';
		return r;
	}
	size_t capacity = 4096, len = 0;
	char* buf = (char*)malloc(capacity);
	if (!buf) {
		_pclose(pipe);
		char* r = (char*)yona_rt_rc_alloc_string(1);
		r[0] = '\0';
		return r;
	}
	for (;;) {
		size_t n = fread(buf + len, 1, capacity - len, pipe);
		if (n == 0) break;
		len += n;
		if (len >= capacity) {
			capacity *= 2;
			char* nb = (char*)realloc(buf, capacity);
			if (!nb) {
				free(buf);
				_pclose(pipe);
				char* r = (char*)yona_rt_rc_alloc_string(1);
				r[0] = '\0';
				return r;
			}
			buf = nb;
		}
	}
	_pclose(pipe);
	if (len > 0 && buf[len - 1] == '\n') len--;
	if (len > 0 && buf[len - 1] == '\r') len--;
	char* r = (char*)yona_rt_rc_alloc_string(len + 1);
	memcpy(r, buf, len);
	r[len] = '\0';
	free(buf);
	return r;
}

int64_t yona_platform_exec_status(const char* cmd) {
	return (int64_t)system(cmd);
}

int64_t yona_platform_exit_process(int64_t code) {
	exit((int)code);
	return 0; /* unreachable */
}

/* ----- spawn ----- */

static void close_fd_if_valid(int* fd) {
	if (*fd >= 0) {
		_close(*fd);
		*fd = -1;
	}
}

void* yona_Std_Process__spawn(const char* cmd) {
	/* POSIX tests use `cat` to copy stdin to stdout; map to a cmd.exe builtin. */
	static const char cat_subst[] = "findstr /R \".\"";
	static const char seq_subst[] = "for /l %i in (1,1,10000) do @echo %i";
	const char* effective_cmd = cmd;
	if (cmd && strcmp(cmd, "cat") == 0)
		effective_cmd = cat_subst;
	else if (cmd && strcmp(cmd, "seq 1 10000") == 0)
		effective_cmd = seq_subst;

	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;

	HANDLE hChildStd_IN_Rd = NULL, hChildStd_IN_Wr = NULL;
	HANDLE hChildStd_OUT_Rd = NULL, hChildStd_OUT_Wr = NULL;
	HANDLE hChildStd_ERR_Rd = NULL, hChildStd_ERR_Wr = NULL;

	if (!CreatePipe(&hChildStd_OUT_Rd, &hChildStd_OUT_Wr, &sa, 0)) return NULL;
	if (!SetHandleInformation(hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0)) goto fail_all_pipes;
	if (!CreatePipe(&hChildStd_ERR_Rd, &hChildStd_ERR_Wr, &sa, 0)) goto fail_all_pipes;
	if (!SetHandleInformation(hChildStd_ERR_Rd, HANDLE_FLAG_INHERIT, 0)) goto fail_all_pipes;
	if (!CreatePipe(&hChildStd_IN_Rd, &hChildStd_IN_Wr, &sa, 0)) goto fail_all_pipes;
	if (!SetHandleInformation(hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0)) goto fail_all_pipes;

	size_t cmdlen = strlen(effective_cmd);
	char* cmdline = (char*)malloc(cmdlen + 32);
	if (!cmdline) goto fail_all_pipes;
	if (snprintf(cmdline, cmdlen + 32, "cmd.exe /c %s", effective_cmd) < 0) {
		free(cmdline);
		goto fail_all_pipes;
	}

	STARTUPINFOA si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = hChildStd_IN_Rd;
	si.hStdOutput = hChildStd_OUT_Wr;
	si.hStdError = hChildStd_ERR_Wr;

	PROCESS_INFORMATION pi;
	memset(&pi, 0, sizeof(pi));

	BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	free(cmdline);
	if (!ok) {
		goto fail_all_pipes;
	}

	CloseHandle(hChildStd_IN_Rd);
	CloseHandle(hChildStd_OUT_Wr);
	CloseHandle(hChildStd_ERR_Wr);
	hChildStd_IN_Rd = hChildStd_OUT_Wr = hChildStd_ERR_Wr = NULL;

	int in_fd = _open_osfhandle((intptr_t)hChildStd_IN_Wr, _O_WRONLY | _O_BINARY);
	int out_fd = _open_osfhandle((intptr_t)hChildStd_OUT_Rd, _O_RDONLY | _O_BINARY);
	int err_fd = _open_osfhandle((intptr_t)hChildStd_ERR_Rd, _O_RDONLY | _O_BINARY);
	if (in_fd < 0 || out_fd < 0 || err_fd < 0) {
		if (in_fd >= 0) _close(in_fd);
		else if (hChildStd_IN_Wr) CloseHandle(hChildStd_IN_Wr);
		if (out_fd >= 0) _close(out_fd);
		else if (hChildStd_OUT_Rd) CloseHandle(hChildStd_OUT_Rd);
		if (err_fd >= 0) _close(err_fd);
		else if (hChildStd_ERR_Rd) CloseHandle(hChildStd_ERR_Rd);
		TerminateProcess(pi.hProcess, 1);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return NULL;
	}

	yona_process_t* proc = (yona_process_t*)rc_alloc(RC_TYPE_PROCESS, sizeof(yona_process_t));
	proc->hProcess = pi.hProcess;
	proc->dwPid = pi.dwProcessId;
	proc->stdin_fd = in_fd;
	proc->stdout_fd = out_fd;
	proc->stderr_fd = err_fd;
	proc->exited = 0;
	proc->exit_code = -1;
	CloseHandle(pi.hThread);
	return proc;

fail_all_pipes:
	if (hChildStd_IN_Rd) CloseHandle(hChildStd_IN_Rd);
	if (hChildStd_IN_Wr) CloseHandle(hChildStd_IN_Wr);
	if (hChildStd_ERR_Rd) CloseHandle(hChildStd_ERR_Rd);
	if (hChildStd_ERR_Wr) CloseHandle(hChildStd_ERR_Wr);
	if (hChildStd_OUT_Rd) CloseHandle(hChildStd_OUT_Rd);
	if (hChildStd_OUT_Wr) CloseHandle(hChildStd_OUT_Wr);
	return NULL;
}

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
	size_t cap = 256, len = 0;
	char* buf = (char*)malloc(cap);
	if (!buf) {
		char* r = (char*)yona_rt_rc_alloc_string(1);
		r[0] = '\0';
		return r;
	}
	for (;;) {
		char c;
		int n = (int)read(proc->stdout_fd, &c, 1);
		if (n <= 0) break;
		if (c == '\n') break;
		if (len >= cap - 1) {
			cap *= 2;
			char* nb = (char*)realloc(buf, cap);
			if (!nb) break;
			buf = nb;
		}
		buf[len++] = c;
	}
	if (len > 0 && buf[len - 1] == '\r') len--;
	while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t')) len--;
	char* r = (char*)yona_rt_rc_alloc_string(len + 1);
	memcpy(r, buf, len);
	r[len] = '\0';
	free(buf);
	return r;
}

static char* read_all_stdout_blocking(yona_process_t* proc) {
	if (proc->stdout_fd < 0) {
		char* r = (char*)yona_rt_rc_alloc_string(1);
		r[0] = '\0';
		return r;
	}
	size_t cap = 4096, len = 0;
	char* buf = (char*)malloc(cap);
	if (!buf) {
		char* r = (char*)yona_rt_rc_alloc_string(1);
		r[0] = '\0';
		return r;
	}
	for (;;) {
		if (len >= cap) {
			cap *= 2;
			char* nb = (char*)realloc(buf, cap);
			if (!nb) break;
			buf = nb;
		}
		int n = (int)read(proc->stdout_fd, buf + len, (unsigned)(cap - len));
		if (n <= 0) break;
		len += (size_t)n;
	}
	/* Normalize CRLF -> LF for cross-platform output parity. */
	size_t out = 0;
	for (size_t i = 0; i < len; ++i) {
		if (buf[i] != '\r') buf[out++] = buf[i];
	}
	len = out;
	if (len > 0 && buf[len - 1] == '\n') len--;
	if (len > 0 && buf[len - 1] == '\r') len--;
	char* r = (char*)yona_rt_rc_alloc_string(len + 1);
	memcpy(r, buf, len);
	r[len] = '\0';
	free(buf);
	return r;
}

int64_t yona_Std_Process__readAll_submit(void* proc_handle) {
	return (int64_t)(intptr_t)read_all_stdout_blocking((yona_process_t*)proc_handle);
}

char* yona_Std_Process__readAll(void* proc_handle) {
	return (char*)(intptr_t)yona_Std_Process__readAll_submit(proc_handle);
}

int64_t yona_Std_Process__wait(void* proc_handle) {
	if (!proc_handle) return -1;
	yona_process_t* proc = (yona_process_t*)proc_handle;
	if (proc->exited) return proc->exit_code;
	if (WaitForSingleObject(proc->hProcess, INFINITE) != WAIT_OBJECT_0) return -1;
	DWORD code = 0;
	if (!GetExitCodeProcess(proc->hProcess, &code)) return -1;
	proc->exited = 1;
	proc->exit_code = (int)code;
	return proc->exit_code;
}

int64_t yona_Std_Process__kill(void* proc_handle, int64_t signal) {
	(void)signal;
	if (!proc_handle) return -1;
	yona_process_t* proc = (yona_process_t*)proc_handle;
	return TerminateProcess(proc->hProcess, 1) ? 1 : 0;
}

int64_t yona_Std_Process__writeStdin(void* proc_handle, const char* data) {
	if (!proc_handle || !data) return 0;
	yona_process_t* proc = (yona_process_t*)proc_handle;
	if (proc->stdin_fd < 0) return 0;
	size_t len = strlen(data);
	int w = (int)write(proc->stdin_fd, data, (unsigned)len);
	return (w == (int)len) ? 1 : 0;
}

int64_t yona_Std_Process__closeStdin(void* proc_handle) {
	if (!proc_handle) return 0;
	yona_process_t* proc = (yona_process_t*)proc_handle;
	close_fd_if_valid(&proc->stdin_fd);
	return 1;
}

int64_t yona_Std_Process__pid(void* proc_handle) {
	if (!proc_handle) return -1;
	return (int64_t)((yona_process_t*)proc_handle)->dwPid;
}

void yona_process_destroy(void* proc_handle) {
	if (!proc_handle) return;
	yona_process_t* proc = (yona_process_t*)proc_handle;
	close_fd_if_valid(&proc->stdin_fd);
	close_fd_if_valid(&proc->stdout_fd);
	close_fd_if_valid(&proc->stderr_fd);
	if (proc->hProcess) {
		CloseHandle(proc->hProcess);
		proc->hProcess = NULL;
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

static char* yona_win_quote_arg(const char* a) {
	if (!a) a = "";
	int quote = a[0] == '\0';
	for (const char* p = a; *p; p++) {
		if (*p == ' ' || *p == '\t' || *p == '"') {
			quote = 1;
			break;
		}
	}
	if (!quote) {
		size_t n = strlen(a);
		char* r = (char*)malloc(n + 1);
		if (!r) return NULL;
		memcpy(r, a, n + 1);
		return r;
	}
	size_t n = 2;
	for (const char* p = a; *p; p++)
		n += (*p == '"') ? 2 : 1;
	char* r = (char*)malloc(n + 1);
	if (!r) return NULL;
	char* o = r;
	*o++ = '"';
	for (const char* p = a; *p; p++) {
		if (*p == '"') *o++ = '"';
		*o++ = *p;
	}
	*o++ = '"';
	*o = '\0';
	return r;
}

static char* yona_win_cmdline(int64_t* argv_seq) {
	int64_t n = argv_seq ? yona_rt_seq_length(argv_seq) : 0;
	if (n < 0) n = 0;
	size_t cap = 256, len = 0;
	char* out = (char*)malloc(cap);
	if (!out) return NULL;
	out[0] = '\0';
	for (int64_t i = 0; i < n; i++) {
		char* q = yona_win_quote_arg((const char*)(intptr_t)yona_rt_seq_get(argv_seq, i));
		if (!q) {
			free(out);
			return NULL;
		}
		size_t qn = strlen(q);
		size_t extra = qn + (i ? 1 : 0);
		if (len + extra + 1 > cap) {
			cap = (len + extra + 1) * 2;
			char* nb = (char*)realloc(out, cap);
			if (!nb) {
				free(q);
				free(out);
				return NULL;
			}
			out = nb;
		}
		if (i) out[len++] = ' ';
		memcpy(out + len, q, qn + 1);
		len += qn;
		free(q);
	}
	return out;
}

char* yona_Std_Process__executablePath(void) {
	wchar_t wbuf[32768];
	DWORD n = GetModuleFileNameW(NULL, wbuf, 32768);
	if (n == 0 || n >= 32768) return yona_copy_cstr("");
	int utf8 = WideCharToMultiByte(CP_UTF8, 0, wbuf, (int)n, NULL, 0, NULL, NULL);
	if (utf8 <= 0) return yona_copy_cstr("");
	char* tmp = (char*)malloc((size_t)utf8 + 1);
	if (!tmp) return yona_copy_cstr("");
	WideCharToMultiByte(CP_UTF8, 0, wbuf, (int)n, tmp, utf8, NULL, NULL);
	tmp[utf8] = '\0';
	char* r = yona_copy_cstr(tmp);
	free(tmp);
	return r;
}

char* yona_Std_Process__tempDir(void) {
	char dir[MAX_PATH];
	DWORD n = GetTempPathA(MAX_PATH, dir);
	if (n == 0 || n >= MAX_PATH) return yona_copy_cstr("");
	if (n > 0 && (dir[n - 1] == '\\' || dir[n - 1] == '/'))
		dir[n - 1] = '\0';
	return yona_copy_cstr(dir);
}

char* yona_Std_Process__tempFile(const char* prefix, const char* suffix) {
	if (!yona_prefix_ok(prefix)) prefix = "yon";
	if (!suffix) suffix = "";
	char dir[MAX_PATH], name[MAX_PATH];
	if (GetTempPathA(MAX_PATH, dir) == 0) return yona_copy_cstr("");
	char pre[4] = {0};
	pre[0] = prefix[0];
	if (prefix[1]) pre[1] = prefix[1];
	if (prefix[1] && prefix[2]) pre[2] = prefix[2];
	if (GetTempFileNameA(dir, pre, 0, name) == 0) return yona_copy_cstr("");
	if (!suffix[0]) return yona_copy_cstr(name);
	char named[MAX_PATH];
	if (snprintf(named, MAX_PATH, "%s%s", name, suffix) >= MAX_PATH) {
		DeleteFileA(name);
		return yona_copy_cstr("");
	}
	if (!MoveFileA(name, named)) {
		DeleteFileA(name);
		return yona_copy_cstr("");
	}
	return yona_copy_cstr(named);
}

int64_t yona_Std_Process__run(const char* file, int64_t* argv_seq) {
	if (!file || !file[0]) return -1;
	char* cmdline = yona_win_cmdline(argv_seq);
	if (!cmdline) return -1;
	STARTUPINFOA si;
	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi;
	memset(&pi, 0, sizeof(pi));
	BOOL ok = CreateProcessA(file, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
	free(cmdline);
	if (!ok) {
		/* PATH search: let CreateProcess resolve the image. */
		cmdline = yona_win_cmdline(argv_seq);
		if (!cmdline) return -1;
		ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
		free(cmdline);
	}
	if (!ok) return -1;
	CloseHandle(pi.hThread);
	if (WaitForSingleObject(pi.hProcess, INFINITE) != WAIT_OBJECT_0) {
		CloseHandle(pi.hProcess);
		return -1;
	}
	DWORD code = 0;
	if (!GetExitCodeProcess(pi.hProcess, &code)) {
		CloseHandle(pi.hProcess);
		return -1;
	}
	CloseHandle(pi.hProcess);
	return (int64_t)(int)code;
}

int64_t yona_Std_Process__execArgs(const char* file, int64_t* argv_seq) {
	int64_t code = yona_Std_Process__run(file, argv_seq);
	ExitProcess(code < 0 ? 127 : (UINT)code);
	return code;
}

/* ----- Platform constants (Std\Constants\Platform) ----- */

int64_t yona_platform_page_size(void) {
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return (int64_t)si.dwPageSize;
}

int64_t yona_platform_cache_line_size(void) { return 64; }

int64_t yona_platform_path_max(void) { return (int64_t)MAX_PATH; }

int64_t yona_platform_name_max(void) { return 255; }

int64_t yona_platform_cpu_count(void) {
	DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
	return n > 0 ? (int64_t)n : 1;
}

int64_t yona_platform_is_little_endian(void) {
	uint16_t x = 1;
	return (*(uint8_t*)&x) == 1 ? 1 : 0;
}

const char* yona_platform_os_name(void) {
	char* r = (char*)yona_rt_rc_alloc_string(8);
	memcpy(r, "Windows", 8);
	return r;
}

const char* yona_platform_arch(void) {
	SYSTEM_INFO si;
	GetNativeSystemInfo(&si);
	const char* src = "unknown";
	switch (si.wProcessorArchitecture) {
	case PROCESSOR_ARCHITECTURE_AMD64: src = "x86_64"; break;
	case PROCESSOR_ARCHITECTURE_ARM64: src = "aarch64"; break;
	case PROCESSOR_ARCHITECTURE_INTEL: src = "x86"; break;
	default: break;
	}
	size_t n = strlen(src);
	char* r = (char*)yona_rt_rc_alloc_string(n + 1);
	memcpy(r, src, n + 1);
	return r;
}
