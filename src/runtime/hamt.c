/*
 * HAMT — Hash Array Mapped Trie
 *
 * Persistent (immutable) hash map with structural sharing.
 * Used for Yona's Dict and Set types.
 *
 * O(1) amortized lookup/insert/delete (max 7 levels for 32-bit hash).
 * 32-way branching at each level, bitmap-compressed to avoid sparse arrays.
 *
 * Node layout (RC-managed via RC_TYPE_DICT):
 *   [datamap: i64] [nodemap: i64] [size: i64] [entries...] [children...]
 *
 *   datamap: bitmap of which 32 slots contain inline key-value entries
 *   nodemap: bitmap of which 32 slots contain child sub-nodes
 *   size:    total entries in this subtree (for O(1) length)
 *
 *   Inline entries: [key0, val0, key1, val1, ...] — popcount(datamap) entries
 *   Child nodes:    [child0, child1, ...] — popcount(nodemap) children (ptrs)
 *
 * Reference: Bagwell (2001) "Ideal Hash Trees", Steindorfer & Vinju (2015) CHAMP
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>

extern void* rc_alloc(int64_t type_tag, size_t bytes);
extern void yona_rt_rc_inc(void* ptr);
extern void yona_rt_rc_dec(void* ptr);

#define RC_TYPE_DICT 3
#define HAMT_BITS 5
#define HAMT_WIDTH (1 << HAMT_BITS)  /* 32 */
#define HAMT_MASK  (HAMT_WIDTH - 1)  /* 0x1F */
#define HAMT_HDR   3  /* datamap, nodemap, size */

/* Extra bits in the RC type_tag word (bits 16+; unused for HAMT).
 * Bits 0-7 = type tag, 8-15 = pool class. */
#ifndef HAMT_FLAG_KEY_HEAP
#define HAMT_FLAG_KEY_HEAP (1LL << 16)
#define HAMT_FLAG_VAL_HEAP (1LL << 17)
#define HAMT_FLAG_IS_SET   (1LL << 18)
#endif

/* ===== Hash function (splitmix64) ===== */

static uint64_t hamt_hash(int64_t key) {
    uint64_t h = (uint64_t)key;
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
    return h ^ (h >> 31);
}

/* ===== Popcount ===== */

static int popcnt(uint64_t x) {
    return __builtin_popcountll(x);
}

/* Index of a bit in the compressed array = popcount of bits below it */
static int hamt_index(uint64_t bitmap, uint64_t bit) {
    return popcnt(bitmap & (bit - 1));
}

/* ===== Node allocation ===== */

typedef struct {
    int64_t datamap;   /* bitmap: inline data entries */
    int64_t nodemap;   /* bitmap: child sub-nodes */
    int64_t size;      /* total entries in subtree */
    int64_t payload[]; /* [k0,v0,k1,v1,..., child0,child1,...] */
} hamt_node_t;

static int64_t hamt_aux_flags(hamt_node_t* n) {
    if (!n) return 0;
    return ((int64_t*)n)[-1] & ~0xFFFFLL;
}

static void hamt_or_aux_flags(hamt_node_t* n, int64_t flags) {
    if (!n) return;
    ((int64_t*)n)[-1] |= flags;
}

static void hamt_copy_aux_flags(hamt_node_t* dst, hamt_node_t* src) {
    if (!dst || !src) return;
    int64_t* d = ((int64_t*)dst) - 1;
    int64_t* s = ((int64_t*)src) - 1;
    *d = (*d & 0xFFFFLL) | (*s & ~0xFFFFLL);
}

/* Transient (unique-owner) check for in-place mutation. */
static int hamt_is_unique(hamt_node_t* n) {
    if (!n) return 0;
    int64_t* hdr = ((int64_t*)n) - 2;
    return __builtin_expect(__atomic_load_n(&hdr[0], __ATOMIC_ACQUIRE) == 1, 1);
}

