/*
 * Single io_uring instance + io_ctx table for all Linux platform TUs.
 */

#include "yona/Runtime/Platform/IoUring.h"

#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

static inline int yonaIoUringSetup(unsigned Entries,
                                   struct io_uring_params *Parameters) {
  return (int)syscall(__NR_io_uring_setup, Entries, Parameters);
}

static inline int yonaIoUringEnter(int RingFileDescriptor,
                                   unsigned SubmissionCount,
                                   unsigned MinimumCompletionCount,
                                   unsigned Flags) {
  return (int)syscall(__NR_io_uring_enter, RingFileDescriptor, SubmissionCount,
                      MinimumCompletionCount, Flags, NULL, 0);
}

typedef struct {
  int RingFileDescriptor;
  unsigned *SubmissionHead, *SubmissionTail, *SubmissionRingMask,
      *SubmissionRingEntries;
  unsigned *SubmissionArray;
  struct io_uring_sqe *SubmissionEntries;
  void *SubmissionRing;
  size_t SubmissionRingSize;
  unsigned *CompletionHead, *CompletionTail, *CompletionRingMask,
      *CompletionRingEntries;
  struct io_uring_cqe *CompletionEntries;
  void *CompletionRing;
  size_t CompletionRingSize;
  atomic_uint_fast64_t NextOperationId;
  int Initialized;
} YonaIoUring;

static YonaIoUring YonaIoRing = {0};
static pthread_mutex_t YonaIoRingMutex = PTHREAD_MUTEX_INITIALIZER;

static int initializeIoUring(void) {
  if (YonaIoRing.Initialized)
    return 0;

  struct io_uring_params Parameters;
  memset(&Parameters, 0, sizeof(Parameters));

  int FileDescriptor = yonaIoUringSetup(256, &Parameters);
  if (FileDescriptor < 0)
    return -1;

  YonaIoRing.RingFileDescriptor = FileDescriptor;

  size_t SubmissionRingSize =
      Parameters.sq_off.array + Parameters.sq_entries * sizeof(unsigned);
  void *SubmissionRing =
      mmap(0, SubmissionRingSize, PROT_READ | PROT_WRITE,
           MAP_SHARED | MAP_POPULATE, FileDescriptor, IORING_OFF_SQ_RING);
  if (SubmissionRing == MAP_FAILED) {
    close(FileDescriptor);
    return -1;
  }

  YonaIoRing.SubmissionRing = SubmissionRing;
  YonaIoRing.SubmissionRingSize = SubmissionRingSize;
  YonaIoRing.SubmissionHead = SubmissionRing + Parameters.sq_off.head;
  YonaIoRing.SubmissionTail = SubmissionRing + Parameters.sq_off.tail;
  YonaIoRing.SubmissionRingMask = SubmissionRing + Parameters.sq_off.ring_mask;
  YonaIoRing.SubmissionRingEntries =
      SubmissionRing + Parameters.sq_off.ring_entries;
  YonaIoRing.SubmissionArray = SubmissionRing + Parameters.sq_off.array;

  size_t SubmissionEntriesSize =
      Parameters.sq_entries * sizeof(struct io_uring_sqe);
  YonaIoRing.SubmissionEntries =
      mmap(0, SubmissionEntriesSize, PROT_READ | PROT_WRITE,
           MAP_SHARED | MAP_POPULATE, FileDescriptor, IORING_OFF_SQES);
  if (YonaIoRing.SubmissionEntries == MAP_FAILED) {
    munmap(SubmissionRing, SubmissionRingSize);
    close(FileDescriptor);
    return -1;
  }

  size_t CompletionRingSize =
      Parameters.cq_off.cqes +
      Parameters.cq_entries * sizeof(struct io_uring_cqe);
  void *CompletionRing;
  if (Parameters.features & IORING_FEAT_SINGLE_MMAP) {
    CompletionRing = SubmissionRing;
  } else {
    CompletionRing =
        mmap(0, CompletionRingSize, PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_POPULATE, FileDescriptor, IORING_OFF_CQ_RING);
    if (CompletionRing == MAP_FAILED) {
      munmap(YonaIoRing.SubmissionEntries, SubmissionEntriesSize);
      munmap(SubmissionRing, SubmissionRingSize);
      close(FileDescriptor);
      return -1;
    }
  }

  YonaIoRing.CompletionRing = CompletionRing;
  YonaIoRing.CompletionRingSize = CompletionRingSize;
  YonaIoRing.CompletionHead = CompletionRing + Parameters.cq_off.head;
  YonaIoRing.CompletionTail = CompletionRing + Parameters.cq_off.tail;
  YonaIoRing.CompletionRingMask = CompletionRing + Parameters.cq_off.ring_mask;
  YonaIoRing.CompletionRingEntries =
      CompletionRing + Parameters.cq_off.ring_entries;
  YonaIoRing.CompletionEntries = CompletionRing + Parameters.cq_off.cqes;

  atomic_init(&YonaIoRing.NextOperationId, 1);
  YonaIoRing.Initialized = 1;
  return 0;
}

