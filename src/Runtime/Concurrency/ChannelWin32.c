/* ===== Channel Runtime (Windows native) =====
 * Same behavior as channel_posix.c using CRITICAL_SECTION + CONDITION_VARIABLE.
 */

#ifndef _WIN32
#error "channel_win32.c is for Windows builds only"
#endif

#include "yona/Runtime/Concurrency/Async.h"
#include "yona/Runtime/Concurrency/Channel.h"
#include "yona/Runtime/Core/Api.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define YONA_RC_TYPE_CHANNEL 20

#define YONA_SYM_CANCELLED 0
#define YONA_SYM_DEADLOCK 0
#define YONA_SYM_CHANNEL_CLOSED 0

struct YonaChannel {
  int64_t Cap;
  int64_t Count;
  int64_t Head;
  int64_t Tail;
  int64_t *Buf;
  CRITICAL_SECTION Mutex;
  CONDITION_VARIABLE NotFull;
  CONDITION_VARIABLE NotEmpty;
  int Closed;
  int Waiters;
  int SendWaiters;
  int RecvWaiters;
  YonaTaskGroup *Group;
  YonaTypeDescriptor PayloadType;
};

static int64_t *chanMakeSome(int64_t Value, int PayloadIsHeap) {
  int64_t *Adt = (int64_t *)YonaRuntimeAllocate(4, 4 * sizeof(int64_t));
  if (Adt == NULL)
    return NULL;
  Adt[0] = 0;
  Adt[1] = 1;
  Adt[2] = PayloadIsHeap ? 1 : 0;
  Adt[3] = Value;
  return Adt;
}

static int64_t *chanMakeNone(void) {
  int64_t *Adt = (int64_t *)YonaRuntimeAllocate(4, 3 * sizeof(int64_t));
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
  YonaChannel *Ch = (YonaChannel *)YonaRuntimeAllocate(YONA_RC_TYPE_CHANNEL,
                                                       sizeof(YonaChannel));
  if (Ch == NULL) {
    free(Buf);
    return NULL;
  }
  Ch->Cap = Cap;
  Ch->Count = 0;
  Ch->Head = 0;
  Ch->Tail = 0;
  Ch->Buf = Buf;
  InitializeCriticalSection(&Ch->Mutex);
  InitializeConditionVariable(&Ch->NotFull);
  InitializeConditionVariable(&Ch->NotEmpty);
  Ch->Closed = 0;
  Ch->Waiters = 0;
  Ch->Group = NULL;
  Ch->PayloadType = *PayloadType;
  return Ch;
}

void YonaRuntimeChannelSend(YonaChannelRef Ch, int64_t Value) {
  EnterCriticalSection(&Ch->Mutex);
  while (Ch->Count == Ch->Cap && !Ch->Closed) {
    if (Ch->Group && YonaRuntimeTaskGroupIsCancelled(Ch->Group)) {
      LeaveCriticalSection(&Ch->Mutex);
      YonaRuntimeTypeDescriptorRelease(&Ch->PayloadType, Value);
      YonaRuntimeRaise(YONA_SYM_CANCELLED,
                       "task cancelled while waiting on channel send");
      return;
    }
    if (YonaRuntimeChannelWaitBegin(Ch, 1, Ch->Count, Ch->Cap, Ch->Closed,
                                    Ch->RecvWaiters)) {
      YonaRuntimeChannelWaitEnd();
      LeaveCriticalSection(&Ch->Mutex);
      YonaRuntimeTypeDescriptorRelease(&Ch->PayloadType, Value);
      YonaRuntimeRaise(YONA_SYM_DEADLOCK,
                       "channel deadlock: send waiting on full "
                       "channel; no runnable tasks remain");
      return;
    }
    Ch->Waiters++;
    Ch->SendWaiters++;
    SleepConditionVariableCS(&Ch->NotFull, &Ch->Mutex, 100);
    Ch->SendWaiters--;
    Ch->Waiters--;
    YonaRuntimeChannelWaitEnd();
  }
  if (Ch->Closed) {
    LeaveCriticalSection(&Ch->Mutex);
    YonaRuntimeTypeDescriptorRelease(&Ch->PayloadType, Value);
    YonaRuntimeRaise(YONA_SYM_CHANNEL_CLOSED, "send on closed channel");
    return;
  }
  Ch->Buf[Ch->Tail] = Value;
  Ch->Tail = (Ch->Tail + 1) % Ch->Cap;
  Ch->Count++;
  WakeConditionVariable(&Ch->NotEmpty);
  LeaveCriticalSection(&Ch->Mutex);
}

