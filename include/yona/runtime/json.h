/*
 * Std\Json C ABI — recursive Json ADT parse/stringify.
 *
 * Layout (RC_TYPE_ADT): [tag, num_fields, heap_mask, fields…]
 *   JsonNull=0, JsonBool=1, JsonInt=2, JsonFloat=3,
 *   JsonString=4 (heap), JsonArray=5 (heap seq), JsonObject=6
 *   (heap seq of 2-tuples (String, Json)).
 *
 * `yona_Std_Json__parse` returns Prelude Result (Ok Json | Err String).
 * Depth is capped at 64; input size at 16 MiB.
 */

#ifndef YONA_RUNTIME_JSON_H
#define YONA_RUNTIME_JSON_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t yona_Std_Json__parse(const char* s);
const char* yona_Std_Json__stringify(int64_t json);
const char* yona_Std_Json__stringifyString(const char* s);
const char* yona_Std_Json__stringifyBool(int64_t b);
const char* yona_Std_Json__stringifyFloat(double f);
const char* yona_Std_Json__null(void);
int64_t yona_Std_Json__parseInt(const char* s);
double yona_Std_Json__parseFloat(const char* s);

#ifdef __cplusplus
}
#endif

#endif /* YONA_RUNTIME_JSON_H */
