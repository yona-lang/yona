/* Shared async I/O operation context and registry contract. */

#ifndef YONA_RUNTIME_PLATFORM_IOCONTEXT_H
#define YONA_RUNTIME_PLATFORM_IOCONTEXT_H

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

void YonaRuntimeIoContextPut(uint64_t Id, YonaIoContext *Context);
YonaIoContext *YonaRuntimeIoContextTake(uint64_t Id);

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_PLATFORM_IOCONTEXT_H */
