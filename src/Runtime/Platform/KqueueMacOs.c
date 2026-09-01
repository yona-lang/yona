/*
 * Single kqueue + worker pool + io_ctx table for all macOS platform TUs.
 */

#ifndef __APPLE__
#error "kqueue_macos.c is for macOS builds only"
#endif

#include "yona/Runtime/Platform/Kqueue.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>

#define YONA_KQUEUE_PENDING_CAPACITY 256
#define YONA_KQUEUE_WORKER_COUNT 8
#define YONA_KQUEUE_OPERATION_CAPACITY 256

enum YonaKqueueOperationKind {
  YonaKqueueRead,
  YonaKqueueWrite,
  YonaKqueueConnect,
  YonaKqueueAccept,
  YonaKqueueSend,
  YonaKqueueReceive,
  YonaKqueueNop,
};

typedef struct YonaKqueueWork {
  struct YonaKqueueWork *Next;
  uint64_t OperationId;
  enum YonaKqueueOperationKind Kind;
  int FileDescriptor;
  void *Buffer;
  size_t Length;
  off_t Offset;
  struct sockaddr_storage Address;
  socklen_t AddressLength;
  socklen_t *AddressLengthPointer;
  int Cancelled;
} YonaKqueueWork;

static int YonaKqueue = -1;
static int YonaWakeReadFileDescriptor = -1;
static int YonaWakeWriteFileDescriptor = -1;
static int YonaKqueueReady;
static atomic_uint_fast64_t YonaNextOperationId;
static pthread_mutex_t YonaKqueueMutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
  uint64_t OperationId;
  int32_t Result;
  int Used;
} YonaPendingCompletion;

static YonaPendingCompletion
    YonaPendingCompletions[YONA_KQUEUE_PENDING_CAPACITY];

static YonaKqueueWork *YonaOperations[YONA_KQUEUE_OPERATION_CAPACITY];

static YonaKqueueWork *YonaWorkHead;
static YonaKqueueWork *YonaWorkTail;
static pthread_mutex_t YonaWorkMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t YonaWorkCondition = PTHREAD_COND_INITIALIZER;
static pthread_t YonaWorkers[YONA_KQUEUE_WORKER_COUNT];
static int YonaWorkersStarted;

static int storePendingCompletion(uint64_t OperationId, int32_t Result) {
  for (int Index = 0; Index < YONA_KQUEUE_PENDING_CAPACITY; Index++) {
    if (!YonaPendingCompletions[Index].Used) {
      YonaPendingCompletions[Index].OperationId = OperationId;
      YonaPendingCompletions[Index].Result = Result;
      YonaPendingCompletions[Index].Used = 1;
      return 0;
    }
  }
  return -1;
}

static int takePendingCompletion(uint64_t OperationId, int32_t *Output) {
  for (int Index = 0; Index < YONA_KQUEUE_PENDING_CAPACITY; Index++) {
    if (YonaPendingCompletions[Index].Used &&
        YonaPendingCompletions[Index].OperationId == OperationId) {
      *Output = YonaPendingCompletions[Index].Result;
      YonaPendingCompletions[Index].Used = 0;
      return 1;
    }
  }
  return 0;
}

static void storeOperation(YonaKqueueWork *Work) {
  for (int Index = 0; Index < YONA_KQUEUE_OPERATION_CAPACITY; Index++) {
    if (!YonaOperations[Index]) {
      YonaOperations[Index] = Work;
      return;
    }
  }
}

static YonaKqueueWork *findOperation(uint64_t OperationId) {
  for (int Index = 0; Index < YONA_KQUEUE_OPERATION_CAPACITY; Index++) {
    if (YonaOperations[Index] &&
        YonaOperations[Index]->OperationId == OperationId)
      return YonaOperations[Index];
  }
  return NULL;
}

static YonaKqueueWork *takeOperation(uint64_t OperationId) {
  for (int Index = 0; Index < YONA_KQUEUE_OPERATION_CAPACITY; Index++) {
    if (YonaOperations[Index] &&
        YonaOperations[Index]->OperationId == OperationId) {
      YonaKqueueWork *Work = YonaOperations[Index];
      YonaOperations[Index] = NULL;
      return Work;
    }
  }
  return NULL;
}

