#ifndef YONA_RUNTIME_CONCURRENCY_CHANNEL_H
#define YONA_RUNTIME_CONCURRENCY_CHANNEL_H

#include "yona/Runtime/Core/Value.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque bounded multi-producer, multi-consumer channel reference.
///
/// Sending transfers one payload reference to the channel. Receiving
/// transfers that reference to the returned Option. Releasing a non-empty
/// channel releases every buffered payload through its type descriptor. All
/// operations except destruction are thread-safe. Destruction requires that
/// no concurrent operation is in flight.
typedef struct YonaChannel YonaChannel;
typedef YonaChannel *YonaChannelRef;

/// Create a channel by copying the mandatory PayloadType descriptor. Capacity
/// values below one are clamped to one. Returns null for a null descriptor,
/// an unrepresentable capacity, or allocation failure. The returned reference
/// is caller-owned.
YonaChannelRef YonaRuntimeChannelCreate(int64_t Capacity,
                                        const YonaTypeDescriptor *PayloadType);

/// Send consumes Value on both success and failure. It raises a runtime
/// exception if the channel is closed, the owning group is cancelled, or the
/// wait would deadlock. Concurrent sends and receives are safe.
void YonaRuntimeChannelSend(YonaChannelRef Channel, int64_t Value);

/// Receive transfers a buffered payload into an owning Option result. Closed
/// and drained channels return None. Zero reports allocation failure. Both
/// receive operations are thread-safe.
int64_t YonaRuntimeChannelReceive(YonaChannelRef Channel);
int64_t YonaRuntimeChannelTryReceive(YonaChannelRef Channel);

/// Close is idempotent and thread-safe. Query operations are thread-safe and
/// borrow the channel reference.
void YonaRuntimeChannelClose(YonaChannelRef Channel);
int64_t YonaRuntimeChannelIsClosed(YonaChannelRef Channel);
int64_t YonaRuntimeChannelLength(YonaChannelRef Channel);
int64_t YonaRuntimeChannelCapacity(YonaChannelRef Channel);
/// Destroy consumes Channel, releases buffered payloads, and is not safe to
/// race with any other operation. A null handle is a no-op.
void YonaRuntimeChannelDestroy(YonaChannelRef Channel);

/// Compiler entry point with a hidden mandatory descriptor. Returns an owning
/// endpoint tuple or null on the same failures as channel creation/allocation.
void *YonaStdChannelChannel(int64_t Capacity,
                            const YonaTypeDescriptor *PayloadType);
void *YonaStdGpuGpuFloatChannel(int64_t Capacity);

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_CONCURRENCY_CHANNEL_H */
