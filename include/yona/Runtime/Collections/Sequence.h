#ifndef YONA_RUNTIME_COLLECTIONS_SEQUENCE_H
#define YONA_RUNTIME_COLLECTIONS_SEQUENCE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t *YonaRuntimeSequenceAllocate(int64_t Count);
int64_t YonaRuntimeSequenceLength(int64_t *Sequence);
int64_t YonaRuntimeSequenceGet(int64_t *Sequence, int64_t Index);
int64_t YonaRuntimeSequenceGetOwned(int64_t *Sequence, int64_t Index);
void YonaRuntimeSequenceSet(int64_t *Sequence, int64_t Index, int64_t Value);
void YonaRuntimeSequenceSetHeap(int64_t *Sequence, int64_t IsHeap);
void YonaRuntimePrintSequence(int64_t *Sequence);
int64_t *YonaRuntimeSequencePrepend(int64_t Value, int64_t *Sequence);
int64_t *YonaRuntimeSequenceAppend(int64_t *Sequence, int64_t Value);
int64_t YonaRuntimeSequenceHead(int64_t *Sequence);
int64_t *YonaRuntimeSequenceTail(int64_t *Sequence);

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_COLLECTIONS_SEQUENCE_H */
