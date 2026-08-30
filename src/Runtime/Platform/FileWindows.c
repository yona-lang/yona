/*
 * Windows file I/O — Phase 2 (incremental): whole-file read uses an I/O
 * completion port + worker thread for overlapped ReadFile; other submits still
 * use direct-result IDs. Falls back to synchronous read if IOCP setup or
 * overlapped open fails.
 */

#ifndef _WIN32
#error "file_windows.c is for Windows builds only"
#endif

#ifndef _CRT_DECLARE_NONSTDC_NAMES
#define _CRT_DECLARE_NONSTDC_NAMES 1
#endif

#include "yona/Runtime/Collections/Sequence.h"
#include "yona/Runtime/Core/Api.h"
#include "yona/Runtime/Platform/Api.h"

#include <fcntl.h>
#include <io.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <windows.h>

#define YONA_IO_OP_DIRECT_RESULT 99

enum IoOpType {
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
  /* Whole-file async read: ctx lives inside YonaWindowsReadOperation; close_fd
     == 2 marker */
  IoOpReadFileIocpPending,
  /* Generic blocking op offload: ctx lives inside YonaWindowsBlockingOperation.
   */
  IoOpWiN32BlockingPending,
  /* Net overlapped/IOCP ops: ctx lives inside yona_win_net_op (net_windows.c).
   */
  IoOpNetIocpPending,
};

typedef struct {
  enum IoOpType Type;
  int Fd;
  char *Buf;
  size_t BufSize;
  int CloseFd;
} YonaIoContext;

#define YONA_IO_CONTEXT_TABLE_SIZE 1024
static struct {
  uint64_t Id;
  YonaIoContext *Ctx;
} IoCtxTable[YONA_IO_CONTEXT_TABLE_SIZE];
static CRITICAL_SECTION IoCtxMutex;
static INIT_ONCE IoCtxInitOnce = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK ioCtxInitCb(PINIT_ONCE O, PVOID P, PVOID *C) {
  (void)O;
  (void)P;
  (void)C;
  InitializeCriticalSection(&IoCtxMutex);
  return TRUE;
}

static void ioCtxEnsureInit(void) {
  InitOnceExecuteOnce(&IoCtxInitOnce, ioCtxInitCb, NULL, NULL);
}

static void YonaRuntimeIoContextPut(uint64_t Id, YonaIoContext *Ctx) {
  ioCtxEnsureInit();
  EnterCriticalSection(&IoCtxMutex);
  unsigned Idx = (unsigned)(Id % YONA_IO_CONTEXT_TABLE_SIZE);
  for (unsigned I = 0; I < YONA_IO_CONTEXT_TABLE_SIZE; I++) {
    unsigned Slot = (Idx + I) % YONA_IO_CONTEXT_TABLE_SIZE;
    if (IoCtxTable[Slot].Id == 0) {
      IoCtxTable[Slot].Id = Id;
      IoCtxTable[Slot].Ctx = Ctx;
      LeaveCriticalSection(&IoCtxMutex);
      return;
    }
  }
  LeaveCriticalSection(&IoCtxMutex);
}

static YonaIoContext *YonaRuntimeIoContextTake(uint64_t Id) {
  ioCtxEnsureInit();
  EnterCriticalSection(&IoCtxMutex);
  unsigned Idx = (unsigned)(Id % YONA_IO_CONTEXT_TABLE_SIZE);
  for (unsigned I = 0; I < YONA_IO_CONTEXT_TABLE_SIZE; I++) {
    unsigned Slot = (Idx + I) % YONA_IO_CONTEXT_TABLE_SIZE;
    if (IoCtxTable[Slot].Id == Id) {
      YonaIoContext *Ctx = IoCtxTable[Slot].Ctx;
      IoCtxTable[Slot].Id = 0;
      IoCtxTable[Slot].Ctx = NULL;
      LeaveCriticalSection(&IoCtxMutex);
      return Ctx;
    }
    if (IoCtxTable[Slot].Id == 0)
      break;
  }
  LeaveCriticalSection(&IoCtxMutex);
  return NULL;
}

static volatile LONG64 DirectResultIdSeq = (LONG64)0x80000000LL;

static int64_t ioRegisterDirectResult(void *Result) {
  uint64_t Id = (uint64_t)InterlockedExchangeAdd64(&DirectResultIdSeq, 1);
  YonaIoContext *Ctx = (YonaIoContext *)malloc(sizeof(YonaIoContext));
  Ctx->Type = (enum IoOpType)YONA_IO_OP_DIRECT_RESULT;
  Ctx->Fd = -1;
  Ctx->Buf = (char *)Result;
  Ctx->BufSize = 0;
  Ctx->CloseFd = 0;
  YonaRuntimeIoContextPut(Id, Ctx);
  return (int64_t)Id;
}