static void hamt_retain_slot(int64_t flags, int64_t key, int64_t val) {
    if ((flags & HAMT_FLAG_KEY_HEAP) && key)
        yona_rt_rc_inc((void*)(intptr_t)key);
    if ((flags & HAMT_FLAG_VAL_HEAP) && val)
        yona_rt_rc_inc((void*)(intptr_t)val);
}

static void hamt_release_slot(int64_t flags, int64_t key, int64_t val) {
    if ((flags & HAMT_FLAG_KEY_HEAP) && key)
        yona_rt_rc_dec((void*)(intptr_t)key);
    if ((flags & HAMT_FLAG_VAL_HEAP) && val)
        yona_rt_rc_dec((void*)(intptr_t)val);
}

static hamt_node_t* hamt_alloc(int data_count, int node_count, int64_t size) {
    /* At each HAMT level data_count + node_count ≤ 32 by invariant
     * (branching factor); assert to catch caller bugs that would
     * otherwise silently wrap the byte-count multiplication. */
    if (data_count < 0 || node_count < 0 ||
        data_count > HAMT_WIDTH || node_count > HAMT_WIDTH) {
        fprintf(stderr, "hamt_alloc: invalid counts data=%d node=%d\n",
                data_count, node_count);
        abort();
    }
    size_t payload_bytes = (size_t)(data_count * 2 + node_count) * sizeof(int64_t);
    hamt_node_t* n = (hamt_node_t*)rc_alloc(RC_TYPE_DICT,
        sizeof(hamt_node_t) + payload_bytes);
    n->datamap = 0;
    n->nodemap = 0;
    n->size = size;
    return n;
}

static hamt_node_t* hamt_alloc_like(int data_count, int node_count, int64_t size,
                                    hamt_node_t* src) {
    hamt_node_t* n = hamt_alloc(data_count, node_count, size);
    if (src) hamt_copy_aux_flags(n, src);
    return n;
}

static int hamt_data_count(hamt_node_t* n) { return popcnt((uint64_t)n->datamap); }
static int hamt_node_count(hamt_node_t* n) { return popcnt((uint64_t)n->nodemap); }

/* Data entries start at payload[0], children after all data */
static int64_t hamt_data_key(hamt_node_t* n, int idx) { return n->payload[idx * 2]; }
static int64_t hamt_data_val(hamt_node_t* n, int idx) { return n->payload[idx * 2 + 1]; }
static hamt_node_t* hamt_child(hamt_node_t* n, int idx) {
    int dc = hamt_data_count(n);
    return (hamt_node_t*)(intptr_t)n->payload[dc * 2 + idx];
}

/* ===== Empty ===== */

hamt_node_t* yona_rt_hamt_empty(void) {
    return hamt_alloc(0, 0, 0);
}

/* ===== Lookup ===== */

int64_t yona_rt_hamt_get(hamt_node_t* node, int64_t key, int64_t default_val) {
    if (!node) return default_val;
    uint64_t hash = hamt_hash(key);
    int shift = 0;

    while (node) {
        uint64_t frag = (hash >> shift) & HAMT_MASK;
        uint64_t bit = (uint64_t)1 << frag;

        if ((uint64_t)node->datamap & bit) {
            /* Inline data entry */
            int idx = hamt_index((uint64_t)node->datamap, bit);
            if (hamt_data_key(node, idx) == key)
                return hamt_data_val(node, idx);
            return default_val; /* hash collision slot occupied by different key */
        }
        if ((uint64_t)node->nodemap & bit) {
            /* Child sub-node */
            int idx = hamt_index((uint64_t)node->nodemap, bit);
            node = hamt_child(node, idx);
            shift += HAMT_BITS;
            continue;
        }
        return default_val; /* slot empty */
    }
    return default_val;
}

int64_t yona_rt_hamt_contains(hamt_node_t* node, int64_t key) {
    /* Use a sentinel that can't be a real value */
    int64_t sentinel = (int64_t)0xDEADBEEFCAFEBABEULL;
    return yona_rt_hamt_get(node, key, sentinel) != sentinel ? 1 : 0;
}

/* ===== Insert (persistent) ===== */