static void wakeAwaiters(void) {
  char WakeByte = 1;
  (void)write(YonaWakeWriteFileDescriptor, &WakeByte, 1);
}

static void completeOperationLocked(uint64_t OperationId, int32_t Result) {
  YonaKqueueWork *Work = takeOperation(OperationId);
  if (Work)
    free(Work);
  (void)storePendingCompletion(OperationId, Result);
  wakeAwaiters();
}

static void setNonBlocking(int FileDescriptor) {
  int Flags = fcntl(FileDescriptor, F_GETFL, 0);
  if (Flags >= 0)
    fcntl(FileDescriptor, F_SETFL, Flags | O_NONBLOCK);
}

static int32_t performFileIo(YonaKqueueWork *Work) {
  ssize_t Count;
  if (Work->Kind == YonaKqueueRead) {
    if (Work->Offset == (off_t)-1)
      Count = read(Work->FileDescriptor, Work->Buffer, Work->Length);
    else
      Count =
          pread(Work->FileDescriptor, Work->Buffer, Work->Length, Work->Offset);
  } else {
    if (Work->Offset == (off_t)-1)
      Count = write(Work->FileDescriptor, Work->Buffer, Work->Length);
    else
      Count = pwrite(Work->FileDescriptor, Work->Buffer, Work->Length,
                     Work->Offset);
  }
  if (Count < 0)
    return (int32_t)-errno;
  return (int32_t)Count;
}

static void queueFileWork(YonaKqueueWork *Work) {
  pthread_mutex_lock(&YonaWorkMutex);
  Work->Next = NULL;
  if (YonaWorkTail)
    YonaWorkTail->Next = Work;
  else
    YonaWorkHead = Work;
  YonaWorkTail = Work;
  pthread_cond_signal(&YonaWorkCondition);
  pthread_mutex_unlock(&YonaWorkMutex);
}

static void *kqueueWorkerMain(void *Unused) {
  (void)Unused;
  for (;;) {
    pthread_mutex_lock(&YonaWorkMutex);
    while (!YonaWorkHead)
      pthread_cond_wait(&YonaWorkCondition, &YonaWorkMutex);
    YonaKqueueWork *Work = YonaWorkHead;
    YonaWorkHead = Work->Next;
    if (!YonaWorkHead)
      YonaWorkTail = NULL;
    pthread_mutex_unlock(&YonaWorkMutex);

    pthread_mutex_lock(&YonaKqueueMutex);
    int Cancelled = Work->Cancelled;
    uint64_t OperationId = Work->OperationId;
    pthread_mutex_unlock(&YonaKqueueMutex);
    if (Cancelled) {
      pthread_mutex_lock(&YonaKqueueMutex);
      completeOperationLocked(OperationId, -ECANCELED);
      pthread_mutex_unlock(&YonaKqueueMutex);
      continue;
    }

    int32_t Result = performFileIo(Work);
    pthread_mutex_lock(&YonaKqueueMutex);
    if (Work->Cancelled)
      Result = -ECANCELED;
    completeOperationLocked(OperationId, Result);
    pthread_mutex_unlock(&YonaKqueueMutex);
  }
  return NULL;
}

static int32_t finishConnect(YonaKqueueWork *Work) {
  int ErrorCode = 0;
  socklen_t Length = sizeof(ErrorCode);
  if (getsockopt(Work->FileDescriptor, SOL_SOCKET, SO_ERROR, &ErrorCode,
                 &Length) < 0)
    return (int32_t)-errno;
  return ErrorCode ? (int32_t)-ErrorCode : 0;
}

static int32_t finishAccept(YonaKqueueWork *Work) {
  socklen_t *AddressLength = Work->AddressLengthPointer;
  int AcceptedFileDescriptor = accept(
      Work->FileDescriptor, (struct sockaddr *)Work->Buffer, AddressLength);
  if (AcceptedFileDescriptor < 0)
    return (int32_t)-errno;
  return (int32_t)AcceptedFileDescriptor;
}

