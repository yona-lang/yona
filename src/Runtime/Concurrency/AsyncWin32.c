/* ===== Async Runtime (Windows native) =====
 *
 * Same semantics as async_posix.c: fixed-Size Thread pool, promises,
 * structured concurrency. Uses CRITICAL_SECTION + CONDITION_VARIABLE
 * instead of pthread.
 */

#ifndef _WIN32
#Error "async_win32.c is for Windows builds only"
#endif

#include "yona/Runtime/Concurrency/Async.h"
#include "yona/Runtime/Platform/SjLj.h"

#include <stdint.h>
#include <stdlib.h>
#include <windows.h>

void YonaRuntimeArenaDestroy(void *ArenaPtr);

#define YONA_POOL_SIZE 8
#define YONA_POOL_MAX_THREADS 32
#define YONA_GROUP_INITIAL_CAP 8

void *YonaRuntimeTryBegin(void);
void YonaRuntimeTryEnd(void);
void YonaRuntimeRaise(int64_t Symbol, const char *Message);
int64_t YonaRuntimeGetExceptionSymbol(void);
const char *YonaRuntimeGetExceptionMessage(void);

struct YonaTask {
  int64_t Result;
  YonaTypeDescriptor ResultType;
  int ResultOwned;
  int Completed;
  int Error;
  CRITICAL_SECTION Mutex;
  CONDITION_VARIABLE Cond;
};

struct YonaTaskGroup {
  int Cancelled;
  int PendingCount;
  YonaTask **Children;
  int ChildCount, ChildCap;
  uint64_t *IoChildren;
  int IoChildCount, IoChildCap;
  int64_t FirstErrorSymbol;
  const char *FirstErrorMsg;
  int HasError;
  void *Arena;
  CRITICAL_SECTION Mutex;
  CONDITION_VARIABLE DoneCond;
};

YonaTaskGroupRef YonaRuntimeTaskGroupBegin(void) {
  YonaTaskGroup *G = (YonaTaskGroup *)calloc(1, sizeof(YonaTaskGroup));
  if (G == NULL)
    return NULL;
  G->ChildCap = YONA_GROUP_INITIAL_CAP;
  G->Children = (YonaTask **)malloc(G->ChildCap * sizeof(YonaTask *));
  G->IoChildCap = YONA_GROUP_INITIAL_CAP;
  G->IoChildren = (uint64_t *)malloc(G->IoChildCap * sizeof(uint64_t));
  if (G->Children == NULL || G->IoChildren == NULL) {
    free(G->Children);
    free(G->IoChildren);
    free(G);
    return NULL;
  }
  InitializeCriticalSection(&G->Mutex);
  InitializeConditionVariable(&G->DoneCond);
  return G;
}

int YonaRuntimeTaskGroupRegister(YonaTaskGroupRef Group, YonaTaskRef Task) {
  if (!Group || !Task)
    return 0;
  EnterCriticalSection(&Group->Mutex);
  if (Group->ChildCount >= Group->ChildCap) {
    const int NewCapacity = Group->ChildCap * 2;
    YonaTask **Children =
        (YonaTask **)realloc(Group->Children, NewCapacity * sizeof(YonaTask *));
    if (Children == NULL) {
      LeaveCriticalSection(&Group->Mutex);
      return 0;
    }
    Group->ChildCap = NewCapacity;
    Group->Children = Children;
  }
  Group->Children[Group->ChildCount++] = Task;
  (void)__atomic_fetch_add(&Group->PendingCount, 1, __ATOMIC_SEQ_CST);
  LeaveCriticalSection(&Group->Mutex);
  return 1;
}

int YonaRuntimeTaskGroupRegisterIo(YonaTaskGroupRef G, uint64_t IoId) {
  if (!G)
    return 0;
  EnterCriticalSection(&G->Mutex);
  if (G->IoChildCount >= G->IoChildCap) {
    const int NewCapacity = G->IoChildCap * 2;
    uint64_t *Children =
        (uint64_t *)realloc(G->IoChildren, NewCapacity * sizeof(uint64_t));
    if (Children == NULL) {
      LeaveCriticalSection(&G->Mutex);
      return 0;
    }
    G->IoChildCap = NewCapacity;
    G->IoChildren = Children;
  }
  G->IoChildren[G->IoChildCount++] = IoId;
  (void)__atomic_fetch_add(&G->PendingCount, 1, __ATOMIC_SEQ_CST);
  LeaveCriticalSection(&G->Mutex);
  return 1;
}

