/* ===== Channel Runtime =====
 *
 * Bounded MPMC channel for inter-task communication.
 * Layout: [Count, Head, Tail, Cap, ring buffer of Cap i64s].
 *
 * send blocks the calling worker thread when the buffer is full.
 * recv blocks when the buffer is empty.
 * close wakes all Waiters; subsequent recv returns None when drained.
 *
 * Designed for use BETWEEN tasks. The async runtime tracks blocked channel
 * Waiters and raises :Deadlock when no runnable task can make progress.
 */

#include "yona/Runtime/Concurrency/Async.h"
#include "yona/Runtime/Concurrency/Channel.h"
#include "yona/Runtime/Core/Api.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define YONA_RUNTIME_TYPE_CHANNEL 20

/* Symbol IDs for channel errors. These are conceptual — at runtime, sym 0 is
 * the generic error symbol. The compile-time intern_symbol gives different
 * IDs for these names but the runtime can't easily reference them without
 * compile-time knowledge. For now use sym 0 with descriptive messages. */
#define YONA_SYMBOL_CANCELLED 0
#define YONA_SYMBOL_DEADLOCK 0
#define YONA_SYMBOL_CHANNEL_CLOSED 0

/* Channel struct.
 * Note: this struct is heap-allocated via YonaRuntimeAllocate, so the returned
 * pointer is offset by RC_HEADER_SIZE (2 i64s) from the actual allocation. */
struct YonaChannel {
  int64_t Cap;
  int64_t Count;
  int64_t Head;
  int64_t Tail;
  int64_t *Buf;
  pthread_mutex_t Mutex;
  pthread_cond_t NotFull;
  pthread_cond_t NotEmpty;
  int Closed;
  int Waiters; /* number of blocked send/recv */
  int SendWaiters;
  int RecvWaiters;
  YonaTaskGroup *Group; /* owning task Group, for cancellation */
  YonaTypeDescriptor PayloadType;
};

/* Allocate Option ADT for recv return.
 * Layout: [tag, num_fields, heap_mask,
 * fields...] (recursive ADT layout) */
static int64_t *chanMakeSome(int64_t Value, int PayloadIsHeap) {
  int64_t *Adt =
      (int64_t *)YonaRuntimeAllocate(4 /* RC_TYPE_ADT */, 4 * sizeof(int64_t));
  if (Adt == NULL)
    return NULL;
  Adt[0] = 0;
  Adt[1] = 1;
  Adt[2] = PayloadIsHeap ? 1 : 0;
  Adt[3] = Value;
  return Adt;
}

static int64_t *chanMakeNone(void) {
  int64_t *Adt =
      (int64_t *)YonaRuntimeAllocate(4 /* RC_TYPE_ADT */, 3 * sizeof(int64_t));
  if (Adt == NULL)
    return NULL;
  Adt[0] = 1;
  Adt[1] = 0;
  Adt[2] = 0;
  return Adt;
}

YonaChannelRef YonaRuntimeChannelCreate(int64_t Cap,
                                        const YonaTypeDescriptor *PayloadType) {
  if (PayloadType == NULL)
    return NULL;
  if (Cap < 1)
    Cap = 1;
  if ((uint64_t)Cap > SIZE_MAX / sizeof(int64_t))
    return NULL;
  int64_t *Buf = (int64_t *)calloc((size_t)Cap, sizeof(int64_t));
  if (Buf == NULL)
    return NULL;
  YonaChannel *Ch = (YonaChannel *)YonaRuntimeAllocate(
      YONA_RUNTIME_TYPE_CHANNEL, sizeof(YonaChannel));
  if (Ch == NULL) {
    free(Buf);
    return NULL;
  }
  Ch->Cap = Cap;
  Ch->Count = 0;
  Ch->Head = 0;
  Ch->Tail = 0;
  Ch->Buf = Buf;
  pthread_mutex_init(&Ch->Mutex, NULL);
  pthread_cond_init(&Ch->NotFull, NULL);
  pthread_cond_init(&Ch->NotEmpty, NULL);
  Ch->Closed = 0;
  Ch->Waiters = 0;
  Ch->Group = NULL; /* TODO(runtime): track task Group for cancellation */
  Ch->PayloadType = *PayloadType;
  return Ch;
}

