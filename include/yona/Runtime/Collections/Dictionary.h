#ifndef YONA_RUNTIME_COLLECTIONS_DICTIONARY_H
#define YONA_RUNTIME_COLLECTIONS_DICTIONARY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Allocate an empty persistent dictionary. CapacityHint is accepted for
/// callers that already know an expected size; the HAMT grows on demand.
/// The caller owns the returned reference.
int64_t *YonaRuntimeDictionaryAllocate(int64_t CapacityHint);

/// Mark whether dictionary keys and values are reference-counted heap values.
/// The dictionary must be uniquely owned while its descriptor flags change.
void YonaRuntimeDictionarySetHeap(int64_t *Dictionary, int64_t KeysAreHeap,
                                  int64_t ValuesAreHeap);

/// Insert or replace an entry, consuming Dictionary and returning the updated
/// persistent dictionary. Key and Value ownership follows the heap flags.
int64_t *YonaRuntimeDictionaryPut(int64_t *Dictionary, int64_t Key,
                                  int64_t Value);

/// Read-only dictionary operations. Get returns DefaultValue when absent.
int64_t YonaRuntimeDictionaryGet(int64_t *Dictionary, int64_t Key,
                                 int64_t DefaultValue);
int64_t YonaRuntimeDictionarySize(int64_t *Dictionary);
int64_t YonaRuntimeDictionaryContains(int64_t *Dictionary, int64_t Key);

/// Return an owned sequence containing the dictionary keys.
int64_t *YonaRuntimeDictionaryKeys(int64_t *Dictionary);
void YonaRuntimePrintDictionary(int64_t *Dictionary);

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_COLLECTIONS_DICTIONARY_H */