void YonaRuntimeTaskGroupCancel(YonaTaskGroupRef G);

int YonaRuntimeTaskGroupIsCancelled(YonaTaskGroupRef G) {
  if (!G)
    return 0;
  return __atomic_load_n(&G->Cancelled, __ATOMIC_SEQ_CST);
}

int64_t YonaRuntimeTaskGroupAwaitAll(YonaTaskGroupRef G);

void YonaRuntimeTaskGroupAttachArena(YonaTaskGroupRef G, void *Arena) {
  if (!G)
    return;
  G->Arena = Arena;
}

void YonaRuntimeTaskGroupDetachArena(YonaTaskGroupRef Group) {
  YonaTaskGroup *TaskGroup = Group;
  if (!TaskGroup || !TaskGroup->Arena)
    return;
  YonaRuntimeArenaDestroy(TaskGroup->Arena);
  TaskGroup->Arena = NULL;
}

static void destroyPromise(YonaTask *P) {
  if (!P)
    return;
  EnterCriticalSection(&P->Mutex);
  while (!P->Completed)
    SleepConditionVariableCS(&P->Cond, &P->Mutex, INFINITE);
  int64_t Result = P->Result;
  int ResultOwned = P->ResultOwned;
  P->ResultOwned = 0;
  LeaveCriticalSection(&P->Mutex);
  if (ResultOwned)
    YonaRuntimeTypeDescriptorRelease(&P->ResultType, Result);
  DeleteCriticalSection(&P->Mutex);
  free(P);
}

void YonaRuntimeTaskGroupEnd(YonaTaskGroupRef Group) {
  YonaTaskGroup *TaskGroup = Group;
  if (!TaskGroup)
    return;
  YonaRuntimeTaskGroupDetachArena(TaskGroup);
  for (int I = 0; I < TaskGroup->ChildCount; I++)
    destroyPromise(TaskGroup->Children[I]);
  DeleteCriticalSection(&TaskGroup->Mutex);
  free(TaskGroup->Children);
  free(TaskGroup->IoChildren);
  free(TaskGroup);
}

typedef struct YonaWorkItem {
  YonaAsyncFunction Fn;
  int64_t Arg;
  void *OwnedContext;
  YonaTask *Promise;
  YonaTaskGroup *Group;
  struct YonaWorkItem *Next;
} YonaWorkItem;

static YonaWorkItem *YonaTaskHead = NULL;
static YonaWorkItem *YonaTaskTail = NULL;
static CRITICAL_SECTION YonaPoolMutex;
static CONDITION_VARIABLE YonaPoolCond;
static INIT_ONCE YonaPoolInitOnce = INIT_ONCE_STATIC_INIT;

/* Channel liveness tracking and managed blocking. */
static CRITICAL_SECTION YonaLivenessMutex;
static INIT_ONCE YonaLivenessInitOnce = INIT_ONCE_STATIC_INIT;
static int64_t YonaNextTaskId = 1;
static int YonaWorkerThreads = 0;
static int YonaQueuedTasks = 0;
static int YonaRunningWorkers = 0;
static int YonaBlockedWorkers = 0;
static int YonaActiveExternalTasks = 0;
static int YonaExternalWaiters = 0;
static __declspec(thread) int64_t YonaCurrentTaskId = 0;
static __declspec(thread) int YonaCurrentTaskIsWorker = 0;
static __declspec(thread) int YonaExternalTaskRegistered = 0;
static __declspec(thread) int YonaChannelWaitKind =
    0; /* 1 worker, 2 external */
static __declspec(thread) int YonaDeadlockCandidateSeen = 0;

static DWORD WINAPI yonaPoolWorkerWin32(void *Unused);