int64_t YonaRuntimeChannelReceive(YonaChannelRef Ch) {
  EnterCriticalSection(&Ch->Mutex);
  while (Ch->Count == 0 && !Ch->Closed) {
    if (Ch->Group && YonaRuntimeTaskGroupIsCancelled(Ch->Group)) {
      LeaveCriticalSection(&Ch->Mutex);
      YonaRuntimeRaise(YONA_SYM_CANCELLED,
                       "task cancelled while waiting on channel recv");
      return 0;
    }
    if (YonaRuntimeChannelWaitBegin(Ch, 2, Ch->Count, Ch->Cap, Ch->Closed,
                                    Ch->SendWaiters)) {
      YonaRuntimeChannelWaitEnd();
      LeaveCriticalSection(&Ch->Mutex);
      YonaRuntimeRaise(YONA_SYM_DEADLOCK,
                       "channel deadlock: recv waiting on empty "
                       "open channel; no runnable tasks remain");
      return 0;
    }
    Ch->Waiters++;
    Ch->RecvWaiters++;
    SleepConditionVariableCS(&Ch->NotEmpty, &Ch->Mutex, 100);
    Ch->RecvWaiters--;
    Ch->Waiters--;
    YonaRuntimeChannelWaitEnd();
  }
  if (Ch->Count == 0 && Ch->Closed) {
    LeaveCriticalSection(&Ch->Mutex);
    return (int64_t)(intptr_t)chanMakeNone();
  }
  int64_t Value = Ch->Buf[Ch->Head];
  Ch->Buf[Ch->Head] = 0;
  Ch->Head = (Ch->Head + 1) % Ch->Cap;
  Ch->Count--;
  WakeConditionVariable(&Ch->NotFull);
  LeaveCriticalSection(&Ch->Mutex);
  int64_t *Result = chanMakeSome(Value, Ch->PayloadType.PayloadIsHeap);
  if (Result == NULL)
    YonaRuntimeTypeDescriptorRelease(&Ch->PayloadType, Value);
  return (int64_t)(intptr_t)Result;
}

int64_t YonaRuntimeChannelTryReceive(YonaChannelRef Ch) {
  EnterCriticalSection(&Ch->Mutex);
  if (Ch->Count == 0) {
    LeaveCriticalSection(&Ch->Mutex);
    return (int64_t)(intptr_t)chanMakeNone();
  }
  int64_t Value = Ch->Buf[Ch->Head];
  Ch->Buf[Ch->Head] = 0;
  Ch->Head = (Ch->Head + 1) % Ch->Cap;
  Ch->Count--;
  WakeConditionVariable(&Ch->NotFull);
  LeaveCriticalSection(&Ch->Mutex);
  int64_t *Result = chanMakeSome(Value, Ch->PayloadType.PayloadIsHeap);
  if (Result == NULL)
    YonaRuntimeTypeDescriptorRelease(&Ch->PayloadType, Value);
  return (int64_t)(intptr_t)Result;
}

void YonaRuntimeChannelClose(YonaChannelRef Ch) {
  EnterCriticalSection(&Ch->Mutex);
  Ch->Closed = 1;
  WakeAllConditionVariable(&Ch->NotFull);
  WakeAllConditionVariable(&Ch->NotEmpty);
  LeaveCriticalSection(&Ch->Mutex);
}

