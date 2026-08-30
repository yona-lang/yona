#ifndef YONA_SRC_RUNTIME_CORE_INTERNAL_H
#define YONA_SRC_RUNTIME_CORE_INTERNAL_H

#include "yona/Runtime/Core/Api.h"

#define YONA_RUNTIME_TYPE_ADT 4
#define YONA_RUNTIME_TYPE_CLOSURE 5
#define YONA_RUNTIME_TYPE_STRING 6
#define YONA_RUNTIME_TYPE_BYTE_ARRAY 8
#define YONA_RUNTIME_TYPE_INT_ARRAY 18
#define YONA_RUNTIME_TYPE_FLOAT_ARRAY 19
#define YONA_RUNTIME_RC_HEADER_SIZE 2

int64_t YonaRuntimeIntArrayLength(int64_t *Array);

const char *YonaRuntimeGpuVulkanStatusName(void);

int64_t *YonaRuntimeMakeNone(void);
int64_t *YonaRuntimeMakeSome(int64_t Value);
int64_t *YonaRuntimeMakeIterator(int64_t *Closure);

#endif /* YONA_SRC_RUNTIME_CORE_INTERNAL_H */