static BOOL CALLBACK yonaLivenessInitOnceCb(PINIT_ONCE InitOnce,
                                            PVOID Parameter, PVOID *Context) {
  (void)InitOnce;
  (void)Parameter;
  (void)Context;
  InitializeCriticalSection(&YonaLivenessMutex);
  return TRUE;
}

static void livenessInit(void) {
  InitOnceExecuteOnce(&YonaLivenessInitOnce, yonaLivenessInitOnceCb, NULL,
                      NULL);
}

static void startPoolWorkerUnlocked(void) {
  HANDLE H = CreateThread(NULL, 0, yonaPoolWorkerWin32, NULL, 0, NULL);
  if (H) {
    CloseHandle(H);
    YonaWorkerThreads++;
  }
}

static void maybeSpawnCompensationWorkerUnlocked(void) {
  if (YonaQueuedTasks <= 0)
    return;
  if (YonaWorkerThreads >= YONA_POOL_MAX_THREADS)
    return;
  if (YonaRunningWorkers > 0)
    return;
  startPoolWorkerUnlocked();
}

static void livenessRegisterExternalTaskUnlocked(void) {
  if (YonaCurrentTaskIsWorker || YonaExternalTaskRegistered)
    return;
  YonaExternalTaskRegistered = 1;
  YonaCurrentTaskId = YonaNextTaskId++;
  YonaActiveExternalTasks++;
}

static void livenessTaskQueued(void) {
  livenessInit();
  EnterCriticalSection(&YonaLivenessMutex);
  YonaQueuedTasks++;
  LeaveCriticalSection(&YonaLivenessMutex);
}

static void livenessWorkerBegin(void) {
  livenessInit();
  EnterCriticalSection(&YonaLivenessMutex);
  if (YonaQueuedTasks > 0)
    YonaQueuedTasks--;
  YonaRunningWorkers++;
  YonaCurrentTaskId = YonaNextTaskId++;
  YonaCurrentTaskIsWorker = 1;
  LeaveCriticalSection(&YonaLivenessMutex);
}

static void livenessWorkerEnd(void) {
  livenessInit();
  EnterCriticalSection(&YonaLivenessMutex);
  if (YonaChannelWaitKind == 1) {
    if (YonaBlockedWorkers > 0)
      YonaBlockedWorkers--;
    YonaChannelWaitKind = 0;
  } else if (YonaCurrentTaskIsWorker && YonaRunningWorkers > 0) {
    YonaRunningWorkers--;
  }
  YonaCurrentTaskId = 0;
  YonaCurrentTaskIsWorker = 0;
  LeaveCriticalSection(&YonaLivenessMutex);
}

int YonaRuntimeChannelWaitBegin(void *Channel, int Op, int64_t Count,
                                int64_t Cap, int Closed, int OppositeWaiters) {
  (void)Channel;
  (void)Op;
  (void)Count;
  (void)Cap;
  (void)Closed;
  livenessInit();
  EnterCriticalSection(&YonaLivenessMutex);
  if (YonaCurrentTaskIsWorker) {
    if (YonaRunningWorkers > 0)
      YonaRunningWorkers--;
    YonaBlockedWorkers++;
    YonaChannelWaitKind = 1;
    maybeSpawnCompensationWorkerUnlocked();
  } else {
    livenessRegisterExternalTaskUnlocked();
    if (YonaActiveExternalTasks > 0)
      YonaActiveExternalTasks--;
    YonaExternalWaiters++;
    YonaChannelWaitKind = 2;
    maybeSpawnCompensationWorkerUnlocked();
  }
  /* See async_posix.c: opposite_waiters is this channel only. */
  int OtherBlocked = YonaCurrentTaskIsWorker ? (YonaBlockedWorkers > 1)
                                             : (YonaBlockedWorkers > 0);
  int DeadlockCandidate =
      (YonaRunningWorkers == 0 && YonaActiveExternalTasks == 0 &&
       YonaQueuedTasks == 0 && OppositeWaiters <= 0 && !OtherBlocked);
  /* A condition-variable signal can make A waiter runnable before it has
   * returned from timedwait and restored its liveness state. Confirm the
   * quiescent state across one wait cycle before raising :Deadlock. */
  int Deadlocked = DeadlockCandidate && YonaDeadlockCandidateSeen;
  YonaDeadlockCandidateSeen = DeadlockCandidate ? 1 : 0;
  LeaveCriticalSection(&YonaLivenessMutex);
  return Deadlocked;
}