int64_t YonaRuntimeIoRegisterDirectResult(void *Result) {
  return ioRegisterDirectResult(Result);
}

typedef struct YonaWinReadOp {
  OVERLAPPED Ov;
  HANDLE HFile;
  HANDLE HDone;
  YonaIoContext Ctx;
} YonaWindowsReadOperation;

#define YONA_WIN_READ_OP_FROM_CTX(c)                                           \
  ((YonaWindowsReadOperation *)((uintptr_t)(c) -                               \
                                offsetof(YonaWindowsReadOperation, Ctx)))

typedef int64_t (*YonaWindowsBlockingFunction)(void *Arg);
typedef void (*YonaWindowsBlockingCleanupFunction)(void *Arg);

typedef struct YonaWinBlockingOp {
  HANDLE HDone;
  HANDLE HThread;
  void *Arg;
  YonaWindowsBlockingFunction Fn;
  YonaWindowsBlockingCleanupFunction Cleanup;
  YonaIoContext Ctx;
} YonaWindowsBlockingOperation;

#define YONA_WIN_BLOCKING_OP_FROM_CTX(c)                                       \
  ((YonaWindowsBlockingOperation *)((uintptr_t)(c) -                           \
                                    offsetof(YonaWindowsBlockingOperation,     \
                                             Ctx)))

static DWORD WINAPI yonaWinBlockingWorker(void *P) {
  YonaWindowsBlockingOperation *Op = (YonaWindowsBlockingOperation *)P;
  int64_t Result = 0;
  if (Op && Op->Fn)
    Result = Op->Fn(Op->Arg);
  if (Op && Op->Cleanup)
    Op->Cleanup(Op->Arg);
  if (Op) {
    Op->Ctx.Buf = (char *)(intptr_t)Result;
    Op->Ctx.Type = (enum IoOpType)YONA_IO_OP_DIRECT_RESULT;
    SetEvent(Op->HDone);
  }
  return 0;
}

static HANDLE IocpPort;
static HANDLE IocpThread;
static INIT_ONCE IocpOnce = INIT_ONCE_STATIC_INIT;

static DWORD WINAPI yonaIocpWorker(void *Unused) {
  (void)Unused;
  for (;;) {
    DWORD Nbytes = 0;
    ULONG_PTR Key = 0;
    LPOVERLAPPED Ov = NULL;
    BOOL Gqc =
        GetQueuedCompletionStatus(IocpPort, &Nbytes, &Key, &Ov, INFINITE);
    (void)Gqc;
    if (!Key || !Ov)
      continue;
    YonaWindowsReadOperation *Op = (YonaWindowsReadOperation *)Key;
    if (Ov != &Op->Ov)
      continue;
    DWORD Xfer = 0;
    if (!GetOverlappedResult(Op->HFile, Ov, &Xfer, FALSE))
      Xfer = 0;
    if (Op->Ctx.Buf) {
      if (Xfer > 0 && Xfer <= Op->Ctx.BufSize)
        Op->Ctx.Buf[Xfer] = '\0';
      else
        Op->Ctx.Buf[0] = '\0';
    }
    Op->Ctx.Type = YonaIoOperationReadFile;
    SetEvent(Op->HDone);
  }
}

static BOOL CALLBACK yonaIocpInitCb(PINIT_ONCE O, PVOID P, PVOID *C) {
  (void)O;
  (void)P;
  (void)C;
  IocpPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
  if (!IocpPort)
    return TRUE;
  IocpThread = CreateThread(NULL, 0, yonaIocpWorker, NULL, 0, NULL);
  if (!IocpThread) {
    CloseHandle(IocpPort);
    IocpPort = NULL;
  }
  return TRUE;
}

static void yonaIocpEnsure(void) {
  InitOnceExecuteOnce(&IocpOnce, yonaIocpInitCb, NULL, NULL);
}

/* Shared id registration hook for other Windows platform TUs (net_windows.c).
 */
int64_t YonaRuntimeWindowsRegisterIoContext(YonaIoContext *Ctx) {
  if (!Ctx)
    return 0;
  uint64_t Id = (uint64_t)InterlockedExchangeAdd64(&DirectResultIdSeq, 1);
  YonaRuntimeIoContextPut(Id, Ctx);
  return (int64_t)Id;
}