void YonaRuntimeChannelSend(YonaChannelRef Ch, int64_t Value) {
  pthread_mutex_lock(&Ch->Mutex);
  while (Ch->Count == Ch->Cap && !Ch->Closed) {
    if (Ch->Group && YonaRuntimeTaskGroupIsCancelled(Ch->Group)) {
      pthread_mutex_unlock(&Ch->Mutex);
      YonaRuntimeTypeDescriptorRelease(&Ch->PayloadType, Value);
      YonaRuntimeRaise(YONA_SYMBOL_CANCELLED,
                       "task cancelled while waiting on channel send");
      return;
    }
    if (YonaRuntimeChannelWaitBegin(Ch, 1, Ch->Count, Ch->Cap, Ch->Closed,
                                    Ch->RecvWaiters)) {
      YonaRuntimeChannelWaitEnd();
      pthread_mutex_unlock(&Ch->Mutex);
      YonaRuntimeTypeDescriptorRelease(&Ch->PayloadType, Value);
      YonaRuntimeRaise(YONA_SYMBOL_DEADLOCK,
                       "channel deadlock: send waiting on full "
                       "channel; no runnable tasks remain");
      return;
    }
    Ch->Waiters++;
    Ch->SendWaiters++;
    struct timespec Ts;
    clock_gettime(CLOCK_REALTIME, &Ts);
    Ts.tv_nsec += 100000000; /* 100ms */
    if (Ts.tv_nsec >= 1000000000) {
      Ts.tv_sec++;
      Ts.tv_nsec -= 1000000000;
    }
    pthread_cond_timedwait(&Ch->NotFull, &Ch->Mutex, &Ts);
    Ch->SendWaiters--;
    Ch->Waiters--;
    YonaRuntimeChannelWaitEnd();
  }
  if (Ch->Closed) {
    pthread_mutex_unlock(&Ch->Mutex);
    YonaRuntimeTypeDescriptorRelease(&Ch->PayloadType, Value);
    YonaRuntimeRaise(YONA_SYMBOL_CHANNEL_CLOSED, "send on Closed channel");
    return;
  }
  Ch->Buf[Ch->Tail] = Value;
  Ch->Tail = (Ch->Tail + 1) % Ch->Cap;
  Ch->Count++;
  pthread_cond_signal(&Ch->NotEmpty);
  pthread_mutex_unlock(&Ch->Mutex);
}

int64_t YonaRuntimeChannelReceive(YonaChannelRef Ch) {
  pthread_mutex_lock(&Ch->Mutex);
  while (Ch->Count == 0 && !Ch->Closed) {
    if (Ch->Group && YonaRuntimeTaskGroupIsCancelled(Ch->Group)) {
      pthread_mutex_unlock(&Ch->Mutex);
      YonaRuntimeRaise(YONA_SYMBOL_CANCELLED,
                       "task cancelled while waiting on channel recv");
      return 0;
    }
    if (YonaRuntimeChannelWaitBegin(Ch, 2, Ch->Count, Ch->Cap, Ch->Closed,
                                    Ch->SendWaiters)) {
      YonaRuntimeChannelWaitEnd();
      pthread_mutex_unlock(&Ch->Mutex);
      YonaRuntimeRaise(YONA_SYMBOL_DEADLOCK,
                       "channel deadlock: recv waiting on empty "
                       "open channel; no runnable tasks remain");
      return 0;
    }
    Ch->Waiters++;
    Ch->RecvWaiters++;
    struct timespec Ts;
    clock_gettime(CLOCK_REALTIME, &Ts);
    Ts.tv_nsec += 100000000; /* 100ms */
    if (Ts.tv_nsec >= 1000000000) {
      Ts.tv_sec++;
      Ts.tv_nsec -= 1000000000;
    }
    pthread_cond_timedwait(&Ch->NotEmpty, &Ch->Mutex, &Ts);
    Ch->RecvWaiters--;
    Ch->Waiters--;
    YonaRuntimeChannelWaitEnd();
  }
  if (Ch->Count == 0 && Ch->Closed) {
    /* Channel Closed and drained */
    pthread_mutex_unlock(&Ch->Mutex);
    return (int64_t)(intptr_t)chanMakeNone();
  }
  int64_t Value = Ch->Buf[Ch->Head];
  Ch->Buf[Ch->Head] = 0;
  Ch->Head = (Ch->Head + 1) % Ch->Cap;
  Ch->Count--;
  pthread_cond_signal(&Ch->NotFull);
  pthread_mutex_unlock(&Ch->Mutex);
  int64_t *Result = chanMakeSome(Value, Ch->PayloadType.PayloadIsHeap);
  if (Result == NULL)
    YonaRuntimeTypeDescriptorRelease(&Ch->PayloadType, Value);
  return (int64_t)(intptr_t)Result;
}