void YonaRuntimeChannelWaitEnd(void) {
  livenessInit();
  EnterCriticalSection(&YonaLivenessMutex);
  if (YonaChannelWaitKind == 1) {
    if (YonaBlockedWorkers > 0)
      YonaBlockedWorkers--;
    YonaRunningWorkers++;
  } else if (YonaChannelWaitKind == 2) {
    if (YonaExternalWaiters > 0)
      YonaExternalWaiters--;
    YonaActiveExternalTasks++;
  }
  YonaChannelWaitKind = 0;
  LeaveCriticalSection(&YonaLivenessMutex);
}

static void fulfillPromise(YonaWorkItem *Task, int64_t Result, int IsError) {
  YonaRuntimeTaskComplete(Task->Promise, Result, IsError, Task->Group);
}

static void destroyTask(YonaWorkItem *Task) {
  free(Task->OwnedContext);
  free(Task);
}

static DWORD WINAPI yonaPoolWorkerWin32(void *Unused) {
  (void)Unused;
  for (;;) {
    EnterCriticalSection(&YonaPoolMutex);
    while (!YonaTaskHead)
      SleepConditionVariableCS(&YonaPoolCond, &YonaPoolMutex, INFINITE);
    YonaWorkItem *Task = YonaTaskHead;
    YonaTaskHead = Task->Next;
    if (!YonaTaskHead)
      YonaTaskTail = NULL;
    LeaveCriticalSection(&YonaPoolMutex);

    livenessWorkerBegin();

    if (Task->Group &&
        __atomic_load_n(&Task->Group->Cancelled, __ATOMIC_SEQ_CST)) {
      fulfillPromise(Task, 0, 1);
      livenessWorkerEnd();
      destroyTask(Task);
      continue;
    }

    /* This pairs with YonaRuntimeRaise's SJLJ longjmp; see exceptions.c. The
     * shared macro uses the target-compatible AArch64 implementation when
     * Clang lacks its normal SJLJ builtin (native Windows ARM64). */
    void *Jmp = YonaRuntimeTryBegin();
    if (YONA_SJLJ_SETJMP(Jmp) == 0) {
      int64_t Result = Task->Fn(Task->Arg);
      YonaRuntimeTryEnd();
      fulfillPromise(Task, Result, 0);
    } else {
      if (Task->Group) {
        EnterCriticalSection(&Task->Group->Mutex);
        if (!Task->Group->HasError) {
          Task->Group->FirstErrorSymbol = YonaRuntimeGetExceptionSymbol();
          Task->Group->FirstErrorMsg = YonaRuntimeGetExceptionMessage();
          Task->Group->HasError = 1;
        }
        LeaveCriticalSection(&Task->Group->Mutex);
        YonaRuntimeTaskGroupCancel(Task->Group);
      }
      fulfillPromise(Task, 0, 1);
    }
    livenessWorkerEnd();
    destroyTask(Task);
  }
}

static BOOL CALLBACK yonaPoolInitOnceCb(PINIT_ONCE InitOnce, PVOID Parameter,
                                        PVOID *Context) {
  (void)InitOnce;
  (void)Parameter;
  (void)Context;
  InitializeCriticalSection(&YonaPoolMutex);
  InitializeConditionVariable(&YonaPoolCond);
  livenessInit();
  EnterCriticalSection(&YonaLivenessMutex);
  for (int I = 0; I < YONA_POOL_SIZE; I++) {
    startPoolWorkerUnlocked();
  }
  LeaveCriticalSection(&YonaLivenessMutex);
  return TRUE;
}

static void yonaPoolInit(void) {
  InitOnceExecuteOnce(&YonaPoolInitOnce, yonaPoolInitOnceCb, NULL, NULL);
}