static int32_t finishSend(YonaKqueueWork *Work) {
  ssize_t Count = send(Work->FileDescriptor, Work->Buffer, Work->Length, 0);
  if (Count < 0)
    return (int32_t)-errno;
  return (int32_t)Count;
}

static int32_t finishReceive(YonaKqueueWork *Work) {
  ssize_t Count = recv(Work->FileDescriptor, Work->Buffer, Work->Length, 0);
  if (Count < 0)
    return (int32_t)-errno;
  return (int32_t)Count;
}

static void handleSocketEvent(int FileDescriptor, int16_t Filter) {
  (void)Filter;
  for (int Index = 0; Index < YONA_KQUEUE_OPERATION_CAPACITY; Index++) {
    YonaKqueueWork *Work = YonaOperations[Index];
    if (!Work || Work->FileDescriptor != FileDescriptor)
      continue;
    if (Work->Kind != YonaKqueueConnect && Work->Kind != YonaKqueueAccept &&
        Work->Kind != YonaKqueueSend && Work->Kind != YonaKqueueReceive)
      continue;
    int32_t Result;
    if (Work->Cancelled)
      Result = -ECANCELED;
    else if (Work->Kind == YonaKqueueConnect)
      Result = finishConnect(Work);
    else if (Work->Kind == YonaKqueueAccept)
      Result = finishAccept(Work);
    else if (Work->Kind == YonaKqueueSend)
      Result = finishSend(Work);
    else
      Result = finishReceive(Work);
    if ((Result == -EAGAIN || Result == -EWOULDBLOCK) && !Work->Cancelled)
      return;
    completeOperationLocked(Work->OperationId, Result);
    return;
  }
}

static void drainWakeup(void) {
  char Buffer[64];
  while (read(YonaWakeReadFileDescriptor, Buffer, sizeof(Buffer)) > 0) {
  }
}

static int initializeKqueue(void) {
  if (YonaKqueueReady)
    return 0;
  YonaKqueue = kqueue();
  if (YonaKqueue < 0)
    return -1;
  int FileDescriptors[2];
  if (pipe(FileDescriptors) < 0) {
    close(YonaKqueue);
    YonaKqueue = -1;
    return -1;
  }
  YonaWakeReadFileDescriptor = FileDescriptors[0];
  YonaWakeWriteFileDescriptor = FileDescriptors[1];
  fcntl(YonaWakeReadFileDescriptor, F_SETFL, O_NONBLOCK);
  fcntl(YonaWakeWriteFileDescriptor, F_SETFL, O_NONBLOCK);
  struct kevent Event;
  EV_SET(&Event, (uintptr_t)YonaWakeReadFileDescriptor, EVFILT_READ,
         EV_ADD | EV_CLEAR, 0, 0, NULL);
  if (kevent(YonaKqueue, &Event, 1, NULL, 0, NULL) < 0) {
    close(YonaWakeReadFileDescriptor);
    close(YonaWakeWriteFileDescriptor);
    close(YonaKqueue);
    YonaKqueue = -1;
    return -1;
  }
  atomic_init(&YonaNextOperationId, 1);
  if (!YonaWorkersStarted) {
    for (int Index = 0; Index < YONA_KQUEUE_WORKER_COUNT; Index++)
      pthread_create(&YonaWorkers[Index], NULL, kqueueWorkerMain, NULL);
    YonaWorkersStarted = 1;
  }
  YonaKqueueReady = 1;
  return 0;
}

static uint64_t allocateOperationId(void) {
  return atomic_fetch_add(&YonaNextOperationId, 1);
}

static int armSocket(YonaKqueueWork *Work, int16_t Filter) {
  struct kevent Event;
  EV_SET(&Event, (uintptr_t)Work->FileDescriptor, Filter, EV_ADD | EV_ONESHOT,
         0, 0, (void *)(uintptr_t)Work->OperationId);
  if (kevent(YonaKqueue, &Event, 1, NULL, 0, NULL) < 0)
    return -1;
  return 0;
}