int64_t YonaRuntimeChannelTryReceive(YonaChannelRef Ch) {
  pthread_mutex_lock(&Ch->Mutex);
  if (Ch->Count == 0) {
    pthread_mutex_unlock(&Ch->Mutex);
    return (int64_t)(intptr_t)chanMakeNone();
  }
  int64_t Value = Ch->Buf[Ch->Head];
  Ch->Buf[Ch->Head] = 0;
  Ch->Head = (Ch->Head + 1) % Ch->Cap;
  Ch->Count--;
  pthread_cond_signal(&Ch->NotFull);
  pthread_mutex_unlock(&Ch->Mutex);
  int64_t *Result = chanMakeSome(Value, Ch->PayloadType.PayloadIsHeap);
  if (Result == NULL)
    YonaRuntimeTypeDescriptorRelease(&Ch->PayloadType, Value);
  return (int64_t)(intptr_t)Result;
}

void YonaRuntimeChannelClose(YonaChannelRef Ch) {
  pthread_mutex_lock(&Ch->Mutex);
  Ch->Closed = 1;
  pthread_cond_broadcast(&Ch->NotFull);
  pthread_cond_broadcast(&Ch->NotEmpty);
  pthread_mutex_unlock(&Ch->Mutex);
}

int64_t YonaRuntimeChannelIsClosed(YonaChannelRef Ch) {
  pthread_mutex_lock(&Ch->Mutex);
  int Closed = Ch->Closed;
  pthread_mutex_unlock(&Ch->Mutex);
  return Closed ? 1 : 0;
}

int64_t YonaRuntimeChannelLength(YonaChannelRef Ch) {
  pthread_mutex_lock(&Ch->Mutex);
  int64_t N = Ch->Count;
  pthread_mutex_unlock(&Ch->Mutex);
  return N;
}

int64_t YonaRuntimeChannelCapacity(YonaChannelRef Ch) { return Ch->Cap; }

/* Called from rc_dec when refcount hits 0. Takes void* to match dispatch. */
void YonaRuntimeChannelDestroy(YonaChannelRef Channel) {
  if (Channel == NULL)
    return;
  YonaChannel *Ch = Channel;
  /* Wake any straggler Waiters before destroying */
  pthread_mutex_lock(&Ch->Mutex);
  pthread_cond_broadcast(&Ch->NotFull);
  pthread_cond_broadcast(&Ch->NotEmpty);
  if (Ch->PayloadType.Release) {
    while (Ch->Count > 0) {
      int64_t Value = Ch->Buf[Ch->Head];
      Ch->Buf[Ch->Head] = 0;
      Ch->Head = (Ch->Head + 1) % Ch->Cap;
      Ch->Count--;
      YonaRuntimeTypeDescriptorRelease(&Ch->PayloadType, Value);
    }
  }
  pthread_mutex_unlock(&Ch->Mutex);
  pthread_mutex_destroy(&Ch->Mutex);
  pthread_cond_destroy(&Ch->NotFull);
  pthread_cond_destroy(&Ch->NotEmpty);
  free(Ch->Buf);
  /* The channel struct itself is freed by rc_dec via the standard pool free */
}

/* ===== Std\Channel wrappers (i64-based for codegen) ===== */