static int ioCtxFindSlotLocked(uint64_t Id) {
  unsigned Idx = (unsigned)(Id % YONA_IO_CONTEXT_TABLE_SIZE);
  for (unsigned I = 0; I < YONA_IO_CONTEXT_TABLE_SIZE; I++) {
    unsigned Slot = (Idx + I) % YONA_IO_CONTEXT_TABLE_SIZE;
    if (IoCtxTable[Slot].Id == Id)
      return (int)Slot;
    if (IoCtxTable[Slot].Id == 0)
      break;
  }
  return -1;
}

int64_t YonaRuntimeIoAwait(int64_t IoId) {
  if (IoId <= 0)
    return 0;
  for (;;) {
    EnterCriticalSection(&IoCtxMutex);
    int Slot = ioCtxFindSlotLocked((uint64_t)IoId);
    if (Slot < 0) {
      LeaveCriticalSection(&IoCtxMutex);
      return 0;
    }
    YonaIoContext *Ctx = IoCtxTable[Slot].Ctx;
    if (Ctx->Type == IoOpReadFileIocpPending) {
      LeaveCriticalSection(&IoCtxMutex);
      YonaWindowsReadOperation *Op = YONA_WIN_READ_OP_FROM_CTX(Ctx);
      WaitForSingleObject(Op->HDone, INFINITE);
      continue;
    }
    if (Ctx->Type == IoOpWiN32BlockingPending) {
      LeaveCriticalSection(&IoCtxMutex);
      YonaWindowsBlockingOperation *Op = YONA_WIN_BLOCKING_OP_FROM_CTX(Ctx);
      WaitForSingleObject(Op->HDone, INFINITE);
      continue;
    }
    if (Ctx->CloseFd == 7 &&
        Ctx->Type != (enum IoOpType)YONA_IO_OP_DIRECT_RESULT) {
      LeaveCriticalSection(&IoCtxMutex);
      /* net_windows.c stores HANDLE wait-event in buf_size while pending. */
      HANDLE H = (HANDLE)(uintptr_t)Ctx->BufSize;
      if (H) {
        WaitForSingleObject(H, INFINITE);
      } else {
        /* Defensive fallback: avoid spinning forever on malformed ctx. */
        return 0;
      }
      continue;
    }
    IoCtxTable[Slot].Id = 0;
    IoCtxTable[Slot].Ctx = NULL;
    LeaveCriticalSection(&IoCtxMutex);

    if (Ctx->Type == (enum IoOpType)YONA_IO_OP_DIRECT_RESULT) {
      int64_t Result = (int64_t)(intptr_t)Ctx->Buf;
      if (Ctx->CloseFd == 3) {
        YonaWindowsBlockingOperation *Op = YONA_WIN_BLOCKING_OP_FROM_CTX(Ctx);
        if (Op->HThread)
          CloseHandle(Op->HThread);
        if (Op->HDone)
          CloseHandle(Op->HDone);
        free(Op);
        return Result;
      }
      if (Ctx->CloseFd == 7) {
        HANDLE H = (HANDLE)(uintptr_t)Ctx->BufSize;
        if (H)
          CloseHandle(H);
      }
      free(Ctx);
      return Result;
    }
    if (Ctx->Type == YonaIoOperationReadFile && Ctx->CloseFd == 2) {
      YonaWindowsReadOperation *Op = YONA_WIN_READ_OP_FROM_CTX(Ctx);
      int64_t R = (int64_t)(intptr_t)Ctx->Buf;
      CloseHandle(Op->HFile);
      CloseHandle(Op->HDone);
      free(Op);
      return R;
    }
    if (Ctx->Buf)
      free(Ctx->Buf);
    if (Ctx->CloseFd == 1 && Ctx->Fd >= 0)
      _close(Ctx->Fd);
    free(Ctx);
    return 0;
  }
}

/* Generic offload for blocking operations that still return through io_await
 * IDs. */