static uint64_t submitFileOperation(enum YonaKqueueOperationKind Kind,
                                    int FileDescriptor, void *Buffer,
                                    size_t Length, off_t Offset) {
  pthread_mutex_lock(&YonaKqueueMutex);
  if (!YonaKqueueReady && initializeKqueue() != 0) {
    pthread_mutex_unlock(&YonaKqueueMutex);
    return 0;
  }
  YonaKqueueWork *Work = (YonaKqueueWork *)calloc(1, sizeof(YonaKqueueWork));
  if (!Work) {
    pthread_mutex_unlock(&YonaKqueueMutex);
    return 0;
  }
  Work->OperationId = allocateOperationId();
  Work->Kind = Kind;
  Work->FileDescriptor = FileDescriptor;
  Work->Buffer = Buffer;
  Work->Length = Length;
  Work->Offset = Offset;
  storeOperation(Work);
  uint64_t OperationId = Work->OperationId;
  pthread_mutex_unlock(&YonaKqueueMutex);
  queueFileWork(Work);
  return OperationId;
}

uint64_t YonaRuntimeKqueueSubmitRead(int FileDescriptor, void *Buffer,
                                     size_t Length, off_t Offset) {
  return submitFileOperation(YonaKqueueRead, FileDescriptor, Buffer, Length,
                             Offset);
}

uint64_t YonaRuntimeKqueueSubmitWrite(int FileDescriptor, const void *Buffer,
                                      size_t Length, off_t Offset) {
  return submitFileOperation(YonaKqueueWrite, FileDescriptor, (void *)Buffer,
                             Length, Offset);
}

uint64_t YonaRuntimeKqueueSubmitConnect(int FileDescriptor, const void *Address,
                                        socklen_t AddressLength) {
  pthread_mutex_lock(&YonaKqueueMutex);
  if (!YonaKqueueReady && initializeKqueue() != 0) {
    pthread_mutex_unlock(&YonaKqueueMutex);
    return 0;
  }
  setNonBlocking(FileDescriptor);
  YonaKqueueWork *Work = (YonaKqueueWork *)calloc(1, sizeof(YonaKqueueWork));
  if (!Work) {
    pthread_mutex_unlock(&YonaKqueueMutex);
    return 0;
  }
  Work->OperationId = allocateOperationId();
  Work->Kind = YonaKqueueConnect;
  Work->FileDescriptor = FileDescriptor;
  if (Address && AddressLength > 0) {
    if (AddressLength > sizeof(Work->Address))
      AddressLength = sizeof(Work->Address);
    memcpy(&Work->Address, Address, AddressLength);
    Work->AddressLength = AddressLength;
    Work->Buffer = &Work->Address;
  }
  int ResultCode =
      connect(FileDescriptor, (const struct sockaddr *)Address, AddressLength);
  if (ResultCode == 0) {
    uint64_t OperationId = Work->OperationId;
    free(Work);
    (void)storePendingCompletion(OperationId, 0);
    pthread_mutex_unlock(&YonaKqueueMutex);
    return OperationId;
  }
  if (errno != EINPROGRESS) {
    uint64_t OperationId = Work->OperationId;
    int32_t ErrorCode = (int32_t)-errno;
    free(Work);
    (void)storePendingCompletion(OperationId, ErrorCode);
    pthread_mutex_unlock(&YonaKqueueMutex);
    return OperationId;
  }
  if (armSocket(Work, EVFILT_WRITE) < 0) {
    free(Work);
    pthread_mutex_unlock(&YonaKqueueMutex);
    return 0;
  }
  storeOperation(Work);
  uint64_t OperationId = Work->OperationId;
  pthread_mutex_unlock(&YonaKqueueMutex);
  return OperationId;
}