static YonaTask *makePromise(const YonaTypeDescriptor *ResultType) {
  if (ResultType == NULL)
    return NULL;
  YonaTask *P = (YonaTask *)malloc(sizeof(YonaTask));
  if (P == NULL)
    return NULL;
  P->Result = 0;
  P->ResultType = *ResultType;
  P->ResultOwned = 0;
  P->Completed = 0;
  P->Error = 0;
  InitializeCriticalSection(&P->Mutex);
  InitializeConditionVariable(&P->Cond);
  return P;
}

YonaTaskRef YonaRuntimeTaskCreate(const YonaTypeDescriptor *ResultType) {
  return makePromise(ResultType);
}

void YonaRuntimeTaskComplete(YonaTaskRef Task, int64_t Result, int IsError,
                             YonaTaskGroupRef Group) {
  if (!Task)
    return;
  EnterCriticalSection(&Task->Mutex);
  if (Task->Completed) {
    LeaveCriticalSection(&Task->Mutex);
    YonaRuntimeTypeDescriptorRelease(&Task->ResultType, Result);
    return;
  }
  Task->Result = Result;
  Task->ResultOwned = 1;
  Task->Error = IsError ? 1 : 0;
  Task->Completed = 1;
  WakeConditionVariable(&Task->Cond);
  LeaveCriticalSection(&Task->Mutex);

  if (Group) {
    if (__atomic_fetch_sub(&Group->PendingCount, 1, __ATOMIC_SEQ_CST) == 1)
      WakeConditionVariable(&Group->DoneCond);
  }
}

static void enqueueTask(YonaWorkItem *Task) {
  yonaPoolInit();
  EnterCriticalSection(&YonaPoolMutex);
  if (YonaTaskTail)
    YonaTaskTail->Next = Task;
  else
    YonaTaskHead = Task;
  YonaTaskTail = Task;
  WakeConditionVariable(&YonaPoolCond);
  LeaveCriticalSection(&YonaPoolMutex);
}

static YonaTask *submitTask(YonaAsyncFunction Fn, int64_t Arg,
                            void *OwnedContext,
                            const YonaTypeDescriptor *ResultType,
                            YonaTaskGroup *Group) {
  if (Fn == NULL || ResultType == NULL) {
    free(OwnedContext);
    return NULL;
  }
  yonaPoolInit();
  YonaTask *Promise = makePromise(ResultType);
  if (Promise == NULL) {
    free(OwnedContext);
    return NULL;
  }
  YonaWorkItem *Task = (YonaWorkItem *)calloc(1, sizeof(YonaWorkItem));
  if (Task == NULL) {
    DeleteCriticalSection(&Promise->Mutex);
    free(Promise);
    free(OwnedContext);
    return NULL;
  }
  Task->Fn = Fn;
  Task->Arg = Arg;
  Task->OwnedContext = OwnedContext;
  Task->Promise = Promise;
  Task->Group = Group;
  if (Group && !YonaRuntimeTaskGroupRegister(Group, Promise)) {
    DeleteCriticalSection(&Promise->Mutex);
    free(Promise);
    free(Task->OwnedContext);
    free(Task);
    return NULL;
  }
  if (!YonaCurrentTaskIsWorker) {
    livenessInit();
    EnterCriticalSection(&YonaLivenessMutex);
    livenessRegisterExternalTaskUnlocked();
    LeaveCriticalSection(&YonaLivenessMutex);
  }
  livenessTaskQueued();
  enqueueTask(Task);
  return Promise;
}

YonaTaskRef YonaRuntimeAsyncCall(YonaAsyncFunction Function, int64_t Argument,
                                 const YonaTypeDescriptor *ResultType) {
  return submitTask(Function, Argument, NULL, ResultType, NULL);
}

void *YonaRuntimeAsyncContextAllocate(int64_t Size) {
  if (Size <= 0 || (uint64_t)Size > SIZE_MAX)
    return NULL;
  return calloc(1, (size_t)Size);
}

YonaTaskRef YonaRuntimeAsyncCallContext(YonaAsyncFunction Function,
                                        void *Context,
                                        const YonaTypeDescriptor *ResultType) {
  return submitTask(Function, (int64_t)(intptr_t)Context, Context, ResultType,
                    NULL);
}