uint64_t YonaRuntimeIoUringSubmit(struct io_uring_sqe *EntryTemplate) {
  pthread_mutex_lock(&YonaIoRingMutex);
  if (!YonaIoRing.Initialized && initializeIoUring() != 0) {
    pthread_mutex_unlock(&YonaIoRingMutex);
    return 0;
  }
  unsigned Tail = *YonaIoRing.SubmissionTail;
  unsigned Mask = *YonaIoRing.SubmissionRingMask;
  unsigned Index = Tail & Mask;
  uint64_t OperationId = atomic_fetch_add(&YonaIoRing.NextOperationId, 1);
  struct io_uring_sqe *Entry = &YonaIoRing.SubmissionEntries[Index];
  *Entry = *EntryTemplate;
  Entry->user_data = OperationId;
  YonaIoRing.SubmissionArray[Index] = Index;
  __atomic_store_n(YonaIoRing.SubmissionTail, Tail + 1, __ATOMIC_RELEASE);
  yonaIoUringEnter(YonaIoRing.RingFileDescriptor, 1, 0, 0);
  pthread_mutex_unlock(&YonaIoRingMutex);
  return OperationId;
}

/* Completions for other in-flight ids must be stashed; otherwise awaiting
 * accept while connect finished first would skip/drop the connect CQE. */
#define YONA_IO_RING_PENDING_CAPACITY 256
typedef struct {
  uint64_t OperationId;
  int32_t Result;
  int Used;
} YonaPendingCompletion;

static YonaPendingCompletion
    YonaPendingCompletions[YONA_IO_RING_PENDING_CAPACITY];

static int storePendingCompletion(uint64_t OperationId, int32_t Result) {
  for (int Index = 0; Index < YONA_IO_RING_PENDING_CAPACITY; Index++) {
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
  for (int Index = 0; Index < YONA_IO_RING_PENDING_CAPACITY; Index++) {
    if (YonaPendingCompletions[Index].Used &&
        YonaPendingCompletions[Index].OperationId == OperationId) {
      *Output = YonaPendingCompletions[Index].Result;
      YonaPendingCompletions[Index].Used = 0;
      return 1;
    }
  }
  return 0;
}

int32_t YonaRuntimeIoUringAwait(uint64_t OperationId) {
  for (;;) {
    pthread_mutex_lock(&YonaIoRingMutex);
    if (!YonaIoRing.Initialized) {
      pthread_mutex_unlock(&YonaIoRingMutex);
      return -1;
    }
    int32_t StashedResult = 0;
    if (takePendingCompletion(OperationId, &StashedResult)) {
      pthread_mutex_unlock(&YonaIoRingMutex);
      return StashedResult;
    }
    unsigned Head =
        __atomic_load_n(YonaIoRing.CompletionHead, __ATOMIC_ACQUIRE);
    unsigned Tail =
        __atomic_load_n(YonaIoRing.CompletionTail, __ATOMIC_ACQUIRE);
    unsigned ConsumedCount = 0;
    int32_t FoundResult = 0;
    int Found = 0;
    while (Head != Tail) {
      unsigned Mask = *YonaIoRing.CompletionRingMask;
      struct io_uring_cqe *Completion =
          &YonaIoRing.CompletionEntries[Head & Mask];
      if (Completion->user_data == OperationId) {
        FoundResult = Completion->res;
        Found = 1;
      } else {
        (void)storePendingCompletion(Completion->user_data, Completion->res);
      }
      Head++;
      ConsumedCount++;
    }
    if (ConsumedCount)
      __atomic_store_n(
          YonaIoRing.CompletionHead,
          __atomic_load_n(YonaIoRing.CompletionHead, __ATOMIC_RELAXED) +
              ConsumedCount,
          __ATOMIC_RELEASE);
    pthread_mutex_unlock(&YonaIoRingMutex);
    if (Found)
      return FoundResult;
    yonaIoUringEnter(YonaIoRing.RingFileDescriptor, 0, 1,
                     IORING_ENTER_GETEVENTS);
  }
}

void YonaRuntimeIoUringCancel(uint64_t TargetOperationId) {
  if (!YonaIoRing.Initialized)
    return;
  pthread_mutex_lock(&YonaIoRingMutex);
  unsigned Tail = *YonaIoRing.SubmissionTail;
  unsigned Mask = *YonaIoRing.SubmissionRingMask;
  unsigned Index = Tail & Mask;
  struct io_uring_sqe *Entry = &YonaIoRing.SubmissionEntries[Index];
  memset(Entry, 0, sizeof(*Entry));
  Entry->opcode = IORING_OP_ASYNC_CANCEL;
  Entry->addr = TargetOperationId;
  Entry->user_data = atomic_fetch_add(&YonaIoRing.NextOperationId, 1);
  YonaIoRing.SubmissionArray[Index] = Index;
  __atomic_store_n(YonaIoRing.SubmissionTail, Tail + 1, __ATOMIC_RELEASE);
  yonaIoUringEnter(YonaIoRing.RingFileDescriptor, 1, 0, 0);
  pthread_mutex_unlock(&YonaIoRingMutex);
}

void YonaRuntimeIoUringCancelGroup(uint64_t *IoIds, int Count) {
  for (int Index = 0; Index < Count; Index++)
    YonaRuntimeIoUringCancel(IoIds[Index]);
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