int64_t
YonaRuntimeWinSubmitBlocking(YonaWindowsBlockingFunction Fn, void *Arg,
                             YonaWindowsBlockingCleanupFunction Cleanup) {
  if (!Fn) {
    if (Cleanup)
      Cleanup(Arg);
    return ioRegisterDirectResult((void *)(intptr_t)0);
  }
  uint64_t Id = (uint64_t)InterlockedExchangeAdd64(&DirectResultIdSeq, 1);
  YonaWindowsBlockingOperation *Op = (YonaWindowsBlockingOperation *)calloc(
      1, sizeof(YonaWindowsBlockingOperation));
  if (!Op) {
    if (Cleanup)
      Cleanup(Arg);
    return ioRegisterDirectResult((void *)(intptr_t)0);
  }
  Op->HDone = CreateEvent(NULL, TRUE, FALSE, NULL);
  Op->Arg = Arg;
  Op->Fn = Fn;
  Op->Cleanup = Cleanup;
  Op->Ctx.Type = IoOpWiN32BlockingPending;
  Op->Ctx.Fd = -1;
  Op->Ctx.Buf = NULL;
  Op->Ctx.BufSize = 0;
  Op->Ctx.CloseFd = 3; /* embedded ctx in YonaWindowsBlockingOperation */
  YonaRuntimeIoContextPut(Id, &Op->Ctx);
  Op->HThread = CreateThread(NULL, 0, yonaWinBlockingWorker, Op, 0, NULL);
  if (!Op->HThread) {
    if (Cleanup)
      Cleanup(Arg);
    Op->Ctx.Buf = (char *)(intptr_t)0;
    Op->Ctx.Type = (enum IoOpType)YONA_IO_OP_DIRECT_RESULT;
    if (Op->HDone)
      SetEvent(Op->HDone);
  }
  return (int64_t)Id;
}

static char *readFileBlocking(const char *Path) {
  int Fd = open(Path, O_RDONLY | O_BINARY);
  if (Fd < 0) {
    char *Empty = (char *)YonaRuntimeAllocateString(1);
    Empty[0] = '\0';
    return Empty;
  }
  struct stat St;
  if (fstat(Fd, &St) < 0) {
    _close(Fd);
    char *E = (char *)YonaRuntimeAllocateString(1);
    E[0] = '\0';
    return E;
  }
  size_t Size = (size_t)St.st_size;
  char *Buf = (char *)YonaRuntimeAllocateStringWithLength(Size + 1, Size);
  int N = (int)read(Fd, Buf, Size);
  if (N >= 0)
    Buf[N] = '\0';
  else
    Buf[0] = '\0';
  _close(Fd);
  return Buf;
}

typedef struct {
  char *Path;
  char *Content;
} WindowsWriteFileRequest;

static void winWriteFileReqCleanup(void *P) {
  WindowsWriteFileRequest *Req = (WindowsWriteFileRequest *)P;
  if (!Req)
    return;
  if (Req->Path)
    free(Req->Path);
  if (Req->Content)
    free(Req->Content);
  free(Req);
}

static int64_t winWriteFileBlocking(void *P) {
  WindowsWriteFileRequest *Req = (WindowsWriteFileRequest *)P;
  if (!Req || !Req->Path || !Req->Content)
    return 0;
  int Ok = YonaRuntimePlatformWriteFile(Req->Path, Req->Content);
  return (Ok == 0) ? 1 : 0;
}

typedef struct {
  char *Path;
} WindowsReadFileRequest;

static void winReadFileReqCleanup(void *P) {
  WindowsReadFileRequest *Req = (WindowsReadFileRequest *)P;
  if (!Req)
    return;
  if (Req->Path)
    free(Req->Path);
  free(Req);
}

static int64_t winReadFileBytesBlocking(void *P) {
  WindowsReadFileRequest *Req = (WindowsReadFileRequest *)P;
  if (!Req || !Req->Path)
    return 0;
  void *Bytes = YonaRuntimeByteArrayFromString(readFileBlocking(Req->Path));
  return (int64_t)(intptr_t)Bytes;
}

typedef struct {
  void (*Finalize)(void *);
  int Fd;
  char *Buf;
  size_t Total;
} WindowsWriteFileDescriptorStringsRequest;

static void winWriteFdStrsReqCleanup(void *P) {
  WindowsWriteFileDescriptorStringsRequest *Req =
      (WindowsWriteFileDescriptorStringsRequest *)P;
  if (!Req)
    return;
  if (Req->Buf)
    free(Req->Buf);
  free(Req);
}

static int64_t winWriteFdStrsBlocking(void *P) {
  WindowsWriteFileDescriptorStringsRequest *Req =
      (WindowsWriteFileDescriptorStringsRequest *)P;
  if (!Req)
    return -1;
  int N = (int)write(Req->Fd, Req->Buf ? Req->Buf : "", (unsigned)Req->Total);
  return (N >= 0) ? (int64_t)N : -1;
}

typedef struct {
  int Fd;
  int64_t Count;
  int64_t Offset;
} WindowsReadFileDescriptorBytesRequest;

static void winReadFdBytesReqCleanup(void *P) { free(P); }

