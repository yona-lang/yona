#ifndef YONA_SRC_RUNTIME_COLLECTIONS_HAMTINTERNAL_H
#define YONA_SRC_RUNTIME_COLLECTIONS_HAMTINTERNAL_H

#include <stdint.h>

#ifndef YONA_HAMT_FLAG_KEY_HEAP
#define YONA_HAMT_FLAG_KEY_HEAP (1LL << 16)
#define YONA_HAMT_FLAG_VALUE_HEAP (1LL << 17)
#define YONA_HAMT_FLAG_IS_SET (1LL << 18)
#endif

typedef struct YonaHamtNode {
  int64_t Datamap;
  int64_t Nodemap;
  int64_t Size;
  int64_t Payload[];
} YonaHamtNode;

static inline int64_t yonaRuntimeHamtFlags(const YonaHamtNode *Node) {
  if (!Node)
    return 0;
  return ((const int64_t *)Node)[-1] & ~0xFFFFLL;
}

static inline void yonaRuntimeHamtAddFlags(YonaHamtNode *Node, int64_t Flags) {
  if (Node)
    ((int64_t *)Node)[-1] |= Flags;
}

YonaHamtNode *YonaRuntimeHamtCreate(void);
int64_t YonaRuntimeHamtGet(YonaHamtNode *Node, int64_t Key,
                           int64_t DefaultValue);
int64_t YonaRuntimeHamtContains(YonaHamtNode *Node, int64_t Key);
YonaHamtNode *YonaRuntimeHamtPut(YonaHamtNode *Node, int64_t Key,
                                 int64_t Value);
int64_t YonaRuntimeHamtSize(YonaHamtNode *Node);
int64_t *YonaRuntimeHamtKeys(YonaHamtNode *Node);
void YonaRuntimeHamtPrint(YonaHamtNode *Node);
void YonaRuntimeHamtPrintSet(YonaHamtNode *Node);
void YonaRuntimeHamtDestroyChildren(void *Node);
void YonaRuntimeHamtStampAuxiliaryFlags(void *Node, int64_t Flags);

#endif /* YONA_SRC_RUNTIME_COLLECTIONS_HAMTINTERNAL_H */
