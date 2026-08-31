/* ===== Async Runtime =====
 *
 * Fixed-Size Thread pool with work queue. Async functions submit tasks
 * to the pool and return A Promise handle immediately (non-blocking).
 * Promises are awaited lazily at use sites via YonaRuntimeTaskAwait.
 *
 * Structured concurrency: Task groups track child promises. If one child
 * fails, siblings are Cancelled (Thread pool: skip execution; io_uring:
 * IORING_OP_ASYNC_CANCEL). Error propagated to parent via group_await_all.
 */

#include "yona/Runtime/Concurrency/Async.h"
#include "yona/Runtime/Platform/SjLj.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void YonaRuntimeArenaDestroy(void *ArenaPtr);

#define YONA_POOL_SIZE 8
#define YONA_POOL_MAX_THREADS 32
#define YONA_GROUP_INITIAL_CAP 8

/* Forward declarations for exception handling (exceptions.c) */
void *YonaRuntimeTryBegin(void);
void YonaRuntimeTryEnd(void);
void YonaRuntimeRaise(int64_t Symbol, const char *Message);
void YonaRuntimeRaiseOwned(int64_t Symbol, const char *Message, void *Owner);
int64_t YonaRuntimeGetExceptionSymbol(void);
const char *YonaRuntimeGetExceptionMessage(void);
void *YonaRuntimeTakeExceptionOwner(void);
void YonaRuntimeRelease(void *Value);
void *YonaRuntimeAllocateStringWithLength(size_t Bytes, size_t StringLength);
void *YonaRuntimeAdtAllocate(int64_t Tag, int64_t FieldCount);
void YonaRuntimeAdtSetField(void *Value, int64_t Index, int64_t Field);
void YonaRuntimeAdtSetHeapMask(void *Value, int64_t Mask);

struct YonaTask {
  int64_t Result;
  YonaTypeDescriptor ResultType;
  int ResultOwned;
  int Completed; /* accessed via __atomic builtins */
  int Error;     /* 1 if Completed with Error */
  pthread_mutex_t Mutex;
  pthread_cond_t Cond;
};

/* ===== Task Groups (Structured Concurrency) ===== */

struct YonaTaskGroup {
  int Cancelled;    /* accessed via __atomic builtins */
  int PendingCount; /* accessed via __atomic builtins */
  /* Thread pool Children */
  YonaTask **Children;
  int ChildCount, ChildCap;
  /* io_uring Children */
  uint64_t *IoChildren;
  int IoChildCount, IoChildCap;
  /* Error from first failing child */
  int64_t FirstErrorSymbol;
  const char *FirstErrorMsg;
  void *FirstErrorOwner;
  int HasError;
  /* Bump Arena for structured-concurrency scope (parent Thread only) */
  void *Arena;
  /* Synchronization */
  pthread_mutex_t Mutex;
  pthread_cond_t DoneCond;
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
  pthread_mutex_init(&G->Mutex, NULL);
  pthread_cond_init(&G->DoneCond, NULL);
  return G;
}

int YonaRuntimeTaskGroupRegister(YonaTaskGroupRef Group, YonaTaskRef Task) {
  if (!Group || !Task)
    return 0;
  pthread_mutex_lock(&Group->Mutex);
  if (Group->ChildCount >= Group->ChildCap) {
    const int NewCapacity = Group->ChildCap * 2;
    YonaTask **Children =
        (YonaTask **)realloc(Group->Children, NewCapacity * sizeof(YonaTask *));
    if (Children == NULL) {
      pthread_mutex_unlock(&Group->Mutex);
      return 0;
    }
    Group->ChildCap = NewCapacity;
    Group->Children = Children;
  }
  Group->Children[Group->ChildCount++] = Task;
  __atomic_fetch_add(&Group->PendingCount, 1, __ATOMIC_SEQ_CST);
  pthread_mutex_unlock(&Group->Mutex);
  return 1;
}

int YonaRuntimeTaskGroupRegisterIo(YonaTaskGroupRef G, uint64_t IoId) {
  if (!G)
    return 0;
  pthread_mutex_lock(&G->Mutex);
  if (G->IoChildCount >= G->IoChildCap) {
    const int NewCapacity = G->IoChildCap * 2;
    uint64_t *Children =
        (uint64_t *)realloc(G->IoChildren, NewCapacity * sizeof(uint64_t));
    if (Children == NULL) {
      pthread_mutex_unlock(&G->Mutex);
      return 0;
    }
    G->IoChildCap = NewCapacity;
    G->IoChildren = Children;
  }
  G->IoChildren[G->IoChildCount++] = IoId;
  __atomic_fetch_add(&G->PendingCount, 1, __ATOMIC_SEQ_CST);
  pthread_mutex_unlock(&G->Mutex);
  return 1;
}