static hamt_node_t* hamt_copy_with_data(hamt_node_t* old, int insert_idx,
                                          int64_t key, int64_t val) {
    int dc = hamt_data_count(old);
    int nc = hamt_node_count(old);
    int64_t flags = hamt_aux_flags(old);
    hamt_node_t* n = hamt_alloc_like(dc + 1, nc, old->size + 1, old);
    n->datamap = old->datamap;
    n->nodemap = old->nodemap;

    /* Copy data entries, inserting new one at insert_idx */
    for (int i = 0; i < insert_idx; i++) {
        n->payload[i * 2] = old->payload[i * 2];
        n->payload[i * 2 + 1] = old->payload[i * 2 + 1];
        hamt_retain_slot(flags, n->payload[i * 2], n->payload[i * 2 + 1]);
    }
    n->payload[insert_idx * 2] = key;
    n->payload[insert_idx * 2 + 1] = val;
    for (int i = insert_idx; i < dc; i++) {
        n->payload[(i + 1) * 2] = old->payload[i * 2];
        n->payload[(i + 1) * 2 + 1] = old->payload[i * 2 + 1];
        hamt_retain_slot(flags, n->payload[(i + 1) * 2], n->payload[(i + 1) * 2 + 1]);
    }

    /* Copy child pointers (rc_inc each) */
    for (int i = 0; i < nc; i++) {
        int64_t child_ptr = old->payload[dc * 2 + i];
        n->payload[(dc + 1) * 2 + i] = child_ptr;
        if (child_ptr) yona_rt_rc_inc((void*)(intptr_t)child_ptr);
    }
    return n;
}

static hamt_node_t* hamt_copy_replace_data(hamt_node_t* old, int data_idx,
                                             int64_t new_val) {
    int dc = hamt_data_count(old);
    int nc = hamt_node_count(old);
    int64_t flags = hamt_aux_flags(old);
    hamt_node_t* n = hamt_alloc_like(dc, nc, old->size, old);
    n->datamap = old->datamap;
    n->nodemap = old->nodemap;

    /* Copy all data, replacing value at data_idx */
    for (int i = 0; i < dc; i++) {
        n->payload[i * 2] = old->payload[i * 2];
        hamt_retain_slot(flags, n->payload[i * 2], 0);
        if (i == data_idx) {
            n->payload[i * 2 + 1] = new_val;
        } else {
            n->payload[i * 2 + 1] = old->payload[i * 2 + 1];
            hamt_retain_slot(flags, 0, n->payload[i * 2 + 1]);
        }
    }
    /* Copy children */
    for (int i = 0; i < nc; i++) {
        int64_t child_ptr = old->payload[dc * 2 + i];
        n->payload[dc * 2 + i] = child_ptr;
        if (child_ptr) yona_rt_rc_inc((void*)(intptr_t)child_ptr);
    }
    return n;
}

static hamt_node_t* hamt_copy_promote_to_node(hamt_node_t* old, int data_idx,
                                                hamt_node_t* child_node, uint64_t bit) {
    int dc = hamt_data_count(old);
    int nc = hamt_node_count(old);
    int child_idx = hamt_index((uint64_t)old->nodemap | bit, bit);
    int64_t flags = hamt_aux_flags(old);

    hamt_node_t* n = hamt_alloc_like(dc - 1, nc + 1, old->size + 1, old);
    n->datamap = old->datamap & ~(int64_t)bit;
    n->nodemap = old->nodemap | (int64_t)bit;

    /* Copy data entries, skipping data_idx */
    int di = 0;
    for (int i = 0; i < dc; i++) {
        if (i == data_idx) continue;
        n->payload[di * 2] = old->payload[i * 2];
        n->payload[di * 2 + 1] = old->payload[i * 2 + 1];
        hamt_retain_slot(flags, n->payload[di * 2], n->payload[di * 2 + 1]);
        di++;
    }

    /* Copy old children + insert new child at child_idx (rc_inc all) */
    int ci = 0;
    for (int i = 0; i < nc + 1; i++) {
        int64_t cp;
        if (i == child_idx) {
            cp = (int64_t)(intptr_t)child_node;
            /* child_node is freshly created, rc=1, no inc needed */
        } else {
            cp = old->payload[dc * 2 + ci];
            if (cp) yona_rt_rc_inc((void*)(intptr_t)cp);
            ci++;
        }
        n->payload[(dc - 1) * 2 + i] = cp;
    }
    return n;
}

