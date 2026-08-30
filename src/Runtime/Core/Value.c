#include "yona/Runtime/Core/Value.h"

#include "yona/Runtime/Core/Api.h"

#include <stdint.h>

static void retainReference(int64_t Value) {
  YonaRuntimeRetain((void *)(intptr_t)Value);
}

static void releaseReference(int64_t Value) {
  YonaRuntimeRelease((void *)(intptr_t)Value);
}

const YonaTypeDescriptor YonaRuntimeUnmanagedTypeDescriptor = {NULL, NULL, 0};
const YonaTypeDescriptor YonaRuntimeReferenceTypeDescriptor = {
    retainReference, releaseReference, 1};

void YonaRuntimeTypeDescriptorRetain(const YonaTypeDescriptor *Descriptor,
                                     int64_t Value) {
  if (Descriptor != NULL && Descriptor->Retain != NULL && Value != 0)
    Descriptor->Retain(Value);
}

void YonaRuntimeTypeDescriptorRelease(const YonaTypeDescriptor *Descriptor,
                                      int64_t Value) {
  if (Descriptor != NULL && Descriptor->Release != NULL && Value != 0)
    Descriptor->Release(Value);
}