/* Cancel: set flag + submit IORING_OP_ASYNC_CANCEL for io Children */
void YonaRuntimeTaskGroupCancel(
    YonaTaskGroupRef G); /* forward decl — implemented after uring include */

int YonaRuntimeTaskGroupIsCancelled(YonaTaskGroupRef G) {
  if (!G)
    return 0;
  return __atomic_load_n(&G->Cancelled, __ATOMIC_SEQ_CST);
}

/* Await all Children, then re-raise first Error if any */
int64_t YonaRuntimeTaskGroupAwaitAll(
    YonaTaskGroupRef G); /* forward decl — needs async_await */

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
  pthread_mutex_lock(&P->Mutex);
  while (!P->Completed)
    pthread_cond_wait(&P->Cond, &P->Mutex);
  int64_t Result = P->Result;
  int ResultOwned = P->ResultOwned;
  P->ResultOwned = 0;
  pthread_mutex_unlock(&P->Mutex);
  if (ResultOwned)
    YonaRuntimeTypeDescriptorRelease(&P->ResultType, Result);
  pthread_mutex_destroy(&P->Mutex);
  pthread_cond_destroy(&P->Cond);
  free(P);
}

void YonaRuntimeTaskGroupEnd(YonaTaskGroupRef Group) {
  YonaTaskGroup *TaskGroup = Group;
  if (!TaskGroup)
    return;
  YonaRuntimeTaskGroupDetachArena(TaskGroup);
  for (int I = 0; I < TaskGroup->ChildCount; I++)
    destroyPromise(TaskGroup->Children[I]);
  YonaRuntimeRelease(TaskGroup->FirstErrorOwner);
  TaskGroup->FirstErrorOwner = NULL;
  pthread_mutex_destroy(&TaskGroup->Mutex);
  pthread_cond_destroy(&TaskGroup->DoneCond);
  free(TaskGroup->Children);
  free(TaskGroup->IoChildren);
  free(TaskGroup);
}

/* ===== Task Queue ===== */

typedef struct YonaWorkItem {
  YonaAsyncFunction Fn;
  int64_t Arg;
  void *OwnedContext;
  YonaTask *Promise;
  YonaTaskGroup *Group; /* owning Group (NULL if ungrouped) */
  struct YonaWorkItem *Next;
} YonaWorkItem;

/* Thread pool state */
static pthread_t YonaPoolThreads[YONA_POOL_SIZE];
static YonaWorkItem *YonaTaskHead = NULL;
static YonaWorkItem *YonaTaskTail = NULL;
static pthread_mutex_t YonaPoolMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t YonaPoolCond = PTHREAD_COND_INITIALIZER;
static _Atomic int YonaPoolInitialized = 0;

/* Channel liveness tracking.
 *
 * The fixed worker pool uses managed blocking: when A worker blocks on A
 * Channel while queued work exists, the runtime may add A compensation worker.
 * Deadlock is reported only when every known worker Task is blocked and no
 * queued Task can make progress. This replaces the old timeout heuristic, so
 * slow producers are allowed to be slow without being misreported as deadlocks.
 */
static pthread_mutex_t YonaLivenessMutex = PTHREAD_MUTEX_INITIALIZER;
static int64_t YonaNextTaskId = 1;
static int YonaWorkerThreads = 0;
static int YonaQueuedTasks = 0;
static int YonaRunningWorkers = 0;
static int YonaBlockedWorkers = 0;
static int YonaActiveExternalTasks = 0;
static int YonaExternalWaiters = 0;
static _Thread_local int64_t YonaCurrentTaskId = 0;
static _Thread_local int YonaCurrentTaskIsWorker = 0;
static _Thread_local int YonaExternalTaskRegistered = 0;
static _Thread_local int YonaChannelWaitKind = 0; /* 1 worker, 2 external */
static _Thread_local int YonaDeadlockCandidateSeen = 0;

static void *yonaPoolWorker(void *Unused);