int64_t YonaRuntimeChannelIsClosed(YonaChannelRef Ch) {
  EnterCriticalSection(&Ch->Mutex);
  int Closed = Ch->Closed;
  LeaveCriticalSection(&Ch->Mutex);
  return Closed ? 1 : 0;
}

int64_t YonaRuntimeChannelLength(YonaChannelRef Ch) {
  EnterCriticalSection(&Ch->Mutex);
  int64_t N = Ch->Count;
  LeaveCriticalSection(&Ch->Mutex);
  return N;
}

int64_t YonaRuntimeChannelCapacity(YonaChannelRef Ch) { return Ch->Cap; }

void YonaRuntimeChannelDestroy(YonaChannelRef Channel) {
  if (Channel == NULL)
    return;
  YonaChannel *Ch = Channel;
  EnterCriticalSection(&Ch->Mutex);
  WakeAllConditionVariable(&Ch->NotFull);
  WakeAllConditionVariable(&Ch->NotEmpty);
  if (Ch->PayloadType.Release) {
    while (Ch->Count > 0) {
      int64_t Value = Ch->Buf[Ch->Head];
      Ch->Buf[Ch->Head] = 0;
      Ch->Head = (Ch->Head + 1) % Ch->Cap;
      Ch->Count--;
      YonaRuntimeTypeDescriptorRelease(&Ch->PayloadType, Value);
    }
  }
  LeaveCriticalSection(&Ch->Mutex);
  DeleteCriticalSection(&Ch->Mutex);
  free(Ch->Buf);
}

void *YonaStdChannelChannel(int64_t Cap,
                            const YonaTypeDescriptor *PayloadType) {
  int64_t Raw = (int64_t)(intptr_t)YonaRuntimeChannelCreate(Cap, PayloadType);
  if (Raw == 0)
    return NULL;
  void *Sender = YonaRuntimeAdtAllocate(0, 1);
  void *Receiver = YonaRuntimeAdtAllocate(0, 1);
  if (Sender == NULL || Receiver == NULL) {
    YonaRuntimeRelease(Sender);
    YonaRuntimeRelease(Receiver);
    YonaRuntimeRelease((void *)(intptr_t)Raw);
    return NULL;
  }
  YonaRuntimeRetain((void *)(intptr_t)Raw);
  YonaRuntimeAdtSetField(Sender, 0, Raw);
  YonaRuntimeAdtSetField(Receiver, 0, Raw);
  YonaRuntimeAdtSetHeapMask(Sender, 1);
  YonaRuntimeAdtSetHeapMask(Receiver, 1);

  void *SenderLinear = YonaRuntimeAdtAllocate(0, 1);
  void *ReceiverLinear = YonaRuntimeAdtAllocate(0, 1);
  if (SenderLinear == NULL || ReceiverLinear == NULL) {
    YonaRuntimeRelease(SenderLinear);
    YonaRuntimeRelease(ReceiverLinear);
    YonaRuntimeRelease(Sender);
    YonaRuntimeRelease(Receiver);
    return NULL;
  }
  YonaRuntimeAdtSetField(SenderLinear, 0, (int64_t)(intptr_t)Sender);
  YonaRuntimeAdtSetField(ReceiverLinear, 0, (int64_t)(intptr_t)Receiver);
  YonaRuntimeAdtSetHeapMask(SenderLinear, 1);
  YonaRuntimeAdtSetHeapMask(ReceiverLinear, 1);

  void *Tuple = YonaRuntimeTupleAllocate(2);
  if (Tuple == NULL) {
    YonaRuntimeRelease(SenderLinear);
    YonaRuntimeRelease(ReceiverLinear);
    return NULL;
  }
  YonaRuntimeTupleSet(Tuple, 0, (int64_t)(intptr_t)SenderLinear);
  YonaRuntimeTupleSet(Tuple, 1, (int64_t)(intptr_t)ReceiverLinear);
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

YonaTaskRef YonaStdTaskSpawn(int64_t *Closure,
                             const YonaTypeDescriptor *ResultType) {
  return YonaRuntimeAsyncSpawnClosure(Closure, ResultType, NULL);
}