uint64_t YonaRuntimeKqueueSubmitAccept(int FileDescriptor, void *Address,
                                       socklen_t *AddressLength) {
  pthread_mutex_lock(&YonaKqueueMutex);
  if (!YonaKqueueReady && initializeKqueue() != 0) {
    pthread_mutex_unlock(&YonaKqueueMutex);
    return 0;
  }
  setNonBlocking(FileDescriptor);
  YonaKqueueWork *Work = (YonaKqueueWork *)calloc(1, sizeof(YonaKqueueWork));
  if (!Work) {
    pthread_mutex_unlock(&YonaKqueueMutex);
    return 0;
  }
  Work->OperationId = allocateOperationId();
  Work->Kind = YonaKqueueAccept;
  Work->FileDescriptor = FileDescriptor;
  Work->Buffer = Address;
  Work->AddressLengthPointer = AddressLength;
  int AcceptedFileDescriptor =
      accept(FileDescriptor, (struct sockaddr *)Address, AddressLength);
  if (AcceptedFileDescriptor >= 0) {
    uint64_t OperationId = Work->OperationId;
    free(Work);
    (void)storePendingCompletion(OperationId, (int32_t)AcceptedFileDescriptor);
    pthread_mutex_unlock(&YonaKqueueMutex);
    return OperationId;
  }
  if (errno != EAGAIN && errno != EWOULDBLOCK) {
    uint64_t OperationId = Work->OperationId;
    int32_t ErrorCode = (int32_t)-errno;
    free(Work);
    (void)storePendingCompletion(OperationId, ErrorCode);
    pthread_mutex_unlock(&YonaKqueueMutex);
    return OperationId;
  }
  if (armSocket(Work, EVFILT_READ) < 0) {
    free(Work);
    pthread_mutex_unlock(&YonaKqueueMutex);
    return 0;
  }
  storeOperation(Work);
  uint64_t OperationId = Work->OperationId;
  pthread_mutex_unlock(&YonaKqueueMutex);
  return OperationId;
}

uint64_t YonaRuntimeKqueueSubmitSend(int FileDescriptor, const void *Buffer,
                                     size_t Length) {
  pthread_mutex_lock(&YonaKqueueMutex);
  if (!YonaKqueueReady && initializeKqueue() != 0) {
    pthread_mutex_unlock(&YonaKqueueMutex);
    return 0;
  }
  setNonBlocking(FileDescriptor);
  YonaKqueueWork *Work = (YonaKqueueWork *)calloc(1, sizeof(YonaKqueueWork));
  if (!Work) {
    pthread_mutex_unlock(&YonaKqueueMutex);
    return 0;
  }
  Work->OperationId = allocateOperationId();
  Work->Kind = YonaKqueueSend;
  Work->FileDescriptor = FileDescriptor;
  Work->Buffer = (void *)Buffer;
  Work->Length = Length;
  ssize_t Count = send(FileDescriptor, Buffer, Length, 0);
  if (Count >= 0) {
    uint64_t OperationId = Work->OperationId;
    free(Work);
    (void)storePendingCompletion(OperationId, (int32_t)Count);
    pthread_mutex_unlock(&YonaKqueueMutex);
    return OperationId;
  }
  if (errno != EAGAIN && errno != EWOULDBLOCK) {
    uint64_t OperationId = Work->OperationId;
    int32_t ErrorCode = (int32_t)-errno;
    free(Work);
    (void)storePendingCompletion(OperationId, ErrorCode);
    pthread_mutex_unlock(&YonaKqueueMutex);
    return OperationId;
  }
  if (armSocket(Work, EVFILT_WRITE) < 0) {
    free(Work);
    pthread_mutex_unlock(&YonaKqueueMutex);
    return 0;
  }
  storeOperation(Work);
  uint64_t OperationId = Work->OperationId;
  pthread_mutex_unlock(&YonaKqueueMutex);
  return OperationId;
}

