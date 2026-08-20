/*
 * Platform abstraction layer for Yona runtime.
 *
 * Each function has a single portable interface. Implementations live in
 * src/runtime/platform/<os>.c and are selected at build time.
 *
 * Adding a new platform file:
 *   1. Create src/runtime/platform/<os>.c
 *   2. Implement all yona_platform_* functions declared here
 *   3. Append the file in CMakeLists.txt for the target OS
 *
 * Async/channel backends live next to platform TUs (`async_posix.c` /
 * `async_win32.c`, `channel_posix.c` / `channel_win32.c`); they are #included
 * from compiled_runtime.c, not compiled as separate translation units.
 */

#ifndef YONA_PLATFORM_H
#define YONA_PLATFORM_H

#include <stdint.h>
#include <stddef.h>

/* Increment only on breaking ABI contract changes. */
#define YONA_PLATFORM_ABI_VERSION 1

/* ===== I/O await (io_uring user_data on Linux; IOCP / direct-result on Windows) ===== */

int64_t yona_rt_io_await(int64_t uring_id);

/* ===== Async File I/O (submit-and-return) ===== */

int64_t yona_platform_read_file_submit(const char* path);
int64_t yona_platform_write_file_submit(const char* path, const char* content);
int64_t yona_platform_read_file_bytes_submit(const char* path);
int64_t yona_platform_read_fd_bytes_submit(int fd, int64_t count, int64_t offset);
int64_t yona_platform_write_fd_bytes_submit(int fd, void* bytes, int64_t offset);
int64_t yona_platform_write_fd_str_submit(int fd, const char* s);
int64_t yona_platform_write_fd_strs_submit(int fd, const char* s1, const char* s2);
const char* yona_platform_read_line_fd(int fd);

/* ===== Filesystem ===== */

char* yona_platform_read_file(const char* path);
int yona_platform_write_file(const char* path, const char* content);
int yona_platform_append_file(const char* path, const char* content);
int yona_platform_file_exists(const char* path);
int yona_platform_remove_file(const char* path);
int64_t yona_platform_file_size(const char* path);
int64_t* yona_platform_list_dir(const char* path);
int64_t yona_platform_open_file_handle(const char* path, int64_t mode_tag);
int64_t yona_platform_close_file_handle(int fd);
int64_t yona_platform_seek_file_handle(int fd, int64_t offset, int64_t whence_tag);
int64_t yona_platform_tell_file_handle(int fd);
int64_t yona_platform_advance_file_handle(int fd, int64_t delta);
int64_t yona_platform_flush_file_handle(int fd);
int64_t yona_platform_truncate_file_handle(int fd, int64_t length);

/* ===== Console I/O ===== */

char* yona_platform_read_line(void);

/* ===== Process ===== */

char* yona_platform_getenv(const char* name);
char* yona_platform_getcwd(void);
char* yona_platform_exec(const char* cmd);
int64_t yona_platform_exec_status(const char* cmd);
int64_t yona_platform_setenv(const char* name, const char* value);
char* yona_platform_hostname(void);
int64_t yona_platform_exit_process(int64_t code);
char* yona_Std_Process__executablePath(void);
char* yona_Std_Process__tempDir(void);
char* yona_Std_Process__tempFile(const char* prefix, const char* suffix);
int64_t yona_Std_Process__run(const char* file, int64_t* argv_seq);
int64_t yona_Std_Process__execArgs(const char* file, int64_t* argv_seq);

void* yona_Std_Process__spawn(const char* cmd);
char* yona_Std_Process__readLine(void* proc);
char* yona_Std_Process__readAll(void* proc);
int64_t yona_Std_Process__wait(void* proc);
int64_t yona_Std_Process__kill(void* proc, int64_t signal);
int64_t yona_Std_Process__writeStdin(void* proc, const char* data);
int64_t yona_Std_Process__closeStdin(void* proc);
int64_t yona_Std_Process__pid(void* proc);
void yona_process_destroy(void* proc);

#endif /* YONA_PLATFORM_H */
