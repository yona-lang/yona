/*
 * Platform abstraction layer for Yona runtime.
 *
 * Each function has a single portable interface. Implementations live in
 * src/Runtime/Platform/<Os>.c and are selected at build time.
 *
 * Adding a new platform file:
 *   1. Create src/Runtime/Platform/<Os>.c
 *   2. Implement all YonaRuntimePlatform*
 * functions declared here
 *   3. Append the file in CMakeLists.txt for the target OS
 *
 * Async/channel backends live next to platform TUs (`async_posix.c` /
 *
 * `async_win32.c`, `channel_posix.c` / `channel_win32.c`) and compile into the

 * * platform-selected runtime concurrency component.
 */

#ifndef YONA_RUNTIME_PLATFORM_API_H
#define YONA_RUNTIME_PLATFORM_API_H

#include <stddef.h>
#include <stdint.h>

/* ===== I/O await (io_uring user_data on Linux; IOCP / direct-result on
 * Windows) ===== */

int64_t YonaRuntimeIoAwait(int64_t IoId);
int64_t YonaRuntimeFileLineIteratorCreate(const char *Path);
int64_t YonaRuntimeFileChunkIteratorCreate(int64_t FileDescriptor,
                                           int64_t ChunkSize);

/* ===== Async File I/O (submit-and-return) ===== */

int64_t YonaRuntimePlatformSubmitFileRead(const char *Path);
int64_t YonaRuntimePlatformSubmitFileWrite(const char *Path,
                                           const char *Content);
int64_t YonaRuntimePlatformSubmitFileByteRead(const char *Path);
int64_t YonaRuntimePlatformSubmitFileDescriptorByteRead(int Fd, int64_t Count,
                                                        int64_t Offset);
int64_t YonaRuntimePlatformSubmitFileDescriptorByteWrite(int Fd, void *Bytes,
                                                         int64_t Offset);
int64_t YonaRuntimePlatformSubmitFileDescriptorStringWrite(int Fd,
                                                           const char *S);
int64_t YonaRuntimePlatformSubmitFileDescriptorStringsWrite(int Fd,
                                                            const char *S1,
                                                            const char *S2);
const char *YonaRuntimePlatformReadLineFromFileDescriptor(int Fd);

/* ===== Filesystem ===== */

char *YonaRuntimePlatformReadFile(const char *Path);
int YonaRuntimePlatformWriteFile(const char *Path, const char *Content);
int YonaRuntimePlatformAppendFile(const char *Path, const char *Content);
int YonaRuntimePlatformFileExists(const char *Path);
int YonaRuntimePlatformRemoveFile(const char *Path);
int64_t YonaRuntimePlatformFileSize(const char *Path);
int64_t *YonaRuntimePlatformListDirectory(const char *Path);
int64_t YonaRuntimePlatformOpenFileHandle(const char *Path, int64_t ModeTag);
int64_t YonaRuntimePlatformCloseFileHandle(int Fd);
int64_t YonaRuntimePlatformSeekFileHandle(int Fd, int64_t Offset,
                                          int64_t WhenceTag);
int64_t YonaRuntimePlatformTellFileHandle(int Fd);
int64_t YonaRuntimePlatformAdvanceFileHandle(int Fd, int64_t Delta);
int64_t YonaRuntimePlatformFlushFileHandle(int Fd);
int64_t YonaRuntimePlatformTruncateFileHandle(int Fd, int64_t Length);

/* ===== Console I/O ===== */

char *YonaRuntimePlatformReadLine(void);

/* ===== Process ===== */

char *YonaRuntimePlatformGetEnvironment(const char *Name);
char *YonaRuntimePlatformGetCurrentWorkingDirectory(void);
char *YonaRuntimePlatformExecute(const char *Executable,
                                 int64_t *ArgumentSequence);
int64_t YonaRuntimePlatformExecuteStatus(const char *Executable,
                                         int64_t *ArgumentSequence);
int64_t YonaRuntimePlatformSetEnvironment(const char *Name, const char *Value);
char *YonaRuntimePlatformHostName(void);
int64_t YonaRuntimePlatformExitProcess(int64_t Code);
char *YonaStdProcessExecutablePath(void);
char *YonaStdProcessTempDir(void);
char *YonaStdProcessTempFile(const char *Prefix, const char *Suffix);
int64_t YonaStdProcessRun(const char *File, int64_t *ArgumentSequence);
int64_t YonaStdProcessExecArgs(const char *File, int64_t *ArgumentSequence);

void *YonaStdProcessSpawn(const char *Executable, int64_t *ArgumentSequence);
char *YonaStdProcessReadLine(void *Proc);
char *YonaStdProcessReadAll(void *Proc);
int64_t YonaStdProcessWait(void *Proc);
int64_t YonaStdProcessKill(void *Proc, int64_t Signal);
int64_t YonaStdProcessWriteStdin(void *Proc, const char *Data);
int64_t YonaStdProcessCloseStdin(void *Proc);
int64_t YonaStdProcessPid(void *Proc);
void YonaRuntimeProcessDestroy(void *Proc);

#endif /* YONA_RUNTIME_PLATFORM_API_H */