static hamt_node_t* hamt_merge_two(int64_t key1, int64_t val1, uint64_t hash1,
                                    int64_t key2, int64_t val2, uint64_t hash2,
                                    int shift, int64_t flags) {
    if (shift >= 64) {
        /* Hash collision at max depth: store both in a data node */
        hamt_node_t* n = hamt_alloc(2, 0, 2);
        hamt_or_aux_flags(n, flags);
        uint64_t frag = hash1 & HAMT_MASK;
        uint64_t bit = (uint64_t)1 << frag;
        /* Use two different bits (wrap around) */
        uint64_t frag2 = (frag + 1) & HAMT_MASK;
        uint64_t bit2 = (uint64_t)1 << frag2;
        n->datamap = (int64_t)(bit | bit2);
        int idx1 = hamt_index(bit | bit2, bit);
        int idx2 = hamt_index(bit | bit2, bit2);
        n->payload[idx1 * 2] = key1;
        n->payload[idx1 * 2 + 1] = val1;
        n->payload[idx2 * 2] = key2;
        n->payload[idx2 * 2 + 1] = val2;
        hamt_retain_slot(flags, key1, val1);
        return n;
    }

    uint64_t frag1 = (hash1 >> shift) & HAMT_MASK;
    uint64_t frag2 = (hash2 >> shift) & HAMT_MASK;

    if (frag1 == frag2) {
        /* Same slot: recurse deeper */
        hamt_node_t* child = hamt_merge_two(key1, val1, hash1,
                                              key2, val2, hash2, shift + HAMT_BITS,
                                              flags);
        hamt_node_t* n = hamt_alloc(0, 1, 2);
        hamt_or_aux_flags(n, flags);
        uint64_t bit = (uint64_t)1 << frag1;
        n->nodemap = (int64_t)bit;
        n->payload[0] = (int64_t)(intptr_t)child;
        return n;
    }

    /* Different slots: both inline */
    uint64_t bit1 = (uint64_t)1 << frag1;
    uint64_t bit2 = (uint64_t)1 << frag2;
    hamt_node_t* n = hamt_alloc(2, 0, 2);
    hamt_or_aux_flags(n, flags);
    n->datamap = (int64_t)(bit1 | bit2);
    int idx1 = hamt_index(bit1 | bit2, bit1);
    int idx2 = hamt_index(bit1 | bit2, bit2);
    n->payload[idx1 * 2] = key1;
    n->payload[idx1 * 2 + 1] = val1;
    n->payload[idx2 * 2] = key2;
    n->payload[idx2 * 2 + 1] = val2;
    hamt_retain_slot(flags, key1, val1);
    return n;
}

/* Forward declaration */
static hamt_node_t* yona_rt_hamt_put_impl(hamt_node_t* node, int64_t key,
                                            int64_t val, uint64_t hash, int shift);

hamt_node_t* yona_rt_hamt_put(hamt_node_t* node, int64_t key, int64_t val) {
    if (!node) {
        hamt_node_t* n = hamt_alloc(1, 0, 1);
        uint64_t hash = hamt_hash(key);
        uint64_t frag = hash & HAMT_MASK;
        n->datamap = (int64_t)((uint64_t)1 << frag);
        n->payload[0] = key;
        n->payload[1] = val;
        return n;
    }

    uint64_t hash = hamt_hash(key);
    hamt_node_t* result = yona_rt_hamt_put_impl(node, key, val, hash, 0);
    if (result && result != node)
        hamt_copy_aux_flags(result, node);
    return result;
}

