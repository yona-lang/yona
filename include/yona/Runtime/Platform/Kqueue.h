/*
 * kqueue async I/O — macOS counterpart to
 * include/yona/Runtime/Platform/IoUring.h.
 *
 * File ops run on a small worker pool and wake awaiters via a kqueue
 * EVFILT_READ pipe (submit-and-return, same ABI as io_uring IDs).
 * Socket ops use EVFILT_READ / EVFILT_WRITE readiness, then the syscall.
 *
 * Ring state and the io_ctx table live in src/Runtime/Platform/KqueueMacOs.c
 * so file/net/os TUs share one kqueue.
 */

#ifndef YONA_RUNTIME_PLATFORM_KQUEUE_H
#define YONA_RUNTIME_PLATFORM_KQUEUE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>

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

/* off == (off_t)-1 means the fd's current position (read/write, not
 * pread/pwrite). */
uint64_t YonaRuntimeKqueueSubmitRead(int FileDescriptor, void *Buffer,
                                     size_t Length, off_t Offset);
uint64_t YonaRuntimeKqueueSubmitWrite(int FileDescriptor, const void *Buffer,
                                      size_t Length, off_t Offset);
uint64_t YonaRuntimeKqueueSubmitConnect(int FileDescriptor, const void *Address,
                                        socklen_t AddressLength);
uint64_t YonaRuntimeKqueueSubmitAccept(int FileDescriptor, void *Address,
                                       socklen_t *AddressLength);
uint64_t YonaRuntimeKqueueSubmitSend(int FileDescriptor, const void *Buffer,
                                     size_t Length);
uint64_t YonaRuntimeKqueueSubmitReceive(int FileDescriptor, void *Buffer,
                                        size_t Length);
uint64_t YonaRuntimeKqueueSubmitNop(void);

int32_t YonaRuntimeKqueueAwait(uint64_t Id);
void YonaRuntimeKqueueCancel(uint64_t TargetId);
void YonaRuntimeKqueueCancelGroup(const uint64_t *IoIds, int Count);

void YonaRuntimeIoContextPut(uint64_t Id, YonaIoContext *Context);
YonaIoContext *YonaRuntimeIoContextTake(uint64_t Id);

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_PLATFORM_KQUEUE_H */