YonaTaskRef
YonaRuntimeAsyncCallContextGrouped(YonaAsyncFunction Function, void *Context,
                                   const YonaTypeDescriptor *ResultType,
                                   YonaTaskGroupRef Group) {
  return submitTask(Function, (int64_t)(intptr_t)Context, Context, ResultType,
                    Group);
}

static int64_t spawnClosureDispatch(int64_t ClosureInt) {
  int64_t *Closure = (int64_t *)(intptr_t)ClosureInt;
  typedef int64_t (*ThunkFnT)(int64_t *);
  ThunkFnT Fn = (ThunkFnT)(intptr_t)Closure[0];
  return Fn(Closure);
}

YonaTaskRef YonaRuntimeAsyncSpawnClosure(int64_t *Closure,
                                         const YonaTypeDescriptor *ResultType,
                                         YonaTaskGroupRef Group) {
  return submitTask((YonaAsyncFunction)spawnClosureDispatch,
                    (int64_t)(intptr_t)Closure, NULL, ResultType, Group);
}

YonaTaskRef YonaRuntimeAsyncCallGrouped(YonaAsyncFunction Function,
                                        int64_t Argument,
                                        const YonaTypeDescriptor *ResultType,
                                        YonaTaskGroupRef Group) {
  return submitTask(Function, Argument, NULL, ResultType, Group);
}

int64_t YonaRuntimeTaskAwaitKeep(YonaTaskRef Task) {
  if (Task == NULL)
    return 0;
  EnterCriticalSection(&Task->Mutex);
  while (!Task->Completed)
    SleepConditionVariableCS(&Task->Cond, &Task->Mutex, INFINITE);
  int64_t Result = Task->Result;
  YonaRuntimeTypeDescriptorRetain(&Task->ResultType, Result);
  LeaveCriticalSection(&Task->Mutex);
  return Result;
}

int64_t YonaRuntimeTaskAwait(YonaTaskRef Task) {
  if (Task == NULL)
    return 0;
  EnterCriticalSection(&Task->Mutex);
  while (!Task->Completed)
    SleepConditionVariableCS(&Task->Cond, &Task->Mutex, INFINITE);
  int64_t Result = Task->Result;
  Task->ResultOwned = 0;
  LeaveCriticalSection(&Task->Mutex);
  DeleteCriticalSection(&Task->Mutex);
  free(Task);
  return Result;
}

void YonaRuntimeTaskGroupCancel(YonaTaskGroupRef G) {
  if (!G)
    return;
  __atomic_store_n(&G->Cancelled, 1, __ATOMIC_SEQ_CST);
}

int64_t YonaRuntimeTaskGroupAwaitAll(YonaTaskGroupRef G) {
  if (!G)
    return 0;
  for (int I = 0; I < G->ChildCount; I++) {
    YonaTask *P = G->Children[I];
    EnterCriticalSection(&P->Mutex);
    while (!P->Completed)
      SleepConditionVariableCS(&P->Cond, &P->Mutex, INFINITE);
    LeaveCriticalSection(&P->Mutex);
  }
  if (G->HasError) {
    int64_t Sym = G->FirstErrorSymbol;
    const char *Msg = G->FirstErrorMsg;
    YonaRuntimeRaise(Sym, Msg);
  }
  return 0;
}

/* Test helper: `extern native` returns an already-Completed Promise (X * 7). */
YonaTaskRef
YonaTestNativePromiseImmediate(int64_t Value,
                               const YonaTypeDescriptor *ResultType) {
  YonaTask *P = YonaRuntimeTaskCreate(ResultType);
  if (!P)
    return NULL;
  YonaRuntimeTaskComplete(P, Value * 7, 0, NULL);
  return P;
}

int64_t YonaTestSlowIdentity(int64_t Ms) {
  if (Ms > 0) {
    DWORD D = (Ms > 0x7FFFFFFF) ? 0x7FFFFFFF : (DWORD)Ms;
    Sleep(D);
  }
  return Ms;
}

int64_t YonaTestSlowAdd(int64_t A, int64_t B) {
  Sleep(10);
  return A + B;
}
