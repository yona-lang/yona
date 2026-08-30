#ifndef YONA_RUNTIME_CORE_VALUE_H
#define YONA_RUNTIME_CORE_VALUE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*YonaValueRetainFunction)(int64_t Value);
typedef void (*YonaValueReleaseFunction)(int64_t Value);

/// Ownership operations for a value erased at a C runtime boundary.
///
/// Descriptors are copied into the runtime object that owns the value, so the
/// descriptor itself may be stack allocated. Retain and Release must be
/// thread-safe and remain callable for the lifetime of every object holding a
/// copy. A null operation means that ownership action is unnecessary.
typedef struct YonaTypeDescriptor {
  YonaValueRetainFunction Retain;
  YonaValueReleaseFunction Release;
  int PayloadIsHeap;
} YonaTypeDescriptor;

extern const YonaTypeDescriptor YonaRuntimeUnmanagedTypeDescriptor;
extern const YonaTypeDescriptor YonaRuntimeReferenceTypeDescriptor;

/// Apply a descriptor ownership operation to a non-null erased value. These
/// helpers are thread-safe when the descriptor operations are thread-safe.
/// A null descriptor, null operation, or zero Value is a successful no-op;
/// neither function allocates or reports failure.
void YonaRuntimeTypeDescriptorRetain(const YonaTypeDescriptor *Descriptor,
                                     int64_t Value);
void YonaRuntimeTypeDescriptorRelease(const YonaTypeDescriptor *Descriptor,
                                      int64_t Value);

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_CORE_VALUE_H */