static int64_t winReadFdBytesBlocking(void *P) {
  WindowsReadFileDescriptorBytesRequest *Req =
      (WindowsReadFileDescriptorBytesRequest *)P;
  if (!Req || Req->Count < 0)
    return 0;
  if (_lseeki64(Req->Fd, Req->Offset, SEEK_SET) < 0)
    return 0;
  int64_t *BytesBuf =
      (int64_t *)YonaRuntimeAllocate(8, sizeof(int64_t) + (size_t)Req->Count);
  BytesBuf[0] = 0;
  int N = (int)read(Req->Fd, (char *)(BytesBuf + 1), (unsigned)Req->Count);
  if (N < 0)
    N = 0;
  BytesBuf[0] = (int64_t)N;
  return (int64_t)(intptr_t)BytesBuf;
}

typedef struct {
  int Fd;
  void *Bytes;
  int64_t Offset;
} WindowsWriteFileDescriptorBytesRequest;

static void winWriteFdBytesReqCleanup(void *P) {
  WindowsWriteFileDescriptorBytesRequest *Req =
      (WindowsWriteFileDescriptorBytesRequest *)P;
  if (!Req)
    return;
  if (Req->Bytes)
    YonaRuntimeRelease(Req->Bytes);
  free(Req);
}

static int64_t winWriteFdBytesBlocking(void *P) {
  WindowsWriteFileDescriptorBytesRequest *Req =
      (WindowsWriteFileDescriptorBytesRequest *)P;
  if (!Req || !Req->Bytes)
    return -1;
  int64_t *B = (int64_t *)Req->Bytes;
  int64_t Len = B[0];
  if (Len < 0)
    Len = 0;
  uint8_t *Data = (uint8_t *)(B + 1);
  if (_lseeki64(Req->Fd, Req->Offset, SEEK_SET) < 0)
    return -1;
  int N = (int)write(Req->Fd, Data, (unsigned)Len);
  return (N >= 0) ? (int64_t)N : -1;
}

/* Returns 0 to fall back to synchronous read + direct-result registration. */
static int64_t readFileSubmitTryIocp(const char *Path) {
  yonaIocpEnsure();
  if (!IocpPort)
    return 0;

  HANDLE Hf = CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ, NULL,
                          OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
  if (Hf == INVALID_HANDLE_VALUE)
    return 0;

  LARGE_INTEGER Li;
  if (!GetFileSizeEx(Hf, &Li)) {
    CloseHandle(Hf);
    return 0;
  }
  LONGLONG Sz64 = Li.QuadPart;
  if (Sz64 < 0) {
    CloseHandle(Hf);
    return 0;
  }
  if (Sz64 == 0) {
    CloseHandle(Hf);
    return 0;
  }
  size_t Size = (size_t)Sz64;
  if (Size > (size_t)UINT32_MAX) {
    CloseHandle(Hf);
    return 0;
  }
  DWORD Dwsz = (DWORD)Size;

  YonaWindowsReadOperation *Op =
      (YonaWindowsReadOperation *)calloc(1, sizeof(*Op));
  if (!Op) {
    CloseHandle(Hf);
    return 0;
  }
  Op->HFile = Hf;
  Op->HDone = CreateEventA(NULL, TRUE, FALSE, NULL);
  if (!Op->HDone) {
    CloseHandle(Hf);
    free(Op);
    return 0;
  }
  memset(&Op->Ov, 0, sizeof(Op->Ov));
  Op->Ctx.Type = IoOpReadFileIocpPending;
  Op->Ctx.Fd = -1;
  Op->Ctx.CloseFd = 2;
  Op->Ctx.Buf = (char *)YonaRuntimeAllocateStringWithLength((size_t)Dwsz + 1,
                                                            (size_t)Dwsz);
  Op->Ctx.BufSize = (size_t)Dwsz;

  HANDLE Assoc = CreateIoCompletionPort(Hf, IocpPort, (ULONG_PTR)Op, 0);
  if (Assoc != IocpPort) {
    CloseHandle(Op->HDone);
    CloseHandle(Hf);
    free(Op);
    return 0;
  }

  uint64_t Id = (uint64_t)InterlockedExchangeAdd64(&DirectResultIdSeq, 1);
  YonaRuntimeIoContextPut(Id, &Op->Ctx);

  DWORD Rd = 0;
  BOOL Rf = ReadFile(Hf, Op->Ctx.Buf, Dwsz, &Rd, &Op->Ov);
  if (Rf) {
    if (Rd > 0 && Rd <= Dwsz)
      Op->Ctx.Buf[Rd] = '\0';
    else
      Op->Ctx.Buf[0] = '\0';
    Op->Ctx.Type = YonaIoOperationReadFile;
    SetEvent(Op->HDone);
  } else {
    DWORD Err = GetLastError();
    if (Err != ERROR_IO_PENDING) {
      (void)YonaRuntimeIoContextTake(Id);
      CloseHandle(Op->HDone);
      CloseHandle(Hf);
      if (Op->Ctx.Buf)
        YonaRuntimeRelease(Op->Ctx.Buf);
      free(Op);
      return 0;
    }
  }
  return (int64_t)Id;
}

