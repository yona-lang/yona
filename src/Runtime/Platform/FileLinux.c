/*
 * Linux file I/O — submit-and-return via io_uring.
 *
 * Each async function submits to io_uring and returns the user_data ID
 * immediately. Completion is handled by YonaRuntimeIoAwait() which does
 * type-specific post-processing (close fd, null-terminate buffer, etc).
 */

#include "yona/Runtime/Collections/Sequence.h"
#include "yona/Runtime/Core/Api.h"
#include "yona/Runtime/Platform/Api.h"
#include "yona/Runtime/Platform/IoUring.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ===== Generic io_uring completer ===== */

/* Sentinel: when io_uring is unavailable, IO functions store the direct
 * result in the io_ctx table with a special type. io_await returns it. */
#define YONA_IO_OP_DIRECT_RESULT 99
static _Atomic uint64_t DirectResultId =
    0x80000000ULL; /* high offset avoids uring ID collision */

/* Register a direct (blocking fallback) result for io_await. */
static int64_t registerDirectIoResult(void *Result) {
  uint64_t Id = atomic_fetch_add(&DirectResultId, 1);
  YonaIoContext *Ctx = (YonaIoContext *)malloc(sizeof(YonaIoContext));
  Ctx->Kind = YONA_IO_OP_DIRECT_RESULT;
  Ctx->FileDescriptor = -1;
  Ctx->Buffer = (char *)Result;
  Ctx->BufferSize = 0;
  Ctx->CloseFileDescriptor = 0;
  YonaRuntimeIoContextPut(Id, Ctx);
  return (int64_t)Id;
}

/* Exported for the runtime's native stdlib entry points. */
int64_t YonaRuntimeIoRegisterDirectResult(void *Result) {
  return registerDirectIoResult(Result);
}

int64_t YonaRuntimeIoAwait(int64_t IoId) {
  if (IoId <= 0)
    return 0;
  YonaIoContext *Ctx = YonaRuntimeIoContextTake((uint64_t)IoId);
  if (Ctx && Ctx->Kind == YONA_IO_OP_DIRECT_RESULT) {
    /* Blocking fallback: result is stored in buf pointer */
    int64_t Result = (int64_t)(intptr_t)Ctx->Buffer;
    free(Ctx);
    return Result;
  }
  if (Ctx)
    YonaRuntimeIoContextPut((uint64_t)IoId,
                            Ctx); /* put it back for real await */

  int32_t Res = YonaRuntimeIoUringAwait((uint64_t)IoId);

  /* Handle cancellation: clean up context and raise */
  if (Res == -125 /* ECANCELED */) {
    Ctx = YonaRuntimeIoContextTake((uint64_t)IoId);
    if (Ctx) {
      if (Ctx->Buffer)
        free(Ctx->Buffer);
      if (Ctx->CloseFileDescriptor && Ctx->FileDescriptor >= 0)
        close(Ctx->FileDescriptor);
      free(Ctx);
    }
    return 0; /* Cancelled — caller checks group error */
  }

  Ctx = YonaRuntimeIoContextTake((uint64_t)IoId);
  if (!Ctx)
    return (int64_t)Res;

  int64_t Result;
  switch (Ctx->Kind) {
  case YonaIoOperationReadFile:
    if (Res >= 0)
      Ctx->Buffer[Res] = '\0';
    else
      Ctx->Buffer[0] = '\0';
    if (Ctx->CloseFileDescriptor)
      close(Ctx->FileDescriptor);
    Result = (int64_t)(intptr_t)Ctx->Buffer;
    break;
  case YonaIoOperationWriteFile:
    if (Ctx->CloseFileDescriptor)
      close(Ctx->FileDescriptor);
    /* Unpin the content buffer (was rc_inc'd at submit) */
    if (Ctx->Buffer)
      YonaRuntimeRelease(Ctx->Buffer);
    Result = (Res == (int32_t)Ctx->BufferSize) ? 1 : 0;
    break;
  case YonaIoOperationAccept:
    free(Ctx->Buffer);
    Result = (Res >= 0) ? (int64_t)Res : -1;
    break;
  case YonaIoOperationConnect:
    free(Ctx->Buffer);
    Result = (Res >= 0) ? (int64_t)Ctx->FileDescriptor : -1;
    if (Res < 0)
      close(Ctx->FileDescriptor);
    break;
  case YonaIoOperationSend:
    /* Unpin the send buffer (was rc_inc'd at submit) */
    if (Ctx->Buffer)
      YonaRuntimeRelease(Ctx->Buffer);
    Result = (int64_t)Res;
    break;
  case YonaIoOperationReceive:
    if (Res > 0)
      Ctx->Buffer[Res] = '\0';
    else
      Ctx->Buffer[0] = '\0';
    Result = (int64_t)(intptr_t)Ctx->Buffer;
    break;
  case YonaIoOperationReceiveBytes: {
    /* Bytes: set length field, return the Bytes buffer */
    int64_t *BytesBuffer = (int64_t *)(intptr_t)Ctx->Buffer;
    BytesBuffer[0] = (Res > 0) ? (int64_t)Res : 0;
    Result = (int64_t)(intptr_t)BytesBuffer;
    break;
  }
  case YonaIoOperationReadFileBytes: {
    int64_t *BytesBuffer = (int64_t *)(intptr_t)Ctx->Buffer;
    BytesBuffer[0] = (Res > 0) ? (int64_t)Res : 0;
    if (Ctx->CloseFileDescriptor)
      close(Ctx->FileDescriptor);
    Result = (int64_t)(intptr_t)BytesBuffer;
    break;
  }
  case YonaIoOperationReadFileDescriptorBytes: {
    int64_t *BytesBuffer = (int64_t *)(intptr_t)Ctx->Buffer;
    BytesBuffer[0] = (Res > 0) ? (int64_t)Res : 0;
    /* Don't close fd — caller owns the handle */
    Result = (int64_t)(intptr_t)BytesBuffer;
    break;
  }
  case YonaIoOperationWriteFileDescriptorBytes: {
    /* Unpin the write buffer */
    if (Ctx->Buffer)
      YonaRuntimeRelease(Ctx->Buffer);
    /* Don't close fd — caller owns the handle */
    Result = (Res >= 0) ? (int64_t)Res : -1;
    break;
  }
  case YonaIoOperationWriteFileDescriptorString: {
    /* Caller allocated the concatenated string via malloc. Free
     * it now that the kernel has consumed it. */
    if (Ctx->Buffer)
      free(Ctx->Buffer);
    Result = (Res >= 0) ? (int64_t)Res : -1;
    break;
  }
  default:
    Result = (int64_t)Res;
    break;
  }
  free(Ctx);
  return Result;
}