static void startPoolWorkerUnlocked(void) {
  pthread_t Thread;
  if (pthread_create(&Thread, NULL, yonaPoolWorker, NULL) == 0) {
    pthread_detach(Thread);
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

static void livenessTaskQueued(void) {
  pthread_mutex_lock(&YonaLivenessMutex);
  YonaQueuedTasks++;
  pthread_mutex_unlock(&YonaLivenessMutex);
}

static void livenessRegisterExternalTaskUnlocked(void) {
  if (YonaCurrentTaskIsWorker || YonaExternalTaskRegistered)
    return;
  YonaExternalTaskRegistered = 1;
  YonaCurrentTaskId = YonaNextTaskId++;
  YonaActiveExternalTasks++;
}

static void livenessWorkerBegin(void) {
  pthread_mutex_lock(&YonaLivenessMutex);
  if (YonaQueuedTasks > 0)
    YonaQueuedTasks--;
  YonaRunningWorkers++;
  YonaCurrentTaskId = YonaNextTaskId++;
  YonaCurrentTaskIsWorker = 1;
  pthread_mutex_unlock(&YonaLivenessMutex);
}

static void livenessWorkerEnd(void) {
  pthread_mutex_lock(&YonaLivenessMutex);
  if (YonaChannelWaitKind == 1) {
    if (YonaBlockedWorkers > 0)
      YonaBlockedWorkers--;
    YonaChannelWaitKind = 0;
  } else if (YonaCurrentTaskIsWorker && YonaRunningWorkers > 0) {
    YonaRunningWorkers--;
  }
  YonaCurrentTaskId = 0;
  YonaCurrentTaskIsWorker = 0;
  pthread_mutex_unlock(&YonaLivenessMutex);
}

int YonaRuntimeChannelWaitBegin(void *Channel, int Operation, int64_t Count,
                                int64_t Capacity, int Closed,
                                int OppositeWaiters) {
  (void)Channel;
  (void)Operation;
  (void)Count;
  (void)Capacity;
  (void)Closed;
  pthread_mutex_lock(&YonaLivenessMutex);
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
  /* Blocked workers on *other* channels can still make progress (e.G. A
   * producer/consumer pair on A work Channel while main waits on done).
   * OppositeWaiters is only for this Channel, so treat other blocked
   * tasks as live progress. */
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
  pthread_mutex_unlock(&YonaLivenessMutex);
  return Deadlocked;
}

void YonaRuntimeChannelWaitEnd(void) {
  pthread_mutex_lock(&YonaLivenessMutex);
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
  pthread_mutex_unlock(&YonaLivenessMutex);
}

void YonaRuntimeTaskComplete(YonaTaskRef Task, int64_t Result, int IsError,
                             YonaTaskGroupRef Group) {
  if (!Task)
    return;
  pthread_mutex_lock(&Task->Mutex);
  if (Task->Completed) {
    pthread_mutex_unlock(&Task->Mutex);
    YonaRuntimeTypeDescriptorRelease(&Task->ResultType, Result);
    return;
  }
  Task->Result = Result;
  Task->ResultOwned = 1;
  Task->Error = IsError ? 1 : 0;
  Task->Completed = 1;
  pthread_cond_signal(&Task->Cond);
  pthread_mutex_unlock(&Task->Mutex);

  if (Group) {
    if (__atomic_fetch_sub(&Group->PendingCount, 1, __ATOMIC_SEQ_CST) == 1) {
      pthread_cond_signal(&Group->DoneCond);
    }
  }
}

static void fulfillPromise(YonaWorkItem *Task, int64_t Result, int IsError) {
  YonaRuntimeTaskComplete(Task->Promise, Result, IsError, Task->Group);
}

static void destroyTask(YonaWorkItem *Task) {
  free(Task->OwnedContext);
  free(Task);
}

static void *yonaPoolWorker(void *Unused) {
  (void)Unused;
  while (1) {
    pthread_mutex_lock(&YonaPoolMutex);
    while (!YonaTaskHead) {
      pthread_cond_wait(&YonaPoolCond, &YonaPoolMutex);
    }
    YonaWorkItem *Task = YonaTaskHead;
    YonaTaskHead = Task->Next;
    if (!YonaTaskHead)
      YonaTaskTail = NULL;
    pthread_mutex_unlock(&YonaPoolMutex);

    livenessWorkerBegin();

    /* Check cancellation before executing */
    if (Task->Group &&
        __atomic_load_n(&Task->Group->Cancelled, __ATOMIC_SEQ_CST)) {
      fulfillPromise(Task, 0, 1);
      livenessWorkerEnd();
      destroyTask(Task);
      continue;
    }

    /* Execute with Error capture via YONA_SJLJ_SETJMP (matches
     * YonaRuntimeRaise; see exceptions.c for the SJLJ buffer rationale). */
    void *Jmp = YonaRuntimeTryBegin();
    if (YONA_SJLJ_SETJMP(Jmp) == 0) {
      int64_t Result = Task->Fn(Task->Arg);
      YonaRuntimeTryEnd();
      fulfillPromise(Task, Result, 0);
    } else {
      /* Task raised an exception — capture in Group */
      void *Owner = YonaRuntimeTakeExceptionOwner();
      if (Task->Group) {
        pthread_mutex_lock(&Task->Group->Mutex);
        if (!Task->Group->HasError) {
          Task->Group->FirstErrorSymbol = YonaRuntimeGetExceptionSymbol();
          Task->Group->FirstErrorMsg = YonaRuntimeGetExceptionMessage();
          Task->Group->FirstErrorOwner = Owner;
          Owner = NULL;
          Task->Group->HasError = 1;
        }
        pthread_mutex_unlock(&Task->Group->Mutex);
        /* Cancel siblings */
        YonaRuntimeTaskGroupCancel(Task->Group);
      }
      YonaRuntimeRelease(Owner);
      fulfillPromise(Task, 0, 1);
    }

    livenessWorkerEnd();
    destroyTask(Task);
  }
  return NULL;
}

static void yonaPoolInit(void) {
  if (YonaPoolInitialized)
    return;
  YonaPoolInitialized = 1;
  pthread_mutex_lock(&YonaLivenessMutex);
  for (int I = 0; I < YONA_POOL_SIZE; I++) {
    (void)YonaPoolThreads[I];
    startPoolWorkerUnlocked();
  }
  pthread_mutex_unlock(&YonaLivenessMutex);
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
  pthread_mutex_init(&P->Mutex, NULL);
  pthread_cond_init(&P->Cond, NULL);
  return P;
}

YonaTaskRef YonaRuntimeTaskCreate(const YonaTypeDescriptor *ResultType) {
  return makePromise(ResultType);
}

static void enqueueTask(YonaWorkItem *Task) {
  pthread_mutex_lock(&YonaPoolMutex);
  if (YonaTaskTail) {
    YonaTaskTail->Next = Task;
  } else {
    YonaTaskHead = Task;
  }
  YonaTaskTail = Task;
  pthread_cond_signal(&YonaPoolCond);
  pthread_mutex_unlock(&YonaPoolMutex);
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
    pthread_mutex_destroy(&Promise->Mutex);
    pthread_cond_destroy(&Promise->Cond);
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
    pthread_mutex_destroy(&Promise->Mutex);
    pthread_cond_destroy(&Promise->Cond);
    free(Promise);
    free(Task->OwnedContext);
    free(Task);
    return NULL;
  }
  if (!YonaCurrentTaskIsWorker) {
    pthread_mutex_lock(&YonaLivenessMutex);
    livenessRegisterExternalTaskUnlocked();
    pthread_mutex_unlock(&YonaLivenessMutex);
  }
  livenessTaskQueued();
  enqueueTask(Task);
  return Promise;
}

/* Submit async work to Thread pool. Returns Promise immediately (non-blocking).
 */
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

/* Spawn A Yona Closure as A Task. The Closure layout is:
 *   [fn_ptr, ret_tag, arity, num_caps, heap_mask, borrow_mask, caps...]
 * The function pointer takes the Closure (env) as its first argument.
 * For zero-arity closures (thunks), we call Fn(Closure). */
static int64_t spawnClosureDispatch(int64_t ClosureInt) {
  int64_t *Closure = (int64_t *)(intptr_t)ClosureInt;
  /* Call the Closure as A thunk: Fn(Closure) */
  typedef int64_t (*ThunkFnT)(int64_t *);
  ThunkFnT Fn = (ThunkFnT)(intptr_t)Closure[0];
  return Fn(Closure);
}

YonaTaskRef YonaRuntimeAsyncSpawnClosure(int64_t *Closure,
                                         const YonaTypeDescriptor *ResultType,
                                         YonaTaskGroupRef Group) {
  /* Submit using the dispatch wrapper. Pass Closure as the int64 Arg. */
  return submitTask((YonaAsyncFunction)spawnClosureDispatch,
                    (int64_t)(intptr_t)Closure, NULL, ResultType, Group);
}

YonaTaskRef YonaRuntimeAsyncCallGrouped(YonaAsyncFunction Function,
                                        int64_t Argument,
                                        const YonaTypeDescriptor *ResultType,
                                        YonaTaskGroupRef Group) {
  return submitTask(Function, Argument, NULL, ResultType, Group);
}

/* Await without freeing — grouped let/comprehension; YonaRuntimeTaskGroupEnd
 * destroys. */
int64_t YonaRuntimeTaskAwaitKeep(YonaTaskRef Task) {
  if (Task == NULL)
    return 0;
  pthread_mutex_lock(&Task->Mutex);
  while (!Task->Completed)
    pthread_cond_wait(&Task->Cond, &Task->Mutex);
  int64_t Result = Task->Result;
  YonaRuntimeTypeDescriptorRetain(&Task->ResultType, Result);
  pthread_mutex_unlock(&Task->Mutex);
  return Result;
}

/* Standalone async: await, return Result, and free the Promise. */
int64_t YonaRuntimeTaskAwait(YonaTaskRef Task) {
  if (Task == NULL)
    return 0;
  pthread_mutex_lock(&Task->Mutex);
  while (!Task->Completed)
    pthread_cond_wait(&Task->Cond, &Task->Mutex);
  int64_t Result = Task->Result;
  Task->ResultOwned = 0;
  pthread_mutex_unlock(&Task->Mutex);
  pthread_mutex_destroy(&Task->Mutex);
  pthread_cond_destroy(&Task->Cond);
  free(Task);
  return Result;
}

/* ===== Task Group: Cancel & Await All ===== */

/* Cancel: set flag. io_uring cancellation is done externally by the codegen
 * or by calling YonaRuntimeIoUringCancel() for each io child (see
 * yona/runtime/uring.h). */
void YonaRuntimeTaskGroupCancel(YonaTaskGroupRef G) {
  if (!G)
    return;
  __atomic_store_n(&G->Cancelled, 1, __ATOMIC_SEQ_CST);
  /* I/O cancellation is coordinated by the platform I/O component. The Task

   * * Group records cancellation here; platform requests observe the flag. */
}

/* Await all Children in the Group, then re-raise first Error if any.
 * Promises stay allocated until YonaRuntimeTaskGroupEnd (after await_keep in
 * body). */
int64_t YonaRuntimeTaskGroupAwaitAll(YonaTaskGroupRef G) {
  if (!G)
    return 0;

  /* Wait for all Thread-pool Children to complete */
  for (int I = 0; I < G->ChildCount; I++) {
    YonaTask *P = G->Children[I];
    pthread_mutex_lock(&P->Mutex);
    while (!P->Completed)
      pthread_cond_wait(&P->Cond, &P->Mutex);
    pthread_mutex_unlock(&P->Mutex);
  }

  /* Re-raise first Error on the caller's Thread */
  if (G->HasError) {
    int64_t Sym = G->FirstErrorSymbol;
    const char *Msg = G->FirstErrorMsg;
    void *Owner = G->FirstErrorOwner;
    G->FirstErrorOwner = NULL;
    /* Don't end Group here — let the codegen do it after cleanup */
    YonaRuntimeRaiseOwned(Sym, Msg, Owner);
  }

  return 0;
}

/* Test helper: async function that sleeps for N milliseconds then returns N */
int64_t YonaTestSlowIdentity(int64_t Ms) {
  usleep((useconds_t)(Ms * 1000));
  return Ms;
}

/* Test helper: propagate a genuinely owned heap ADT through an async worker's
 * exception boundary and into the parent task group. */
int64_t YonaTestAsyncRaiseOwned(int64_t Ignored) {
  (void)Ignored;
  static const char Message[] = "boom";
  char *Payload =
      (char *)YonaRuntimeAllocateStringWithLength(sizeof(Message), 4);
  if (!Payload)
    YonaRuntimeRaise(0, "exception payload allocation failed");
  memcpy(Payload, Message, sizeof(Message));
  void *Owner = YonaRuntimeAdtAllocate(0, 1);
  if (!Owner) {
    YonaRuntimeRelease(Payload);
    YonaRuntimeRaise(0, "exception owner allocation failed");
  }
  YonaRuntimeAdtSetField(Owner, 0, (int64_t)(intptr_t)Payload);
  YonaRuntimeAdtSetHeapMask(Owner, 1);
  YonaRuntimeRaiseOwned(0, Payload, Owner);
  return 0;
}

/* Test helper: multi-Arg async function — adds two numbers with A delay */
int64_t YonaTestSlowAdd(int64_t A, int64_t B) {
  usleep(10000); /* 10ms */
  return A + B;
}

/* Returns A runtime Promise Completed before the caller can await — exercises
 * `extern native` (C ABI: YonaTask*). Result is `X * 7` (e.G. 6 -> 42). */
YonaTaskRef
YonaTestNativePromiseImmediate(int64_t Value,
                               const YonaTypeDescriptor *ResultType) {
  YonaTask *P = YonaRuntimeTaskCreate(ResultType);
  if (!P)
    return NULL;
  YonaRuntimeTaskComplete(P, Value * 7, 0, NULL);
  return P;
}