int64_t YonaRuntimePlatformSubmitFileRead(const char *Path) {
  int64_t Id = readFileSubmitTryIocp(Path);
  if (Id > 0)
    return Id;
  return ioRegisterDirectResult((void *)(intptr_t)readFileBlocking(Path));
}

int64_t YonaRuntimePlatformSubmitFileWrite(const char *Path,
                                           const char *Content) {
  int Ok = YonaRuntimePlatformWriteFile(Path, Content);
  return ioRegisterDirectResult((void *)(intptr_t)(Ok == 0 ? 1 : 0));
}

int64_t YonaRuntimePlatformSubmitFileByteRead(const char *Path) {
  void *Bytes = YonaRuntimeByteArrayFromString(readFileBlocking(Path));
  return ioRegisterDirectResult(Bytes);
}

char *YonaRuntimePlatformReadFile(const char *Path) {
  return readFileBlocking(Path);
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
  return stat(Path, &St) == 0 ? 1 : 0;
}

int YonaRuntimePlatformRemoveFile(const char *Path) {
  return remove(Path) == 0 ? 0 : -1;
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
  Flags |= O_BINARY;
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
  return (int64_t)_lseeki64(Fd, (__int64)Offset, Whence);
}

int64_t YonaRuntimePlatformTellFileHandle(int Fd) {
  return (int64_t)_lseeki64(Fd, 0, SEEK_CUR);
}

int64_t YonaRuntimePlatformAdvanceFileHandle(int Fd, int64_t Delta) {
  return (int64_t)_lseeki64(Fd, (__int64)Delta, SEEK_CUR);
}

int64_t YonaRuntimePlatformFlushFileHandle(int Fd) {
  return _commit(Fd) == 0 ? 1 : 0;
}

int64_t YonaRuntimePlatformTruncateFileHandle(int Fd, int64_t Length) {
  return _chsize_s(Fd, (__int64)Length) == 0 ? 1 : 0;
}

int64_t *YonaRuntimePlatformListDirectory(const char *Path) {
  char Pattern[MAX_PATH];
  snprintf(Pattern, sizeof(Pattern), "%s\\*", Path);
  WIN32_FIND_DATAA Fd;
  HANDLE H = FindFirstFileA(Pattern, &Fd);
  if (H == INVALID_HANDLE_VALUE)
    return YonaRuntimeSequenceAllocate(0);

  int64_t Count = 0;
  do {
    if (Fd.cFileName[0] == '.' &&
        (Fd.cFileName[1] == '\0' ||
         (Fd.cFileName[1] == '.' && Fd.cFileName[2] == '\0')))
      continue;
    Count++;
  } while (FindNextFileA(H, &Fd));
  FindClose(H);

  int64_t *Seq = YonaRuntimeSequenceAllocate(Count);
  H = FindFirstFileA(Pattern, &Fd);
  if (H == INVALID_HANDLE_VALUE) {
    /* race */
    return YonaRuntimeSequenceAllocate(0);
  }
  int64_t I = 0;
  do {
    if (Fd.cFileName[0] == '.' &&
        (Fd.cFileName[1] == '\0' ||
         (Fd.cFileName[1] == '.' && Fd.cFileName[2] == '\0')))
      continue;
    size_t Len = strlen(Fd.cFileName);
    char *Name = (char *)YonaRuntimeAllocateString(Len + 1);
    memcpy(Name, Fd.cFileName, Len + 1);
    YonaRuntimeSequenceSet(Seq, I, (int64_t)(intptr_t)Name);
    I++;
  } while (FindNextFileA(H, &Fd));
  YonaRuntimeSequenceSetHeap(Seq, 1);
  FindClose(H);
  return Seq;
}

#define YONA_LINE_ITER_BUF_SIZE 65536

typedef struct {
  void (*Finalize)(void *);
  int Fd;
  char *Buf;
  size_t BufPos;
  size_t BufLen;
  int Eof;
} LineIteratorState;

static void lineIterFinalize(void *Raw) {
  LineIteratorState *St = (LineIteratorState *)Raw;
  if (St->Fd >= 0)
    close(St->Fd);
  free(St->Buf);
}