/* ===== Submit-and-return functions ===== */

/* Blocking fallback: read entire file synchronously.
 * Used when io_uring is unavailable (ENOMEM, old kernel, container). */
static int64_t readFileBlocking(const char *Path) {
  int Fd = open(Path, O_RDONLY);
  if (Fd < 0) {
    char *Empty = (char *)YonaRuntimeAllocateString(1);
    Empty[0] = '\0';
    return (int64_t)(intptr_t)Empty;
  }
  struct stat St;
  if (fstat(Fd, &St) < 0) {
    close(Fd);
    char *E = (char *)YonaRuntimeAllocateString(1);
    E[0] = '\0';
    return (int64_t)(intptr_t)E;
  }
  size_t Size = (size_t)St.st_size;
  char *Buf = (char *)YonaRuntimeAllocateStringWithLength(Size + 1, Size);
  ssize_t N = read(Fd, Buf, Size);
  if (N >= 0)
    Buf[N] = '\0';
  else
    Buf[0] = '\0';
  close(Fd);
  return (int64_t)(intptr_t)Buf;
}

/* readFile: open, allocate buffer, submit uring read, return ID.
 * Uses YonaRuntimeAllocate_string_len to store the file size in the RC header,
 * enabling O(1) string length instead of O(n) strlen. */
int64_t YonaRuntimePlatformSubmitFileRead(const char *Path) {
  int Fd = open(Path, O_RDONLY);
  if (Fd < 0)
    return 0;
  struct stat St;
  if (fstat(Fd, &St) < 0) {
    close(Fd);
    return 0;
  }
  size_t Size = (size_t)St.st_size;
  char *Buf = (char *)YonaRuntimeAllocateStringWithLength(Size + 1, Size);

  YonaIoContext *Ctx = (YonaIoContext *)malloc(sizeof(YonaIoContext));
  Ctx->Kind = YonaIoOperationReadFile;
  Ctx->FileDescriptor = Fd;
  Ctx->Buffer = Buf;
  Ctx->BufferSize = Size;
  Ctx->CloseFileDescriptor = 1;

  struct io_uring_sqe Sqe;
  memset(&Sqe, 0, sizeof(Sqe));
  Sqe.opcode = IORING_OP_READ;
  Sqe.fd = Fd;
  Sqe.addr = (unsigned long)Buf;
  Sqe.len = (unsigned)Size;
  Sqe.off = 0;

  uint64_t Id = YonaRuntimeIoUringSubmit(&Sqe);
  if (Id == 0) {
    /* io_uring unavailable — fall back to blocking read */
    close(Fd);
    free(Ctx);
    return 0;
  }
  YonaRuntimeIoContextPut(Id, Ctx);
  return (int64_t)Id;
}

