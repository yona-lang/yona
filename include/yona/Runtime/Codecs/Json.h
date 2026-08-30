/*
 * Std\Json C ABI — recursive Json ADT parse/stringify.
 *
 * Layout (RC_TYPE_ADT): [tag, num_fields, heap_mask, fields…]
 *   JsonNull=0, JsonBool=1, JsonInt=2, JsonFloat=3,
 *   JsonString=4 (heap), JsonArray=5 (heap seq), JsonObject=6
 *   (heap seq of 2-tuples (String, Json)).
 *
 * `YonaStdJsonParse` returns Prelude Result (Ok Json | Err String).
 * Depth is capped at 64; input size at 16 MiB.
 */

#ifndef YONA_RUNTIME_CODECS_JSON_H
#define YONA_RUNTIME_CODECS_JSON_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t YonaStdJsonParse(const char *S);
const char *YonaStdJsonStringify(int64_t Json);
const char *YonaStdJsonStringifyString(const char *S);
const char *YonaStdJsonStringifyBool(int64_t B);
const char *YonaStdJsonStringifyFloat(double F);
const char *YonaStdJsonNull(void);
int64_t YonaStdJsonParseInt(const char *S);
double YonaStdJsonParseFloat(const char *S);

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_CODECS_JSON_H */