static int64_t lineIterNext(int64_t *ClosureEnv) {
  LineIteratorState *St = (LineIteratorState *)(intptr_t)ClosureEnv[6];
  if (St->Eof) {
    int64_t *Adt = (int64_t *)YonaRuntimeAllocate(4, 3 * sizeof(int64_t));
    Adt[0] = 1;
    Adt[1] = 0;
    Adt[2] = 0;
    return (int64_t)(intptr_t)Adt;
  }
  char LineBuf[8192];
  size_t LineLen = 0;
  for (;;) {
    if (St->BufPos >= St->BufLen) {
      int N = (int)read(St->Fd, St->Buf, YONA_LINE_ITER_BUF_SIZE);
      if (N <= 0) {
        St->Eof = 1;
        if (LineLen == 0) {
          int64_t *Adt = (int64_t *)YonaRuntimeAllocate(4, 3 * sizeof(int64_t));
          Adt[0] = 1;
          Adt[1] = 0;
          Adt[2] = 0;
          return (int64_t)(intptr_t)Adt;
        }
        break;
      }
      St->BufLen = (size_t)N;
      St->BufPos = 0;
    }
    while (St->BufPos < St->BufLen) {
      char C = St->Buf[St->BufPos++];
      if (C == '\n')
        goto line_done;
      if (LineLen < sizeof(LineBuf) - 1)
        LineBuf[LineLen++] = C;
    }
  }
line_done:
  LineBuf[LineLen] = '\0';
  char *LineStr =
      (char *)YonaRuntimeAllocateStringWithLength(LineLen + 1, LineLen);
  memcpy(LineStr, LineBuf, LineLen + 1);
  int64_t *Adt = (int64_t *)YonaRuntimeAllocate(4, 4 * sizeof(int64_t));
  Adt[0] = 0;
  Adt[1] = 1;
  Adt[2] = 1;
  Adt[3] = (int64_t)(intptr_t)LineStr;
  return (int64_t)(intptr_t)Adt;
}

int64_t YonaRuntimeFileLineIteratorCreate(const char *Path) {
  int Fd = open(Path, O_RDONLY);
  LineIteratorState *St = (LineIteratorState *)YonaRuntimeNativeStateAllocate(
      sizeof(LineIteratorState), lineIterFinalize);
  if (Fd < 0) {
    St->Fd = -1;
    St->Buf = NULL;
    St->BufPos = 0;
    St->BufLen = 0;
    St->Eof = 1;
  } else {
    St->Fd = Fd;
    St->Buf = (char *)malloc(YONA_LINE_ITER_BUF_SIZE);
    St->BufPos = 0;
    St->BufLen = 0;
    St->Eof = 0;
  }
  int64_t *Closure =
      (int64_t *)YonaRuntimeClosureCreate((void *)lineIterNext, 0, 0, 1);
  YonaRuntimeClosureSetCapture(Closure, 0, (int64_t)(intptr_t)St);
  YonaRuntimeClosureSetHeapMask(Closure, 1);
  int64_t *IterAdt = (int64_t *)YonaRuntimeAllocate(4, 4 * sizeof(int64_t));
  IterAdt[0] = 0;
  IterAdt[1] = 1;
  IterAdt[2] = 1;
  IterAdt[3] = (int64_t)(intptr_t)Closure;
  return (int64_t)(intptr_t)IterAdt;
}

typedef struct {
  void (*Finalize)(void *);
  int Fd;
  int64_t Position;
  int64_t ChunkSize;
  int Eof;
} ChunkIteratorState;

static int64_t chunkIterNext(int64_t *Env) {
  ChunkIteratorState *St = (ChunkIteratorState *)(intptr_t)Env[6];
  if (St->Eof) {
    int64_t *None = (int64_t *)YonaRuntimeAllocate(4, 3 * sizeof(int64_t));
    None[0] = 1;
    None[1] = 0;
    None[2] = 0;
    return (int64_t)(intptr_t)None;
  }
  int64_t *Buf = (int64_t *)YonaRuntimeAllocate(8, sizeof(int64_t) +
                                                       (size_t)St->ChunkSize);
  Buf[0] = 0;
  if (_lseeki64(St->Fd, St->Position, SEEK_SET) < 0) {
    YonaRuntimeRelease(Buf);
    St->Eof = 1;
    int64_t *None = (int64_t *)YonaRuntimeAllocate(4, 3 * sizeof(int64_t));
    None[0] = 1;
    None[1] = 0;
    None[2] = 0;
    return (int64_t)(intptr_t)None;
  }
  int N = (int)read(St->Fd, (char *)(Buf + 1), (unsigned)(St->ChunkSize));
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
  int64_t *Some = (int64_t *)YonaRuntimeAllocate(4, 4 * sizeof(int64_t));
  Some[0] = 0;
  Some[1] = 1;
  Some[2] = 1;
  Some[3] = (int64_t)(intptr_t)Buf;
  return (int64_t)(intptr_t)Some;
}