uint64_t YonaRuntimeKqueueSubmitReceive(int FileDescriptor, void *Buffer,
                                        size_t Length) {
  pthread_mutex_lock(&YonaKqueueMutex);
  if (!YonaKqueueReady && initializeKqueue() != 0) {
    pthread_mutex_unlock(&YonaKqueueMutex);
    return 0;
  }
  setNonBlocking(FileDescriptor);
  YonaKqueueWork *Work = (YonaKqueueWork *)calloc(1, sizeof(YonaKqueueWork));
  if (!Work) {
    pthread_mutex_unlock(&YonaKqueueMutex);
    return 0;
  }
  Work->OperationId = allocateOperationId();
  Work->Kind = YonaKqueueReceive;
  Work->FileDescriptor = FileDescriptor;
  Work->Buffer = Buffer;
  Work->Length = Length;
  ssize_t Count = recv(FileDescriptor, Buffer, Length, 0);
  if (Count >= 0) {
    uint64_t OperationId = Work->OperationId;
    free(Work);
    (void)storePendingCompletion(OperationId, (int32_t)Count);
    pthread_mutex_unlock(&YonaKqueueMutex);
    return OperationId;
  }
  if (errno != EAGAIN && errno != EWOULDBLOCK) {
    uint64_t OperationId = Work->OperationId;
    int32_t ErrorCode = (int32_t)-errno;
    free(Work);
    (void)storePendingCompletion(OperationId, ErrorCode);
    pthread_mutex_unlock(&YonaKqueueMutex);
    return OperationId;
  }
  if (armSocket(Work, EVFILT_READ) < 0) {
    free(Work);
    pthread_mutex_unlock(&YonaKqueueMutex);
    return 0;
  }
  storeOperation(Work);
  uint64_t OperationId = Work->OperationId;
  pthread_mutex_unlock(&YonaKqueueMutex);
  return OperationId;
}

uint64_t YonaRuntimeKqueueSubmitNop(void) {
  pthread_mutex_lock(&YonaKqueueMutex);
  if (!YonaKqueueReady && initializeKqueue() != 0) {
    pthread_mutex_unlock(&YonaKqueueMutex);
    return 0;
  }
  uint64_t OperationId = allocateOperationId();
  (void)storePendingCompletion(OperationId, 0);
  pthread_mutex_unlock(&YonaKqueueMutex);
  return OperationId;
}

int32_t YonaRuntimeKqueueAwait(uint64_t OperationId) {
  for (;;) {
    pthread_mutex_lock(&YonaKqueueMutex);
    if (!YonaKqueueReady && initializeKqueue() != 0) {
      pthread_mutex_unlock(&YonaKqueueMutex);
      return -1;
    }
    int32_t StashedResult = 0;
    if (takePendingCompletion(OperationId, &StashedResult)) {
      pthread_mutex_unlock(&YonaKqueueMutex);
      return StashedResult;
    }
    int KqueueDescriptor = YonaKqueue;
    pthread_mutex_unlock(&YonaKqueueMutex);

    struct kevent Event;
    int Count = kevent(KqueueDescriptor, NULL, 0, &Event, 1, NULL);

    pthread_mutex_lock(&YonaKqueueMutex);
    if (Count > 0) {
      if ((int)Event.ident == YonaWakeReadFileDescriptor &&
          Event.filter == EVFILT_READ)
        drainWakeup();
      else
        handleSocketEvent((int)Event.ident, Event.filter);
    }
    if (takePendingCompletion(OperationId, &StashedResult)) {
      pthread_mutex_unlock(&YonaKqueueMutex);
      return StashedResult;
    }
    pthread_mutex_unlock(&YonaKqueueMutex);
  }
}

void YonaRuntimeKqueueCancel(uint64_t TargetOperationId) {
  pthread_mutex_lock(&YonaKqueueMutex);
  if (!YonaKqueueReady) {
    pthread_mutex_unlock(&YonaKqueueMutex);
    return;
  }
  YonaKqueueWork *Work = findOperation(TargetOperationId);
  if (!Work) {
    pthread_mutex_unlock(&YonaKqueueMutex);
    return;
  }
  Work->Cancelled = 1;
  if (Work->Kind == YonaKqueueConnect || Work->Kind == YonaKqueueAccept ||
      Work->Kind == YonaKqueueSend || Work->Kind == YonaKqueueReceive) {
    struct kevent Event;
    int16_t Filter =
        (Work->Kind == YonaKqueueReceive || Work->Kind == YonaKqueueAccept)
            ? EVFILT_READ
            : EVFILT_WRITE;
    EV_SET(&Event, (uintptr_t)Work->FileDescriptor, Filter, EV_DELETE, 0, 0,
           NULL);
    (void)kevent(YonaKqueue, &Event, 1, NULL, 0, NULL);
    completeOperationLocked(TargetOperationId, -ECANCELED);
  }
  pthread_mutex_unlock(&YonaKqueueMutex);
}

