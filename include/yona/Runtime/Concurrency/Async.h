#ifndef YONA_RUNTIME_CONCURRENCY_ASYNC_H
#define YONA_RUNTIME_CONCURRENCY_ASYNC_H

#include "yona/Runtime/Core/Value.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque reference to an asynchronous computation.
///
/// Every task copies a mandatory result descriptor at creation. Successful
/// and error completion transfer one result reference to the task.
/// `YonaRuntimeTaskAwait` consumes an ungrouped task and transfers that
/// reference to the caller. `YonaRuntimeTaskAwaitKeep` retains a new caller
/// reference while leaving the original owned by its task group. The group
/// releases the original during `YonaRuntimeTaskGroupEnd`. Null result values
/// never invoke descriptor operations. Completion and await are thread-safe;
/// consuming the same task from multiple threads is invalid.
typedef struct YonaTask YonaTask;
typedef YonaTask *YonaTaskRef;

/// Opaque structured-concurrency scope. The creating thread owns the handle
/// until `YonaRuntimeTaskGroupEnd`. Registration, cancellation, and status
/// queries are thread-safe. A null handle is accepted only by status queries.
typedef struct YonaTaskGroup YonaTaskGroup;
typedef YonaTaskGroup *YonaTaskGroupRef;

/// Entry point used by the runtime worker pool.
typedef int64_t (*YonaAsyncFunction)(int64_t Argument);

/// Create an uncompleted task by copying ResultType. Returns null if the
/// descriptor is null or allocation fails.
YonaTaskRef YonaRuntimeTaskCreate(const YonaTypeDescriptor *ResultType);

/// Transfer Result to Task and complete it. A repeated completion releases the
/// rejected Result through the task descriptor. Group must be the group that
/// owns Task, or null for an ungrouped task. Task must be non-null; otherwise
/// the producer retains Result. Thread-safe against await and one completion.
void YonaRuntimeTaskComplete(YonaTaskRef Task, int64_t Result, int IsError,
                             YonaTaskGroupRef Group);

/// Await and consume an ungrouped task, transferring its result to the caller.
/// A null task returns zero. Concurrent consuming awaits are invalid.
int64_t YonaRuntimeTaskAwait(YonaTaskRef Task);

/// Await a group-owned task and retain a caller-owned result reference. A null
/// task returns zero. This is thread-safe against completion, but group end
/// must not race with an await.
int64_t YonaRuntimeTaskAwaitKeep(YonaTaskRef Task);

/// Submit work to the shared pool. Function transfers its returned value to
/// the task. ResultType is mandatory and copied. Returns null for a null
/// function/descriptor or allocation failure. Submission and completion are
/// thread-safe; an ungrouped result must be consumed with TaskAwait.
YonaTaskRef YonaRuntimeAsyncCall(YonaAsyncFunction Function, int64_t Argument,
                                 const YonaTypeDescriptor *ResultType);

/// Grouped submission has the same ownership contract. It returns null if
/// registration fails; the group owns a successful task.
YonaTaskRef YonaRuntimeAsyncCallGrouped(YonaAsyncFunction Function,
                                        int64_t Argument,
                                        const YonaTypeDescriptor *ResultType,
                                        YonaTaskGroupRef Group);
/// Allocate zero-initialized invocation context. Returns null for a nonpositive
/// size or allocation failure. A CallContext operation consumes Context on
/// success and on submission failure.
void *YonaRuntimeAsyncContextAllocate(int64_t Size);
YonaTaskRef YonaRuntimeAsyncCallContext(YonaAsyncFunction Function,
                                        void *Context,
                                        const YonaTypeDescriptor *ResultType);
YonaTaskRef
YonaRuntimeAsyncCallContextGrouped(YonaAsyncFunction Function, void *Context,
                                   const YonaTypeDescriptor *ResultType,
                                   YonaTaskGroupRef Group);
/// Submit a closure whose returned reference is transferred to the task. The
/// runtime copies ResultType and consumes Closure through its invocation ABI.
/// Failure and thread-safety match AsyncCall.
YonaTaskRef YonaRuntimeAsyncSpawnClosure(int64_t *Closure,
                                         const YonaTypeDescriptor *ResultType,
                                         YonaTaskGroupRef Group);

/// Native `Std\Task.spawn` entry point. Codegen supplies the hidden mandatory
/// descriptor for the closure result; callers own the returned ungrouped task.
/// Returns null on submission failure and is thread-safe.
YonaTaskRef YonaStdTaskSpawn(int64_t *Closure,
                             const YonaTypeDescriptor *ResultType);

/// Create a caller-owned structured-concurrency scope, or return null on
/// allocation failure. Register returns one on success and zero for invalid
/// handles or allocation failure; ownership stays with the caller on failure.
YonaTaskGroupRef YonaRuntimeTaskGroupBegin(void);
int YonaRuntimeTaskGroupRegister(YonaTaskGroupRef Group, YonaTaskRef Task);
int YonaRuntimeTaskGroupRegisterIo(YonaTaskGroupRef Group, uint64_t IoId);

/// Cancellation is thread-safe and prevents queued work from starting; work
/// already running completes normally. Null cancellation is a no-op and null
/// status queries report false.
void YonaRuntimeTaskGroupCancel(YonaTaskGroupRef Group);
int YonaRuntimeTaskGroupIsCancelled(YonaTaskGroupRef Group);
/// Wait for all registered tasks. Returns zero, including for null. A worker
/// exception is re-raised on the caller after all tasks finish.
int64_t YonaRuntimeTaskGroupAwaitAll(YonaTaskGroupRef Group);
/// Attach transfers ownership of Arena to Group. Detach destroys the attached
/// arena and is idempotent. Both accept null, cannot fail, and must be
/// serialized with registration, await, and group teardown.
void YonaRuntimeTaskGroupAttachArena(YonaTaskGroupRef Group, void *Arena);
void YonaRuntimeTaskGroupDetachArena(YonaTaskGroupRef Group);
/// Wait for and consume Group, releasing every task-owned result. A null group
/// is a no-op. End must not race with registration or result observation.
void YonaRuntimeTaskGroupEnd(YonaTaskGroupRef Group);
/// Bind/unbind the current thread's borrowed group context. These operations
/// allocate nothing and cannot fail; pushes and pops must be balanced on the
/// same thread, and Group must outlive the binding.
void YonaRuntimeTaskGroupArenaBindPush(YonaTaskGroupRef Group);
void YonaRuntimeTaskGroupArenaBindPop(void);

/// Internal worker-liveness hooks used by channels. Begin borrows Channel and
/// returns one when blocking is safe, zero when it would deadlock. The hooks
/// allocate nothing; a successful Begin must be paired with End on the same
/// thread. Concurrent calls for distinct waits are thread-safe.
int YonaRuntimeChannelWaitBegin(void *Channel, int Operation, int64_t Count,
                                int64_t Capacity, int Closed,
                                int OppositeWaiters);
void YonaRuntimeChannelWaitEnd(void);

/// Test-only native task that copies ResultType and owns Value before return.
/// Returns null for an invalid descriptor or allocation failure. The returned
/// ungrouped task is caller-owned and otherwise follows the await contract.
YonaTaskRef
YonaTestNativePromiseImmediate(int64_t Value,
                               const YonaTypeDescriptor *ResultType);

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_CONCURRENCY_ASYNC_H */
