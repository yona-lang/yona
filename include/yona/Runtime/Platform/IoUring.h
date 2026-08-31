/*
 * io_uring shared infrastructure — used by file_linux.c, net_linux.c, etc.
 *
 * Raw syscall interface (no liburing dependency). Lazily initialized,
 * thread-safe via mutex. Supports submit-and-wait pattern via user_data IDs.
 *
 * Ring state and the io_ctx table live in src/Runtime/Platform/IoUringLinux.c
 * so every platform TU shares one ring. (A header-static singleton used to
 * give file/net/os each their own ring — net submit + file await SIGSEGV.)
 *
 * Header lives under include/yona/Runtime/Platform/; implementations consume
 * it from src/Runtime/Platform/ (Linux .c files only).
 */

#ifndef YONA_RUNTIME_PLATFORM_IOURING_H
#define YONA_RUNTIME_PLATFORM_IOURING_H

#include <linux/io_uring.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum YonaIoOperationKind {
  YonaIoOperationReadFile,
  YonaIoOperationWriteFile,
  YonaIoOperationAccept,
  YonaIoOperationConnect,
  YonaIoOperationSend,
  YonaIoOperationReceive,
  YonaIoOperationReceiveBytes,
  YonaIoOperationReadFileBytes,
  YonaIoOperationReadFileDescriptorBytes,
  YonaIoOperationWriteFileDescriptorBytes,
  YonaIoOperationWriteFileDescriptorString,
} YonaIoOperationKind;

typedef struct YonaIoContext {
  YonaIoOperationKind Kind;
  int FileDescriptor;
  char *Buffer;
  size_t BufferSize;
  int CloseFileDescriptor;
} YonaIoContext;

#define YONA_IO_CONTEXT_TABLE_SIZE 1024

uint64_t YonaRuntimeIoUringSubmit(struct io_uring_sqe *Submission);
int32_t YonaRuntimeIoUringAwait(uint64_t Id);
void YonaRuntimeIoUringCancel(uint64_t TargetId);
void YonaRuntimeIoUringCancelGroup(const uint64_t *IoIds, int Count);

void YonaRuntimeIoContextPut(uint64_t Id, YonaIoContext *Context);
YonaIoContext *YonaRuntimeIoContextTake(uint64_t Id);

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_PLATFORM_IOURING_H */