/* writeFile: open, submit uring write, return ID */
int64_t YonaRuntimePlatformSubmitFileWrite(const char *Path,
                                           const char *Content) {
  int Fd = open(Path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (Fd < 0)
    return 0;
  size_t Len = strlen(Content);

  /* Copy content to RC-managed buffer so it survives until I/O completes.
   * Cannot rc_inc the original — it may be a string constant without RC header.
   */
  char *Pinned = (char *)YonaRuntimeAllocateString(Len + 1);
  memcpy(Pinned, Content, Len + 1);

  YonaIoContext *Ctx = (YonaIoContext *)malloc(sizeof(YonaIoContext));
  Ctx->Kind = YonaIoOperationWriteFile;
  Ctx->FileDescriptor = Fd;
  Ctx->Buffer = Pinned; /* rc_dec'd in completer */
  Ctx->BufferSize = Len;
  Ctx->CloseFileDescriptor = 1;

  struct io_uring_sqe Sqe;
  memset(&Sqe, 0, sizeof(Sqe));
  Sqe.opcode = IORING_OP_WRITE;
  Sqe.fd = Fd;
  Sqe.addr = (unsigned long)Pinned; /* use RC-managed copy */
  Sqe.len = (unsigned)Len;
  Sqe.off = 0;

  uint64_t Id = YonaRuntimeIoUringSubmit(&Sqe);
  if (Id == 0) {
    close(Fd);
    YonaRuntimeRelease(Pinned);
    free(Ctx);
    return 0;
  }
  YonaRuntimeIoContextPut(Id, Ctx);
  return (int64_t)Id;
}

/* readFileBytes: like readFile but returns Bytes instead of String */
int64_t YonaRuntimePlatformSubmitFileByteRead(const char *Path) {
  int Fd = open(Path, O_RDONLY);
  if (Fd < 0)
    return 0;
  struct stat St;
  if (fstat(Fd, &St) < 0) {
    close(Fd);
    return 0;
  }
  size_t Size = (size_t)St.st_size;

  int64_t *Buf = (int64_t *)YonaRuntimeAllocate(8 /* RC_TYPE_BYTE_ARRAY */,
                                                sizeof(int64_t) + Size);
  Buf[0] = 0; /* length set by completer */

  YonaIoContext *Ctx = (YonaIoContext *)malloc(sizeof(YonaIoContext));
  Ctx->Kind = YonaIoOperationReadFileBytes;
  Ctx->FileDescriptor = Fd;
  Ctx->Buffer = (char *)Buf;
  Ctx->BufferSize = Size;
  Ctx->CloseFileDescriptor = 1;

  struct io_uring_sqe Sqe;
  memset(&Sqe, 0, sizeof(Sqe));
  Sqe.opcode = IORING_OP_READ;
  Sqe.fd = Fd;
  Sqe.addr = (unsigned long)(uint8_t *)(Buf + 1);
  Sqe.len = (unsigned)Size;
  Sqe.off = 0;

  uint64_t Id = YonaRuntimeIoUringSubmit(&Sqe);
  if (Id == 0) {
    close(Fd);
    free(Ctx);
    return 0;
  }
  YonaRuntimeIoContextPut(Id, Ctx);
  return (int64_t)Id;
}

/* ===== Handle-based binary I/O submit functions ===== */

/* pread from open fd at given offset. Does NOT close fd. */
int64_t YonaRuntimePlatformSubmitFileDescriptorByteRead(int Fd, int64_t Count,
                                                        int64_t Offset) {
  int64_t *Buf = (int64_t *)YonaRuntimeAllocate(
      8 /* RC_TYPE_BYTE_ARRAY */, sizeof(int64_t) + (size_t)Count);
  Buf[0] = 0; /* length set by completer */

  struct io_uring_sqe Sqe;
  memset(&Sqe, 0, sizeof(Sqe));
  Sqe.opcode = IORING_OP_READ;
  Sqe.fd = Fd;
  Sqe.addr = (unsigned long)(uint8_t *)(Buf + 1);
  Sqe.len = (unsigned)Count;
  Sqe.off = (uint64_t)Offset; /* pread semantics */

  uint64_t Id = YonaRuntimeIoUringSubmit(&Sqe);
  if (Id == 0) {
    /* Fallback: blocking pread */
    ssize_t N =
        pread(Fd, (uint8_t *)(Buf + 1), (size_t)Count, (off_t)Offset);
    Buf[0] = N > 0 ? N : 0;
    return registerDirectIoResult(Buf);
  }

  YonaIoContext *Ctx = (YonaIoContext *)malloc(sizeof(YonaIoContext));
  Ctx->Kind = YonaIoOperationReadFileDescriptorBytes;
  Ctx->FileDescriptor = Fd;
  Ctx->Buffer = (char *)Buf;
  Ctx->BufferSize = (size_t)Count;
  Ctx->CloseFileDescriptor = 0; /* caller owns the handle */
  YonaRuntimeIoContextPut(Id, Ctx);
  return (int64_t)Id;
}

/* pwrite to open fd at given offset. Does NOT close fd. */
int64_t YonaRuntimePlatformSubmitFileDescriptorByteWrite(int Fd, void *Bytes,
                                                         int64_t Offset) {
  int64_t *B = (int64_t *)Bytes;
  int64_t Len = B[0];
  uint8_t *Data = (uint8_t *)(B + 1);

  /* Pin the bytes buffer — rc_inc so it stays alive during async I/O */
  YonaRuntimeRetain(Bytes);

  struct io_uring_sqe Sqe;
  memset(&Sqe, 0, sizeof(Sqe));
  Sqe.opcode = IORING_OP_WRITE;
  Sqe.fd = Fd;
  Sqe.addr = (unsigned long)Data;
  Sqe.len = (unsigned)Len;
  Sqe.off = (uint64_t)Offset; /* pwrite semantics */

  uint64_t Id = YonaRuntimeIoUringSubmit(&Sqe);
  if (Id == 0) {
    /* Fallback: blocking pwrite */
    ssize_t N = pwrite(Fd, Data, (size_t)Len, (off_t)Offset);
    YonaRuntimeRelease(Bytes);
    return registerDirectIoResult((void *)(intptr_t)(N >= 0 ? N : -1));
  }

  YonaIoContext *Ctx = (YonaIoContext *)malloc(sizeof(YonaIoContext));
  Ctx->Kind = YonaIoOperationWriteFileDescriptorBytes;
  Ctx->FileDescriptor = Fd;
  Ctx->Buffer = (char *)Bytes; /* rc_dec'd in completer */
  Ctx->BufferSize = (size_t)Len;
  Ctx->CloseFileDescriptor = 0;
  YonaRuntimeIoContextPut(Id, Ctx);
  return (int64_t)Id;
}

/* ===== Synchronous fallbacks ===== */

char *YonaRuntimePlatformReadFile(const char *Path) {
  FILE *F = fopen(Path, "rb");
  if (!F) {
    char *R = (char *)YonaRuntimeAllocateString(1);
    R[0] = '\0';
    return R;
  }
  fseek(F, 0, SEEK_END);
  long Size = ftell(F);
  fseek(F, 0, SEEK_SET);
  if (Size < 0)
    Size = 0;
  char *Buf = (char *)YonaRuntimeAllocateString((size_t)Size + 1);
  size_t Rd = fread(Buf, 1, (size_t)Size, F);
  Buf[Rd] = '\0';
  fclose(F);
  return Buf;
}

int YonaRuntimePlatformWriteFile(const char *Path, const char *Content) {
  FILE *F = fopen(Path, "wb");
  if (!F)
    return -1;
  size_t Len = strlen(Content);
  size_t W = fwrite(Content, 1, Len, F);
  fclose(F);
  return (W == Len) ? 0 : -1;
}

int YonaRuntimePlatformAppendFile(const char *Path, const char *Content) {
  FILE *F = fopen(Path, "ab");
  if (!F)
    return -1;
  size_t Len = strlen(Content);
  size_t W = fwrite(Content, 1, Len, F);
  fclose(F);
  return (W == Len) ? 0 : -1;
}

int YonaRuntimePlatformFileExists(const char *Path) {
  struct stat St;
  return (stat(Path, &St) == 0) ? 1 : 0;
}

int YonaRuntimePlatformRemoveFile(const char *Path) {
  return (remove(Path) == 0) ? 0 : -1;
}

int64_t YonaRuntimePlatformFileSize(const char *Path) {
  struct stat St;
  if (stat(Path, &St) != 0)
    return -1;
  return (int64_t)St.st_size;
}

int64_t YonaRuntimePlatformOpenFileHandle(const char *Path, int64_t ModeTag) {
  int Flags = O_RDONLY;
  if (ModeTag == 1)
    Flags = O_WRONLY | O_CREAT | O_TRUNC; /* Write */
  else if (ModeTag == 2)
    Flags = O_RDWR | O_CREAT; /* ReadWrite */
  else if (ModeTag == 3)
    Flags = O_WRONLY | O_CREAT | O_APPEND; /* Append */
  return (int64_t)open(Path, Flags, 0644);
}

int64_t YonaRuntimePlatformCloseFileHandle(int Fd) {
  return (int64_t)close(Fd);
}

int64_t YonaRuntimePlatformSeekFileHandle(int Fd, int64_t Offset,
                                          int64_t WhenceTag) {
  int Whence = SEEK_SET;
  if (WhenceTag == 1)
    Whence = SEEK_CUR;
  else if (WhenceTag == 2)
    Whence = SEEK_END;
  return (int64_t)lseek(Fd, (off_t)Offset, Whence);
}

int64_t YonaRuntimePlatformTellFileHandle(int Fd) {
  return (int64_t)lseek(Fd, 0, SEEK_CUR);
}

int64_t YonaRuntimePlatformAdvanceFileHandle(int Fd, int64_t Delta) {
  return (int64_t)lseek(Fd, (off_t)Delta, SEEK_CUR);
}

int64_t YonaRuntimePlatformFlushFileHandle(int Fd) {
  return fsync(Fd) == 0 ? 1 : 0;
}

int64_t YonaRuntimePlatformTruncateFileHandle(int Fd, int64_t Length) {
  return ftruncate(Fd, (off_t)Length) == 0 ? 1 : 0;
}

int64_t *YonaRuntimePlatformListDirectory(const char *Path) {
  DIR *Dir = opendir(Path);
  if (!Dir)
    return YonaRuntimeSequenceAllocate(0);
  int64_t Count = 0;
  struct dirent *Entry;
  while ((Entry = readdir(Dir)) != NULL) {
    if (Entry->d_name[0] == '.' &&
        (Entry->d_name[1] == '\0' ||
         (Entry->d_name[1] == '.' && Entry->d_name[2] == '\0')))
      continue;
    Count++;
  }
  rewinddir(Dir);
  int64_t *Seq = YonaRuntimeSequenceAllocate(Count);
  int64_t I = 0;
  while ((Entry = readdir(Dir)) != NULL) {
    if (Entry->d_name[0] == '.' &&
        (Entry->d_name[1] == '\0' ||
         (Entry->d_name[1] == '.' && Entry->d_name[2] == '\0')))
      continue;
    size_t Len = strlen(Entry->d_name);
    char *Name = (char *)YonaRuntimeAllocateString(Len + 1);
    memcpy(Name, Entry->d_name, Len + 1);
    YonaRuntimeSequenceSet(Seq, I, (int64_t)(intptr_t)Name);
    I++;
  }
  YonaRuntimeSequenceSetHeap(Seq, 1);
  closedir(Dir);
  return Seq;
}

/* ===== Streaming File Line Iterator ===== */
/* Reads a file line-by-line with 64KB buffered I/O.
 * Returns an Iterator (closure that yields Option String).
 * Memory: O(64KB buffer + one line) instead of O(file_size). */

#define YONA_LINE_ITER_BUF_SIZE 65536

/* Forward declarations for runtime functions used by the iterator */

typedef struct {
  void (*Finalize)(void *);
  int Fd;
  char *Buf;
  size_t BufferPosition;
  size_t BufferLength;
  int Eof;
} YonaLineIteratorState;

static void finalizeLineIterator(void *Raw) {
  YonaLineIteratorState *St = (YonaLineIteratorState *)Raw;
  if (St->Fd >= 0)
    close(St->Fd);
  free(St->Buf);
}

/* Read the next line from the buffered file iterator.
 * Returns an Option: Some(line_string) or None (as heap ADT).
 * Option layout: [rc, tag_encoded, tag_i64, value_i64] where tag=0 is Some,
 * tag=1 is None. */
static int64_t advanceLineIterator(int64_t *ClosureEnvironment) {
  /* closure_env layout: [fn_ptr, ret_tag, arity, num_caps, heap_mask,
   * borrow_mask, cap0] cap0 = pointer to YonaLineIteratorState */
  YonaLineIteratorState *St =
      (YonaLineIteratorState *)(intptr_t)ClosureEnvironment[6];

  if (St->Eof) {
    /* Return None: ADT layout [tag, num_fields, heap_mask] */
    int64_t *Adt = (int64_t *)YonaRuntimeAllocate(4 /* RC_TYPE_ADT */,
                                                  3 * sizeof(int64_t));
    Adt[0] = 1; /* tag = None */
    Adt[1] = 0; /* num_fields = 0 */
    Adt[2] = 0; /* heap_mask = 0 */
    return (int64_t)(intptr_t)Adt;
  }

  /* Scan buffer for newline, refill if needed */
  char LineBuffer[8192];
  size_t LineLength = 0;

  while (1) {
    /* Refill buffer if empty */
    if (St->BufferPosition >= St->BufferLength) {
      ssize_t N = read(St->Fd, St->Buf, YONA_LINE_ITER_BUF_SIZE);
      if (N <= 0) {
        St->Eof = 1;
        if (LineLength == 0) {
          /* EOF with no partial line — return None */
          int64_t *Adt = (int64_t *)YonaRuntimeAllocate(4, 3 * sizeof(int64_t));
          Adt[0] = 1;
          Adt[1] = 0;
          Adt[2] = 0;
          return (int64_t)(intptr_t)Adt;
        }
        break; /* Return the partial line */
      }
      St->BufferLength = (size_t)N;
      St->BufferPosition = 0;
    }

    /* Scan for newline in current buffer */
    while (St->BufferPosition < St->BufferLength) {
      char C = St->Buf[St->BufferPosition++];
      if (C == '\n')
        goto LineComplete;
      if (LineLength < sizeof(LineBuffer) - 1)
        LineBuffer[LineLength++] = C;
    }
  }

LineComplete:
  LineBuffer[LineLength] = '\0';

  /* Allocate RC string for the line */
  char *LineString =
      (char *)YonaRuntimeAllocateStringWithLength(LineLength + 1, LineLength);
  memcpy(LineString, LineBuffer, LineLength + 1);

  /* Return Some(line_str) — ADT layout [tag, num_fields, heap_mask, field0] */
  int64_t *Adt =
      (int64_t *)YonaRuntimeAllocate(4 /* RC_TYPE_ADT */, 4 * sizeof(int64_t));
  Adt[0] = 0; /* tag = Some */
  Adt[1] = 1; /* num_fields = 1 */
  Adt[2] = 1; /* heap_mask = bit 0 (field 0 is heap-allocated string) */
  Adt[3] = (int64_t)(intptr_t)LineString;
  return (int64_t)(intptr_t)Adt;
}

/* Create a streaming line iterator for a file.
 * Returns an Iterator ADT wrapping the next-line closure. */
int64_t YonaRuntimeFileLineIteratorCreate(const char *Path) {
  int Fd = open(Path, O_RDONLY);

  /* Allocate state */
  YonaLineIteratorState *St =
      (YonaLineIteratorState *)YonaRuntimeNativeStateAllocate(
          sizeof(YonaLineIteratorState), finalizeLineIterator);
  if (Fd < 0) {
    /* File not found — empty iterator */
    St->Fd = -1;
    St->Buf = NULL;
    St->BufferPosition = 0;
    St->BufferLength = 0;
    St->Eof = 1;
  } else {
    St->Fd = Fd;
    St->Buf = (char *)malloc(YONA_LINE_ITER_BUF_SIZE);
    St->BufferPosition = 0;
    St->BufferLength = 0;
    St->Eof = 0;
  }
  /* Create a closure that captures the state.
   * Closure layout: [fn_ptr, ret_tag, arity, num_caps, heap_mask, borrow_mask,
   * cap0, ...] */
  int64_t *Closure = (int64_t *)YonaRuntimeClosureCreate(
      (void *)advanceLineIterator, 0, /* ret_tag */
      0, /* arity: 0 explicit args — () -> Option a */
      1  /* num_caps: 1 captured value (the state pointer) */
  );
  /* Set cap0 = state pointer */
  YonaRuntimeClosureSetCapture(Closure, 0, (int64_t)(intptr_t)St);
  YonaRuntimeClosureSetHeapMask(Closure, 1);

  /* Wrap in Iterator ADT: [tag=0, num_fields=1, heap_mask=1, closure_ptr] */
  int64_t *IteratorAdt =
      (int64_t *)YonaRuntimeAllocate(4 /* RC_TYPE_ADT */, 4 * sizeof(int64_t));
  IteratorAdt[0] = 0; /* tag = Iterator */
  IteratorAdt[1] = 1; /* num_fields */
  IteratorAdt[2] = 1;
  IteratorAdt[3] = (int64_t)(intptr_t)Closure;

  return (int64_t)(intptr_t)IteratorAdt;
}

/* ===== Binary Chunk Iterator ===== */
/* Reads fixed-size chunks from an open fd using pread. */

typedef struct {
  void (*Finalize)(void *);
  int Fd;
  int64_t Position;
  int64_t ChunkSize;
  int Eof;
} YonaChunkIteratorState;

static int64_t advanceChunkIterator(int64_t *Env) {
  YonaChunkIteratorState *St = (YonaChunkIteratorState *)(intptr_t)Env[6];
  if (St->Eof) {
    int64_t *None = (int64_t *)YonaRuntimeAllocate(4, 3 * sizeof(int64_t));
    None[0] = 1;
    None[1] = 0;
    None[2] = 0;
    return (int64_t)(intptr_t)None;
  }

  /* Submit non-blocking pread via io_uring */
  int64_t *Buf = (int64_t *)YonaRuntimeAllocate(
      8 /* RC_TYPE_BYTE_ARRAY */, sizeof(int64_t) + (size_t)St->ChunkSize);
  Buf[0] = 0;

  struct io_uring_sqe Sqe;
  memset(&Sqe, 0, sizeof(Sqe));
  Sqe.opcode = IORING_OP_READ;
  Sqe.fd = St->Fd;
  Sqe.addr = (unsigned long)(uint8_t *)(Buf + 1);
  Sqe.len = (unsigned)St->ChunkSize;
  Sqe.off = (uint64_t)St->Position;

  uint64_t Id = YonaRuntimeIoUringSubmit(&Sqe);
  ssize_t N;
  if (Id == 0) {
    /* Fallback: blocking pread */
    N = pread(St->Fd, (uint8_t *)(Buf + 1), (size_t)St->ChunkSize,
              (off_t)St->Position);
  } else {
    N = (ssize_t)YonaRuntimeIoUringAwait(Id);
  }

  if (N <= 0) {
    St->Eof = 1;
    YonaRuntimeRelease(Buf);
    int64_t *None = (int64_t *)YonaRuntimeAllocate(4, 3 * sizeof(int64_t));
    None[0] = 1;
    None[1] = 0;
    None[2] = 0;
    return (int64_t)(intptr_t)None;
  }
  Buf[0] = N;
  St->Position += N;

  /* Some(bytes) */
  int64_t *Some = (int64_t *)YonaRuntimeAllocate(4, 4 * sizeof(int64_t));
  Some[0] = 0;
  Some[1] = 1;
  Some[2] = 1; /* tag=Some, 1 field, heap_mask=1 */
  Some[3] = (int64_t)(intptr_t)Buf;
  return (int64_t)(intptr_t)Some;
}

/* readChunks: create a streaming binary chunk iterator for an open fd.
 * Does NOT close the fd — caller owns the handle. */
int64_t YonaRuntimeFileChunkIteratorCreate(int64_t FileDescriptor,
                                           int64_t ChunkSize) {
  YonaChunkIteratorState *St =
      (YonaChunkIteratorState *)YonaRuntimeNativeStateAllocate(
          sizeof(YonaChunkIteratorState), NULL);
  St->Fd = (int)FileDescriptor;
  St->Position = 0;
  St->ChunkSize = ChunkSize;
  St->Eof = 0;

  int64_t *Closure = (int64_t *)YonaRuntimeClosureCreate(
      (void *)advanceChunkIterator, 0, 0, 1);
  YonaRuntimeClosureSetCapture(Closure, 0, (int64_t)(intptr_t)St);
  YonaRuntimeClosureSetHeapMask(Closure, 1);

  int64_t *IteratorAdt = (int64_t *)YonaRuntimeAllocate(4, 4 * sizeof(int64_t));
  IteratorAdt[0] = 0;
  IteratorAdt[1] = 1;
  IteratorAdt[2] = 1;
  IteratorAdt[3] = (int64_t)(intptr_t)Closure;
  return (int64_t)(intptr_t)IteratorAdt;
}

/* ===== Std\Io support ===== */

/* Submit a write of one or two string chunks to an open fd. The write
 * goes to the file's current position (no pwrite — writes to TTYs or
 * pipes don't support offsets). We concatenate chunks into one buffer
 * so a single IORING_OP_WRITE covers it; the buffer is free'd by the
 * completer via YonaIoOperationWriteFileDescriptorString. Returns uring
 * user_data ID.
 *
 * Falls back to blocking write() when io_uring is unavailable. */
static int64_t submitFileDescriptorStringsWrite(int Fd, const char *S1,
                                                const char *S2) {
  size_t L1 = S1 ? strlen(S1) : 0;
  size_t L2 = S2 ? strlen(S2) : 0;
  size_t Total = L1 + L2;
  char *Buf = (char *)malloc(Total + 1);
  if (L1)
    memcpy(Buf, S1, L1);
  if (L2)
    memcpy(Buf + L1, S2, L2);
  Buf[Total] = '\0';

  YonaIoContext *Ctx = (YonaIoContext *)malloc(sizeof(YonaIoContext));
  Ctx->Kind = YonaIoOperationWriteFileDescriptorString;
  Ctx->FileDescriptor = Fd;
  Ctx->Buffer = Buf;
  Ctx->BufferSize = Total;
  Ctx->CloseFileDescriptor = 0;

  struct io_uring_sqe Sqe;
  memset(&Sqe, 0, sizeof(Sqe));
  Sqe.opcode = IORING_OP_WRITE;
  Sqe.fd = Fd;
  Sqe.addr = (unsigned long)Buf;
  Sqe.len = (unsigned)Total;
  Sqe.off = (uint64_t)-1; /* -1 = use fd's current position (no pwrite) */

  uint64_t Id = YonaRuntimeIoUringSubmit(&Sqe);
  if (Id == 0) {
    /* Fallback: blocking write */
    ssize_t N = write(Fd, Buf, Total);
    free(Buf);
    free(Ctx);
    return registerDirectIoResult((void *)(intptr_t)(N >= 0 ? N : -1));
  }
  YonaRuntimeIoContextPut(Id, Ctx);
  return (int64_t)Id;
}

int64_t YonaRuntimePlatformSubmitFileDescriptorStringWrite(int Fd,
                                                           const char *S) {
  return submitFileDescriptorStringsWrite(Fd, S, NULL);
}

int64_t YonaRuntimePlatformSubmitFileDescriptorStringsWrite(int Fd,
                                                            const char *S1,
                                                            const char *S2) {
  return submitFileDescriptorStringsWrite(Fd, S1, S2);
}

/* Blocking line read. Called from the thread pool (AFN) so the calling
 * task isn't stalled. Returns a heap-allocated string (including the
 * trailing '\n' stripped) on success, NULL on EOF or error. */

const char *YonaRuntimePlatformReadLineFromFileDescriptor(int Fd) {
  /* Read up to 8 KB looking for '\n'. For stdin/TTY this is usually
   * one syscall; for pipes it may take several short reads. We grow
   * the buffer when needed. */
  size_t Cap = 512;
  size_t Len = 0;
  char *Buf = (char *)malloc(Cap);
  for (;;) {
    if (Len + 1 >= Cap) {
      Cap *= 2;
      Buf = (char *)realloc(Buf, Cap);
    }
    char Ch;
    ssize_t N = read(Fd, &Ch, 1);
    if (N <= 0) {
      if (Len == 0) {
        free(Buf);
        return NULL;
      } /* EOF with no data */
      break;
    }
    if (Ch == '\n')
      break;
    if (Ch == '\r')
      continue; /* swallow CR — CRLF and stray CR */
    Buf[Len++] = Ch;
  }
  char *Out = (char *)YonaRuntimeAllocateString(Len + 1);
  memcpy(Out, Buf, Len);
  Out[Len] = '\0';
  free(Buf);
  return Out;
}