void *YonaStdChannelChannel(int64_t Cap,
                            const YonaTypeDescriptor *PayloadType) {
  int64_t Raw = (int64_t)(intptr_t)YonaRuntimeChannelCreate(Cap, PayloadType);
  if (Raw == 0)
    return NULL;
  void *Left = YonaRuntimeAdtAllocate(0, 1);
  void *Right = YonaRuntimeAdtAllocate(0, 1);
  if (Left == NULL || Right == NULL) {
    YonaRuntimeRelease(Left);
    YonaRuntimeRelease(Right);
    YonaRuntimeRelease((void *)(intptr_t)Raw);
    return NULL;
  }
  YonaRuntimeRetain((void *)(intptr_t)Raw);
  YonaRuntimeAdtSetField(Left, 0, Raw);
  YonaRuntimeAdtSetField(Right, 0, Raw);
  YonaRuntimeAdtSetHeapMask(Left, 1);
  YonaRuntimeAdtSetHeapMask(Right, 1);
  void *Tuple = YonaRuntimeTupleAllocate(2);
  if (Tuple == NULL) {
    YonaRuntimeRelease(Left);
    YonaRuntimeRelease(Right);
    return NULL;
  }
  YonaRuntimeTupleSet(Tuple, 0, (int64_t)(intptr_t)Left);
  YonaRuntimeTupleSet(Tuple, 1, (int64_t)(intptr_t)Right);
  YonaRuntimeTupleSetHeapMask(Tuple, 3);
  return Tuple;
}

void *YonaStdGpuGpuFloatChannel(int64_t Cap) {
  return YonaStdChannelChannel(Cap, &YonaRuntimeReferenceTypeDescriptor);
}

int64_t YonaStdChannelSend(int64_t ChI64, int64_t Value) {
  YonaRuntimeChannelSend((YonaChannel *)(intptr_t)ChI64, Value);
  return 0;
}

int64_t YonaStdChannelRawSend(int64_t ChI64, int64_t Value) {
  return YonaStdChannelSend(ChI64, Value);
}

int64_t YonaStdChannelRecv(int64_t ChI64) {
  return YonaRuntimeChannelReceive((YonaChannel *)(intptr_t)ChI64);
}

int64_t YonaStdChannelRawRecv(int64_t ChI64) {
  return YonaStdChannelRecv(ChI64);
}

int64_t YonaStdChannelTryRecv(int64_t ChI64) {
  return YonaRuntimeChannelTryReceive((YonaChannel *)(intptr_t)ChI64);
}

int64_t YonaStdChannelRawTryRecv(int64_t ChI64) {
  return YonaStdChannelTryRecv(ChI64);
}

int64_t YonaStdChannelClose(int64_t ChI64) {
  YonaRuntimeChannelClose((YonaChannel *)(intptr_t)ChI64);
  return 0;
}

int64_t YonaStdChannelRawClose(int64_t ChI64) {
  return YonaStdChannelClose(ChI64);
}

int64_t YonaStdChannelIsClosed(int64_t ChI64) {
  return YonaRuntimeChannelIsClosed((YonaChannel *)(intptr_t)ChI64);
}

int64_t YonaStdChannelRawIsClosed(int64_t ChI64) {
  return YonaStdChannelIsClosed(ChI64);
}

int64_t YonaStdChannelLength(int64_t ChI64) {
  return YonaRuntimeChannelLength((YonaChannel *)(intptr_t)ChI64);
}

int64_t YonaStdChannelRawLength(int64_t ChI64) {
  return YonaStdChannelLength(ChI64);
}

int64_t YonaStdChannelCapacity(int64_t ChI64) {
  return YonaRuntimeChannelCapacity((YonaChannel *)(intptr_t)ChI64);
}

int64_t YonaStdChannelRawCapacity(int64_t ChI64) {
  return YonaStdChannelCapacity(ChI64);
}

/* ===== Std\Task — task spawning ===== */

/* YonaRuntimeAsyncSpawnClosure is defined in async_posix.c which is #included
 * before channel_posix.c, so the function is already in scope. We use it
 * directly without a forward declaration. */

/* spawn: takes a Yona Closure (zero-arity thunk), returns a promise.
 * The codegen treats this as IO so the result is auto-awaited at use site.
 * The Closure runs on a thread pool worker concurrently with the caller. */
YonaTaskRef YonaStdTaskSpawn(int64_t *Closure,
                             const YonaTypeDescriptor *ResultType) {
  return YonaRuntimeAsyncSpawnClosure(Closure, ResultType, NULL);
}