void YonaRuntimeKqueueCancelGroup(const uint64_t *IoIds, int Count) {
  for (int Index = 0; Index < Count; Index++)
    YonaRuntimeKqueueCancel(IoIds[Index]);
}

typedef struct {
  uint64_t OperationId;
  YonaIoContext *Context;
  unsigned char State;
} YonaIoContextSlot;

static YonaIoContextSlot YonaIoContextTable[YONA_IO_CONTEXT_TABLE_SIZE];

enum YonaIoContextSlotKind {
  YonaIoContextSlotEmpty = 0,
  YonaIoContextSlotOccupied = 1,
  YonaIoContextSlotTombstone = 2,
};

static pthread_mutex_t YonaIoContextMutex = PTHREAD_MUTEX_INITIALIZER;

void YonaRuntimeIoContextPut(uint64_t OperationId, YonaIoContext *Context) {
  pthread_mutex_lock(&YonaIoContextMutex);
  unsigned BaseIndex = (unsigned)(OperationId % YONA_IO_CONTEXT_TABLE_SIZE);
  unsigned FirstTombstone = YONA_IO_CONTEXT_TABLE_SIZE;
  for (unsigned ProbeIndex = 0; ProbeIndex < YONA_IO_CONTEXT_TABLE_SIZE;
       ProbeIndex++) {
    unsigned Slot = (BaseIndex + ProbeIndex) % YONA_IO_CONTEXT_TABLE_SIZE;
    if (YonaIoContextTable[Slot].State == YonaIoContextSlotTombstone &&
        FirstTombstone == YONA_IO_CONTEXT_TABLE_SIZE) {
      FirstTombstone = Slot;
      continue;
    }
    if (YonaIoContextTable[Slot].State == YonaIoContextSlotEmpty) {
      unsigned Target =
          FirstTombstone == YONA_IO_CONTEXT_TABLE_SIZE ? Slot : FirstTombstone;
      YonaIoContextTable[Target].OperationId = OperationId;
      YonaIoContextTable[Target].Context = Context;
      YonaIoContextTable[Target].State = YonaIoContextSlotOccupied;
      pthread_mutex_unlock(&YonaIoContextMutex);
      return;
    }
  }
  if (FirstTombstone != YONA_IO_CONTEXT_TABLE_SIZE) {
    YonaIoContextTable[FirstTombstone].OperationId = OperationId;
    YonaIoContextTable[FirstTombstone].Context = Context;
    YonaIoContextTable[FirstTombstone].State = YonaIoContextSlotOccupied;
  }
  pthread_mutex_unlock(&YonaIoContextMutex);
}

YonaIoContext *YonaRuntimeIoContextTake(uint64_t OperationId) {
  pthread_mutex_lock(&YonaIoContextMutex);
  unsigned BaseIndex = (unsigned)(OperationId % YONA_IO_CONTEXT_TABLE_SIZE);
  for (unsigned ProbeIndex = 0; ProbeIndex < YONA_IO_CONTEXT_TABLE_SIZE;
       ProbeIndex++) {
    unsigned Slot = (BaseIndex + ProbeIndex) % YONA_IO_CONTEXT_TABLE_SIZE;
    if (YonaIoContextTable[Slot].State == YonaIoContextSlotOccupied &&
        YonaIoContextTable[Slot].OperationId == OperationId) {
      YonaIoContext *Context = YonaIoContextTable[Slot].Context;
      YonaIoContextTable[Slot].OperationId = 0;
      YonaIoContextTable[Slot].Context = NULL;
      YonaIoContextTable[Slot].State = YonaIoContextSlotTombstone;
      pthread_mutex_unlock(&YonaIoContextMutex);
      return Context;
    }
    if (YonaIoContextTable[Slot].State == YonaIoContextSlotEmpty)
      break;
  }
  pthread_mutex_unlock(&YonaIoContextMutex);
  return NULL;
}