static hamt_node_t* yona_rt_hamt_put_impl(hamt_node_t* node, int64_t key,
                                            int64_t val, uint64_t hash, int shift) {
    uint64_t frag = (hash >> shift) & HAMT_MASK;
    uint64_t bit = (uint64_t)1 << frag;
    int unique = hamt_is_unique(node);

    if ((uint64_t)node->datamap & bit) {
        /* Slot has inline data */
        int idx = hamt_index((uint64_t)node->datamap, bit);
        int64_t existing_key = hamt_data_key(node, idx);

        if (existing_key == key) {
            /* Same key: replace value */
            if (unique) {
                int64_t flags = hamt_aux_flags(node);
                int64_t old_val = node->payload[idx * 2 + 1];
                node->payload[idx * 2 + 1] = val;
                if (old_val != val)
                    hamt_release_slot(flags, 0, old_val);
                return node;
            }
            return hamt_copy_replace_data(node, idx, val);
        }

        /* Different key in same slot: promote to sub-node */
        uint64_t existing_hash = hamt_hash(existing_key);
        int64_t existing_val = hamt_data_val(node, idx);
        hamt_node_t* child = hamt_merge_two(existing_key, existing_val, existing_hash,
                                              key, val, hash, shift + HAMT_BITS,
                                              hamt_aux_flags(node));
        /* Promote requires changing node size (data→child), can't mutate in place */
        return hamt_copy_promote_to_node(node, idx, child, bit);
    }

    if ((uint64_t)node->nodemap & bit) {
        /* Slot has child node: recurse */
        int idx = hamt_index((uint64_t)node->nodemap, bit);
        hamt_node_t* old_child = hamt_child(node, idx);
        int64_t old_child_size = old_child ? old_child->size : 0;
        hamt_node_t* new_child = yona_rt_hamt_put_impl(old_child, key, val, hash,
                                                          shift + HAMT_BITS);
        int64_t size_delta = (new_child ? new_child->size : 0) - old_child_size;

        if (unique) {
            /* Transient: swap child pointer in place, no allocation */
            int dc = hamt_data_count(node);
            if (new_child != old_child)
                yona_rt_rc_dec((void*)(intptr_t)old_child);
            node->payload[dc * 2 + idx] = (int64_t)(intptr_t)new_child;
            node->size += size_delta;
            return node;
        }

        /* Copy node, replacing child at idx */
        int dc = hamt_data_count(node);
        int nc = hamt_node_count(node);
        int64_t flags = hamt_aux_flags(node);
        hamt_node_t* n = hamt_alloc_like(dc, nc, node->size + size_delta, node);
        n->datamap = node->datamap;
        n->nodemap = node->nodemap;
        memcpy(n->payload, node->payload, (size_t)(dc * 2) * sizeof(int64_t));
        for (int i = 0; i < dc; i++)
            hamt_retain_slot(flags, n->payload[i * 2], n->payload[i * 2 + 1]);
        for (int i = 0; i < nc; i++) {
            int64_t cp;
            if (i == idx) {
                cp = (int64_t)(intptr_t)new_child;
            } else {
                cp = node->payload[dc * 2 + i];
                if (cp) yona_rt_rc_inc((void*)(intptr_t)cp);
            }
            n->payload[dc * 2 + i] = cp;
        }
        return n;
    }

    /* Slot empty: add inline data — requires growing payload, can't mutate */
    int idx = hamt_index((uint64_t)node->datamap | bit, bit);
    hamt_node_t* n = hamt_copy_with_data(node, idx, key, val);
    n->datamap |= (int64_t)bit;
    return n;
}

/* ===== Size ===== */

int64_t yona_rt_hamt_size(hamt_node_t* node) {
    return node ? node->size : 0;
}

/* ===== Iteration (for printing and generators) ===== */

typedef void (*hamt_iter_fn)(int64_t key, int64_t val, void* ctx);

static void hamt_iterate_impl(hamt_node_t* node, hamt_iter_fn fn, void* ctx) {
    if (!node) return;
    int dc = hamt_data_count(node);
    int nc = hamt_node_count(node);
    for (int i = 0; i < dc; i++)
        fn(hamt_data_key(node, i), hamt_data_val(node, i), ctx);
    for (int i = 0; i < nc; i++)
        hamt_iterate_impl(hamt_child(node, i), fn, ctx);
}