int64_t YonaRuntimeFileChunkIteratorCreate(int64_t FileDescriptor,
                                           int64_t ChunkSize) {
  ChunkIteratorState *St = (ChunkIteratorState *)YonaRuntimeNativeStateAllocate(
      sizeof(ChunkIteratorState), NULL);
  St->Fd = (int)FileDescriptor;
  St->Position = 0;
  St->ChunkSize = ChunkSize;
  St->Eof = 0;
  int64_t *Closure =
      (int64_t *)YonaRuntimeClosureCreate((void *)chunkIterNext, 0, 0, 1);
  YonaRuntimeClosureSetCapture(Closure, 0, (int64_t)(intptr_t)St);
  YonaRuntimeClosureSetHeapMask(Closure, 1);
  int64_t *IterAdt = (int64_t *)YonaRuntimeAllocate(4, 4 * sizeof(int64_t));
  IterAdt[0] = 0;
  IterAdt[1] = 1;
  IterAdt[2] = 1;
  IterAdt[3] = (int64_t)(intptr_t)Closure;
  return (int64_t)(intptr_t)IterAdt;
}

static int64_t writeFdStrsSubmitImpl(int Fd, const char *S1, const char *S2) {
  size_t L1 = S1 ? strlen(S1) : 0;
  size_t L2 = S2 ? strlen(S2) : 0;
  size_t Total = L1 + L2;
  char *Buf = (char *)malloc(Total + 1);
  if (L1)
    memcpy(Buf, S1, L1);
  if (L2)
    memcpy(Buf + L1, S2, L2);
  Buf[Total] = '\0';
  int N = (int)write(Fd, Buf, (unsigned)Total);
  free(Buf);
  return ioRegisterDirectResult((void *)(intptr_t)(N >= 0 ? (int64_t)N : -1));
}

int64_t YonaRuntimePlatformSubmitFileDescriptorStringWrite(int Fd,
                                                           const char *S) {
  return writeFdStrsSubmitImpl(Fd, S, NULL);
}

int64_t YonaRuntimePlatformSubmitFileDescriptorStringsWrite(int Fd,
                                                            const char *S1,
                                                            const char *S2) {
  return writeFdStrsSubmitImpl(Fd, S1, S2);
}

const char *YonaRuntimePlatformReadLineFromFileDescriptor(int Fd) {
  size_t Cap = 512;
  size_t Len = 0;
  char *B = (char *)malloc(Cap);
  for (;;) {
    if (Len + 1 >= Cap) {
      Cap *= 2;
      B = (char *)realloc(B, Cap);
    }
    char Ch;
    int N = (int)read(Fd, &Ch, 1);
    if (N <= 0) {
      if (Len == 0) {
        free(B);
        return NULL;
      }
      break;
    }
    if (Ch == '\n')
      break;
    if (Ch == '\r')
      continue;
    B[Len++] = Ch;
  }
  char *Out = (char *)YonaRuntimeAllocateString(Len + 1);
  memcpy(Out, B, Len);
  Out[Len] = '\0';
  free(B);
  return Out;
}

int64_t YonaRuntimePlatformSubmitFileDescriptorByteRead(int Fd, int64_t Count,
                                                        int64_t Offset) {
  if (_lseeki64(Fd, Offset, SEEK_SET) < 0)
    return 0;
  int64_t *BytesBuf =
      (int64_t *)YonaRuntimeAllocate(8, sizeof(int64_t) + (size_t)Count);
  BytesBuf[0] = 0;
  int N = (int)read(Fd, (char *)(BytesBuf + 1), (unsigned)Count);
  if (N < 0)
    N = 0;
  BytesBuf[0] = (int64_t)N;
  return ioRegisterDirectResult(BytesBuf);
}

int64_t YonaRuntimePlatformSubmitFileDescriptorByteWrite(int Fd, void *Bytes,
                                                         int64_t Offset) {
  int64_t *B = (int64_t *)Bytes;
  int64_t Len = B[0];
  uint8_t *Data = (uint8_t *)(B + 1);
  if (_lseeki64(Fd, Offset, SEEK_SET) < 0)
    return ioRegisterDirectResult((void *)(intptr_t)-1);
  int N = (int)write(Fd, Data, (unsigned)Len);
  YonaRuntimeRelease(Bytes);
  return ioRegisterDirectResult((void *)(intptr_t)(N >= 0 ? (int64_t)N : -1));
}