/* ===== Print ===== */

extern void yona_rt_print_heap_value(int64_t val);

typedef struct { int first; int64_t flags; int as_set; } print_ctx_t;

static void hamt_print_slot(int64_t v, int heap) {
    if (heap)
        yona_rt_print_heap_value(v);
    else
        printf("%" PRId64, v);
}

static void hamt_print_entry(int64_t key, int64_t val, void* ctx) {
    print_ctx_t* pc = (print_ctx_t*)ctx;
    if (!pc->first) printf(", ");
    hamt_print_slot(key, (pc->flags & HAMT_FLAG_KEY_HEAP) != 0);
    if (!pc->as_set) {
        printf(": ");
        hamt_print_slot(val, (pc->flags & HAMT_FLAG_VAL_HEAP) != 0);
    }
    pc->first = 0;
}

void yona_rt_hamt_print(hamt_node_t* node) {
    printf("{");
    if (node) {
        print_ctx_t ctx = {1, hamt_aux_flags(node), 0};
        hamt_iterate_impl(node, hamt_print_entry, &ctx);
    }
    printf("}");
}

void yona_rt_hamt_print_set(hamt_node_t* node) {
    printf("{");
    if (node) {
        print_ctx_t ctx = {1, hamt_aux_flags(node), 1};
        hamt_iterate_impl(node, hamt_print_entry, &ctx);
    }
    printf("}");
}

/* ===== Collect keys to seq (for iteration/generators) ===== */

typedef struct {
    int64_t* seq;
    int64_t idx;
    int keys_are_heap;
} collect_ctx_t;

static void hamt_collect_key(int64_t key, int64_t val, void* ctx) {
    (void)val;
    collect_ctx_t* cc = (collect_ctx_t*)ctx;
    if (cc->keys_are_heap && key)
        yona_rt_rc_inc((void*)(intptr_t)key);
    yona_rt_seq_set(cc->seq, cc->idx, key);
    cc->idx++;
}

int64_t* yona_rt_hamt_keys(hamt_node_t* node) {
    extern int64_t* yona_rt_seq_alloc(int64_t count);
    extern void yona_rt_seq_set(int64_t* seq, int64_t index, int64_t value);
    extern void yona_rt_seq_set_heap(int64_t* seq, int64_t flag);
    int64_t sz = yona_rt_hamt_size(node);
    int64_t* seq = yona_rt_seq_alloc(sz);
    const int keys_are_heap = node && (hamt_aux_flags(node) & HAMT_FLAG_KEY_HEAP);
    yona_rt_seq_set_heap(seq, keys_are_heap);
    if (sz > 0) {
        collect_ctx_t ctx = {seq, 0, keys_are_heap};
        hamt_iterate_impl(node, hamt_collect_key, &ctx);
    }
    return seq;
}

/* ===== RC destructor support ===== */
/* Called from yona_rt_rc_dec when a HAMT node's refcount hits 0.
 * rc_dec heap keys/values per aux flags, then child sub-nodes. */

void yona_rt_hamt_destroy_children(void* node_ptr) {
    hamt_node_t* node = (hamt_node_t*)node_ptr;
    if (!node) return;
    int64_t flags = hamt_aux_flags(node);
    int dc = hamt_data_count(node);
    int nc = hamt_node_count(node);
    for (int i = 0; i < dc; i++)
        hamt_release_slot(flags, hamt_data_key(node, i), hamt_data_val(node, i));
    for (int i = 0; i < nc; i++) {
        void* child = (void*)(intptr_t)node->payload[dc * 2 + i];
        if (child) yona_rt_rc_dec(child);
    }
}

void yona_rt_hamt_stamp_aux_flags(void* node_ptr, int64_t flags) {
    hamt_node_t* node = (hamt_node_t*)node_ptr;
    if (!node || !flags) return;
    hamt_or_aux_flags(node, flags);
    int nc = hamt_node_count(node);
    for (int i = 0; i < nc; i++)
        yona_rt_hamt_stamp_aux_flags(hamt_child(node, i), flags);
}
