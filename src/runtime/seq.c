/*
 * Seq — persistent immutable sequence.
 *
 * Two representations:
 *   Small (≤32 elements): flat array [length, heap_flag, elem0, ...]  (RC_TYPE_SEQ)
 *   Large (>32 elements):  RBT with head chain + optional trie + tail buffer
 *
 * The head chain is a linked list of 32-element buffers for O(1) cons/head/tail.
 * The trie is a 32-way radix-balanced trie for O(log32 n) indexed access on
 * snoc-built elements. The tail buffer absorbs snoc for O(1) amortized append.
 *
 * Time complexities:
 *   cons (prepend):  O(1) amortized
 *   head:            O(1)
 *   tail:            O(1) amortized
 *   snoc (append):   O(1) amortized
 *   get(i):          O(1) small / O(n/32) head chain / O(log32 n) trie
 *   length:          O(1)
 *   concat:          O(n)
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>

extern void* rc_alloc(int64_t type_tag, size_t bytes);
extern void yona_rt_rc_inc(void* ptr);
extern void yona_rt_rc_dec(void* ptr);

/* Branch prediction hints — most seq operations take the fast path. */
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

#define RC_TYPE_SEQ       1
#define RC_TYPE_RBT       12
#define RC_TYPE_RBT_NODE  13
#define RC_TYPE_RBT_LEAF  14
#define RC_TYPE_RBT_CHUNK 15
#define B     32
#define BITS  5
#define MASK  (B - 1)
#define RC_ARENA_SENTINEL INT64_MAX
#define SEQ_HDR_SIZE 2

/* Flat seq layout: [count, flags, elem0, elem1, ...]
 * flags encodes both heap_flag and offset for O(1) tail:
 *   bits 0-31:  heap_flag (1 if elements are heap pointers)
 *   bits 32-63: offset (index of first valid element in elems[])
 * Access: seq[SEQ_HDR_SIZE + FLAT_OFF(seq) + index] */
#define FLAT_OFF(seq) ((int)((uint64_t)(seq)[1] >> 32))
#define FLAT_HF(seq)  ((int)((seq)[1] & 0xFFFFFFFF))
#define FLAT_SET_OFF_HF(seq, off, hf) ((seq)[1] = ((int64_t)(off) << 32) | ((hf) & 0xFFFFFFFF))

/* ===== Types ===== */

typedef struct { int64_t children[B]; } rbt_node_t;
typedef struct { int64_t heap_flag; int64_t elems[B]; } rbt_leaf_t;

typedef struct rbt_chunk {
    int64_t offset;
    int64_t count;
    int64_t elems[B];
    struct rbt_chunk* next;  /* RC-managed */
} rbt_chunk_t;

/* RBT root — length at offset 0 for codegen's inline seq[0] reads.
 * back_off tracks how many trie elements have been consumed from the
 * front (by tail draining into head_buf), avoiding O(n) trie rebuilds. */
typedef struct {
    int64_t length;
    int64_t heap_flag;
    int64_t head_off;
    int64_t head_cnt;
    int64_t head_buf[B];
    rbt_chunk_t* head_next;
    int64_t head_chain_len;
    int64_t tail_cnt;
    int64_t tail_buf[B];
    int64_t back_shift;
    void*   back_root;
    int64_t back_size;    /* total elements ever pushed into trie */
    int64_t back_off;     /* consumed from front of trie (logical offset) */
} rbt_t;

/* Active trie elements = back_size - back_off */
static inline int64_t trie_active(rbt_t* r) {
    return r->back_size - r->back_off;
}

/* ===== Low-level helpers ===== */

static inline __attribute__((always_inline)) int is_rbt(int64_t* seq) {
    if (!seq) return 0;
    return (((int64_t*)seq)[-1] & 0xFF) == RC_TYPE_RBT;
}

static inline __attribute__((always_inline)) int is_unique(void* ptr) {
    if (!ptr) return 0;
    return __atomic_load_n(((int64_t*)ptr) - 2, __ATOMIC_ACQUIRE) == 1;
}

static inline void retain_heap_value(int64_t heap_flag, int64_t value) {
    if (heap_flag && value) yona_rt_rc_inc((void*)(intptr_t)value);
}

static inline void release_heap_value(int64_t heap_flag, int64_t value) {
    if (heap_flag && value) yona_rt_rc_dec((void*)(intptr_t)value);
}

static void retain_heap_range(int64_t heap_flag, const int64_t* values,
                              int64_t count) {
    if (!heap_flag) return;
    for (int64_t i = 0; i < count; ++i)
        retain_heap_value(1, values[i]);
}

/* RBT roots own the values stored directly in their head/tail buffers and,
 * by current destructor contract, the values reachable through head chunks.
 * A shallow root clone therefore needs independent element references even
 * though the chunk nodes themselves are shared. Trie leaves are RC-owned by
 * the shared trie and must not be retained here. */
static void retain_rbt_direct_values(const rbt_t* r) {
    if (!r->heap_flag) return;
    retain_heap_range(1, r->head_buf + r->head_off, r->head_cnt);
    retain_heap_range(1, r->tail_buf, r->tail_cnt);
    for (const rbt_chunk_t* chunk = r->head_next; chunk; chunk = chunk->next)
        retain_heap_range(1, chunk->elems + chunk->offset, chunk->count);
}

static void retain_chunk_values(int64_t heap_flag, const rbt_chunk_t* chunk) {
    if (!heap_flag) return;
    for (; chunk; chunk = chunk->next)
        retain_heap_range(1, chunk->elems + chunk->offset, chunk->count);
}

static rbt_node_t* node_alloc(void) {
    rbt_node_t* n = (rbt_node_t*)rc_alloc(RC_TYPE_RBT_NODE, sizeof(rbt_node_t));
    memset(n, 0, sizeof(rbt_node_t));
    return n;
}

static rbt_leaf_t* leaf_alloc(int64_t hf) {
    rbt_leaf_t* l = (rbt_leaf_t*)rc_alloc(RC_TYPE_RBT_LEAF, sizeof(rbt_leaf_t));
    l->heap_flag = hf;
    memset(l->elems, 0, sizeof(l->elems));
    return l;
}

static rbt_chunk_t* chunk_alloc(void) {
    rbt_chunk_t* c = (rbt_chunk_t*)rc_alloc(RC_TYPE_RBT_CHUNK, sizeof(rbt_chunk_t));
    c->offset = 0;
    c->count = 0;
    c->next = NULL;
    return c;
}

/* Fast rbt allocation for cons path — zeroes only tail/trie fields. */
static rbt_t* rbt_alloc_cons(void) {
    rbt_t* r = (rbt_t*)rc_alloc(RC_TYPE_RBT, sizeof(rbt_t));
    r->head_next = NULL;
    r->head_chain_len = 0;
    r->tail_cnt = 0;
    r->back_shift = 0;
    r->back_root = NULL;
    r->back_size = 0;
    r->back_off = 0;
    return r;
}

static rbt_t* rbt_alloc_zeroed(void) {
    rbt_t* r = (rbt_t*)rc_alloc(RC_TYPE_RBT, sizeof(rbt_t));
    memset(r, 0, sizeof(rbt_t));
    return r;
}

/* Clone rbt header (shallow: rc_inc trie root + head chain). */
static rbt_t* rbt_clone(rbt_t* src) {
    rbt_t* r = (rbt_t*)rc_alloc(RC_TYPE_RBT, sizeof(rbt_t));
    memcpy(r, src, sizeof(rbt_t));
    if (r->back_root) yona_rt_rc_inc(r->back_root);
    if (r->head_next) yona_rt_rc_inc(r->head_next);
    retain_rbt_direct_values(r);
    return r;
}

/* Copy fields from src rbt into a freshly allocated rbt (for non-unique paths). */
static rbt_t* rbt_copy_body(rbt_t* src) {
    rbt_t* nr = (rbt_t*)rc_alloc(RC_TYPE_RBT, sizeof(rbt_t));
    nr->heap_flag = src->heap_flag;
    nr->tail_cnt = src->tail_cnt;
    if (src->tail_cnt)
        memcpy(nr->tail_buf, src->tail_buf, (size_t)src->tail_cnt * sizeof(int64_t));
    nr->back_shift = src->back_shift;
    nr->back_root = src->back_root;
    if (nr->back_root) yona_rt_rc_inc(nr->back_root);
    nr->back_size = src->back_size;
    nr->back_off = src->back_off;
    retain_heap_range(src->heap_flag, nr->tail_buf, nr->tail_cnt);
    return nr;
}

static rbt_node_t* node_copy(rbt_node_t* src) {
    rbt_node_t* n = node_alloc();
    memcpy(n->children, src->children, sizeof(n->children));
    for (int i = 0; i < B; i++)
        if (n->children[i]) yona_rt_rc_inc((void*)(intptr_t)n->children[i]);
    return n;
}

/* ===== Trie operations (right-side only) ===== */

static int64_t trie_get(void* node, int64_t shift, int64_t index) {
    while (shift > 0) {
        node = (void*)(intptr_t)((rbt_node_t*)node)->children[(index >> shift) & MASK];
        shift -= BITS;
    }
    return ((rbt_leaf_t*)node)->elems[index & MASK];
}

static void* trie_push_leaf(void* node, int64_t shift, int64_t trie_idx,
                             rbt_leaf_t* leaf) {
    if (shift == 0) return leaf;
    int slot = (int)((trie_idx >> shift) & MASK);
    rbt_node_t* n = is_unique(node) ? (rbt_node_t*)node
                                     : node_copy((rbt_node_t*)node);
    void* child = (void*)(intptr_t)n->children[slot];
    if (shift == BITS) {
        n->children[slot] = (int64_t)(intptr_t)leaf;
    } else if (!child) {
        rbt_node_t* path = node_alloc();
        void* cur = path;
        for (int64_t s = shift - BITS; s > BITS; s -= BITS) {
            rbt_node_t* next = node_alloc();
            ((rbt_node_t*)cur)->children[(trie_idx >> s) & MASK] =
                (int64_t)(intptr_t)next;
            cur = next;
        }
        ((rbt_node_t*)cur)->children[(trie_idx >> BITS) & MASK] =
            (int64_t)(intptr_t)leaf;
        n->children[slot] = (int64_t)(intptr_t)path;
    } else {
        void* new_child = trie_push_leaf(child, shift - BITS, trie_idx, leaf);
        if (new_child != child && !is_unique(node))
            yona_rt_rc_dec(child);
        n->children[slot] = (int64_t)(intptr_t)new_child;
    }
    return n;
}

static void trie_push_buf(rbt_t* r, int64_t* buf) {
    rbt_leaf_t* leaf = leaf_alloc(r->heap_flag);
    memcpy(leaf->elems, buf, B * sizeof(int64_t));
    if (!r->back_root) {
        r->back_root = leaf;
        r->back_shift = 0;
    } else {
        int64_t cap = 1;
        for (int64_t s = r->back_shift; s >= 0; s -= BITS) cap *= B;
        if (r->back_size >= cap) {
            rbt_node_t* grown = node_alloc();
            yona_rt_rc_inc(r->back_root);
            grown->children[0] = (int64_t)(intptr_t)r->back_root;
            r->back_root = trie_push_leaf(grown, r->back_shift + BITS,
                                           r->back_size, leaf);
            r->back_shift += BITS;
        } else {
            r->back_root = trie_push_leaf(r->back_root, r->back_shift,
                                           r->back_size, leaf);
        }
    }
    r->back_size += B;
}

/* ===== Head chain helpers ===== */

static int64_t chain_get(rbt_chunk_t* chunk, int64_t index) {
    while (chunk) {
        if (index < chunk->count)
            return chunk->elems[chunk->offset + index];
        index -= chunk->count;
        chunk = chunk->next;
    }
    return 0;
}

/* ===== Public API ===== */

int64_t* yona_rt_seq_alloc(int64_t count) {
    /* Reject negatives and sizes that would overflow the byte-count
     * multiplication below. The ceiling is defensive — real programs
     * hit OOM long before this, but silent wraparound is a heap-overflow
     * footgun so we trap instead. */
    if (UNLIKELY(count < 0 ||
                 count > (int64_t)((SIZE_MAX / sizeof(int64_t)) - SEQ_HDR_SIZE))) {
        fprintf(stderr, "yona_rt_seq_alloc: invalid count %lld\n",
                (long long)count);
        abort();
    }
    int64_t* seq = (int64_t*)rc_alloc(RC_TYPE_SEQ,
                                       (SEQ_HDR_SIZE + count) * sizeof(int64_t));
    seq[0] = count;
    seq[1] = 0;
    return seq;
}

void yona_rt_seq_set_heap(int64_t* seq, int64_t flag) {
    if (is_rbt(seq))
        ((rbt_t*)seq)->heap_flag = flag;
    else
        FLAT_SET_OFF_HF(seq, FLAT_OFF(seq), (int)flag);
}

int64_t yona_rt_seq_length(int64_t* seq) { return seq[0]; }

int64_t yona_rt_seq_is_empty(int64_t* seq) {
    if (!seq) return 1;
    return seq[0] == 0;
}

int64_t yona_rt_seq_get(int64_t* seq, int64_t index) {
    if (UNLIKELY(!seq || index < 0 || index >= seq[0])) {
        fprintf(stderr, "yona_rt_seq_get: index %lld out of range (len=%lld)\n",
                (long long)index, (long long)(seq ? seq[0] : 0));
        abort();
    }
    if (LIKELY(!is_rbt(seq))) return seq[SEQ_HDR_SIZE + FLAT_OFF(seq) + index];
    rbt_t* r = (rbt_t*)seq;
    if (LIKELY(index < r->head_cnt))
        return r->head_buf[r->head_off + index];
    index -= r->head_cnt;
    if (index < r->head_chain_len)
        return chain_get(r->head_next, index);
    index -= r->head_chain_len;
    int64_t ta = trie_active(r);
    if (index < ta)
        return trie_get(r->back_root, r->back_shift, r->back_off + index);
    index -= ta;
    return r->tail_buf[index];
}

/* Generic value-return ABI: callers own returned heap values. The primitive
 * seq_get above intentionally remains borrowed for internal hot paths. */
int64_t yona_rt_seq_get_owned(int64_t* seq, int64_t index) {
    int64_t value = yona_rt_seq_get(seq, index);
    const int64_t heap_flag = is_rbt(seq) ? ((rbt_t*)seq)->heap_flag : FLAT_HF(seq);
    retain_heap_value(heap_flag, value);
    return value;
}

void yona_rt_seq_set(int64_t* seq, int64_t index, int64_t value) {
    seq[SEQ_HDR_SIZE + index] = value;
}

int64_t yona_rt_seq_head(int64_t* seq) {
    if (UNLIKELY(is_rbt(seq))) return ((rbt_t*)seq)->head_buf[((rbt_t*)seq)->head_off];
    return seq[SEQ_HDR_SIZE + FLAT_OFF(seq)];
}

/* ===== Flat-to-RBT promotion ===== */

static rbt_t* flat_to_rbt_for_cons(int64_t* flat, int64_t elem) {
    int64_t len = flat[0];
    int hf = FLAT_HF(flat), off = FLAT_OFF(flat);
    rbt_t* r = rbt_alloc_cons();
    r->length = len + 1;
    r->heap_flag = hf;
    r->head_off = B - 1;
    r->head_cnt = 1;
    r->head_buf[B - 1] = elem;
    rbt_chunk_t* c = chunk_alloc();
    c->count = len;
    memcpy(c->elems, flat + SEQ_HDR_SIZE + off, (size_t)len * sizeof(int64_t));
    retain_heap_range(hf, c->elems, len);
    r->head_next = c;
    r->head_chain_len = len;
    return r;
}

static rbt_t* flat_to_rbt_for_snoc(int64_t* flat, int64_t elem) {
    int64_t len = flat[0];
    int hf = FLAT_HF(flat), off = FLAT_OFF(flat);
    /* Note: snoc promotions should use offset-adjusted elements */
    int64_t* base = flat + SEQ_HDR_SIZE + off;
    rbt_t* r = rbt_alloc_zeroed();
    r->length = len + 1;
    r->heap_flag = hf;
    if (len <= B) {
        r->head_off = 0;
        r->head_cnt = len;
        memcpy(r->head_buf, base, (size_t)len * sizeof(int64_t));
        retain_heap_range(hf, r->head_buf, len);
    } else {
        r->head_off = 0;
        r->head_cnt = B;
        memcpy(r->head_buf, base, B * sizeof(int64_t));
        retain_heap_range(hf, r->head_buf, B);
        rbt_chunk_t* c = chunk_alloc();
        c->count = len - B;
        memcpy(c->elems, base + B, (size_t)(len - B) * sizeof(int64_t));
        retain_heap_range(hf, c->elems, len - B);
        r->head_next = c;
        r->head_chain_len = len - B;
    }
    r->tail_cnt = 1;
    r->tail_buf[0] = elem;
    return r;
}

/* ===== Cons (prepend) — O(1) amortized ===== */

/* Callee-borrows: seq's refcount is NOT modified. The codegen is
 * responsible for managing the input's lifetime — it emits rc_dec
 * for anonymous intermediates after cons returns. This preserves the
 * unique-owner (rc==1) in-place mutation path which is critical for
 * foldl+cons patterns (e.g. reverse, build). */
int64_t* yona_rt_seq_cons(int64_t elem, int64_t* seq) {
    int64_t len = seq[0];

    if (LIKELY(!is_rbt(seq))) {
        if (LIKELY(len < B)) {
            int off = FLAT_OFF(seq);
            int hf = FLAT_HF(seq);

            /* If there's offset space, prepend into it (O(1), no copy) */
            if (off > 0) {
                int64_t* hdr = seq - 2;
                if (__atomic_load_n(&hdr[0], __ATOMIC_ACQUIRE) == 1
                    && hdr[0] != RC_ARENA_SENTINEL) {
                    seq[SEQ_HDR_SIZE + off - 1] = elem;
                    seq[0] = len + 1;
                    FLAT_SET_OFF_HF(seq, off - 1, hf);
                    return seq;
                }
            }

            /* No offset space — copy with elem prepended */
            int64_t* res = yona_rt_seq_alloc(len + 1);
            res[SEQ_HDR_SIZE] = elem;
            memcpy(res + SEQ_HDR_SIZE + 1, seq + SEQ_HDR_SIZE + off,
                   (size_t)len * sizeof(int64_t));
            FLAT_SET_OFF_HF(res, 0, hf);
            retain_heap_range(hf, res + SEQ_HDR_SIZE + 1, len);
            return res;
        }
        return (int64_t*)flat_to_rbt_for_cons(seq, elem);
    }

    rbt_t* r = (rbt_t*)seq;

    if (LIKELY(r->head_off > 0)) {
        /* Fast path: room in head_buf (31/32 cons calls) */
        if (LIKELY(is_unique(r))) {
            r->head_off--;
            r->head_cnt++;
            r->length++;
            r->head_buf[r->head_off] = elem;
            return (int64_t*)r;
        }
        rbt_t* nr = rbt_clone(r);
        nr->head_off--;
        nr->head_cnt++;
        nr->length++;
        nr->head_buf[nr->head_off] = elem;
        return (int64_t*)nr;
    }

    /* Head_buf full: chain it into head_next (1/32 cons calls). */
    rbt_chunk_t* c = chunk_alloc();
    c->count = r->head_cnt;
    memcpy(c->elems, r->head_buf, (size_t)r->head_cnt * sizeof(int64_t));
    c->next = r->head_next;

    if (is_unique(r)) {
        r->head_next = c;
        r->head_chain_len += r->head_cnt;
        r->head_off = B - 1;
        r->head_cnt = 1;
        r->head_buf[B - 1] = elem;
        r->length++;
        return (int64_t*)r;
    }
    retain_heap_range(r->heap_flag, c->elems, c->count);
    retain_chunk_values(r->heap_flag, r->head_next);
    if (r->head_next) yona_rt_rc_inc(r->head_next);
    rbt_t* nr = rbt_copy_body(r);
    nr->length = r->length + 1;
    nr->head_off = B - 1;
    nr->head_cnt = 1;
    nr->head_buf[B - 1] = elem;
    nr->head_next = c;
    nr->head_chain_len = r->head_chain_len + r->head_cnt;
    return (int64_t*)nr;
}

/* ===== Tail — O(1) amortized ===== */

/* Callee-borrows variant: seq's refcount is preserved. Used for sites that
 * keep the original seq alive after the tail call (comprehensions walking
 * a borrowed source, generic tail functions that return a fresh ref). */
int64_t* yona_rt_seq_tail(int64_t* seq) {
    int64_t len = seq[0];
    if (UNLIKELY(len <= 1)) return yona_rt_seq_alloc(0);

    if (LIKELY(!is_rbt(seq))) {
        if (LIKELY(len <= B)) {
            /* Offset-based tail: bump offset instead of memmove.
             * For unique owner, modify in place. For shared, copy. */
            int64_t* hdr = seq - 2;
            int off = FLAT_OFF(seq);
            int hf = FLAT_HF(seq);
            if (__atomic_load_n(&hdr[0], __ATOMIC_ACQUIRE) == 1
                && hdr[0] != RC_ARENA_SENTINEL) {
                release_heap_value(hf, seq[SEQ_HDR_SIZE + off]);
                seq[0] = len - 1;
                FLAT_SET_OFF_HF(seq, off + 1, hf);
                return seq;
            }
            /* Shared: copy only the valid elements (no offset) */
            int64_t* res = yona_rt_seq_alloc(len - 1);
            FLAT_SET_OFF_HF(res, 0, hf);
            memcpy(res + SEQ_HDR_SIZE, seq + SEQ_HDR_SIZE + off + 1,
                   (size_t)(len - 1) * sizeof(int64_t));
            retain_heap_range(hf, res + SEQ_HDR_SIZE, len - 1);
            return res;
        }
        /* Large flat seq (>B, from generator): promote to rbt minus first element */
        int hf = FLAT_HF(seq), off = FLAT_OFF(seq);
        int64_t* base = seq + SEQ_HDR_SIZE + off;
        rbt_t* r = rbt_alloc_cons();
        r->length = len - 1;
        r->heap_flag = hf;
        int64_t remain = len - 1;
        if (remain <= B) {
            r->head_off = 0;
            r->head_cnt = remain;
            memcpy(r->head_buf, base + 1, (size_t)remain * sizeof(int64_t));
            retain_heap_range(hf, r->head_buf, remain);
        } else {
            r->head_off = 0;
            r->head_cnt = B;
            memcpy(r->head_buf, base + 1, B * sizeof(int64_t));
            retain_heap_range(hf, r->head_buf, B);
            rbt_chunk_t* prev = NULL;
            rbt_chunk_t* first = NULL;
            int64_t pos = B + 1;
            int64_t chain_total = 0;
            while (pos < len) {
                rbt_chunk_t* c = chunk_alloc();
                int64_t n = len - pos;
                if (n > B) n = B;
                c->count = n;
                memcpy(c->elems, base + pos, (size_t)n * sizeof(int64_t));
                retain_heap_range(hf, c->elems, n);
                chain_total += n;
                if (!first) first = c;
                if (prev) prev->next = c;
                prev = c;
                pos += n;
            }
            r->head_next = first;
            r->head_chain_len = chain_total;
        }
        return (int64_t*)r;
    }

    rbt_t* r = (rbt_t*)seq;

    if (LIKELY(r->head_cnt > 1)) {
        /* Fast path: bump head_off (31/32 tail calls) */
        if (LIKELY(is_unique(r))) {
            release_heap_value(r->heap_flag, r->head_buf[r->head_off]);
            r->head_off++;
            r->head_cnt--;
            r->length--;
            return (int64_t*)r;
        }
        rbt_t* nr = rbt_clone(r);
        release_heap_value(nr->heap_flag, nr->head_buf[nr->head_off]);
        nr->head_off++;
        nr->head_cnt--;
        nr->length--;
        return (int64_t*)nr;
    }

    /* head_cnt == 1: exhausted, pull from chain (1/32 tail calls) */
    if (r->head_next) {
        rbt_chunk_t* c = r->head_next;
        if (is_unique(r)) {
            /* Unique path: r survives, with r->head_next rewritten from c
             * to c->next. Ownership transfers — r used to reach c->next
             * through c, now reaches it directly; still one owner, so
             * c->next's rc is unchanged. c itself loses its owner (r) and
             * must be freed, but c also holds a ref to c->next we don't
             * want to double-count, so sever c->next before rc_dec(c).
             *
             * Previously this was rc_inc(c) + rc_inc(c->next) + rc_dec(c),
             * which netted +1 on c->next per pop. Over a 10K-element foldl
             * that leaked ~311 chunks — the list_* benchmark RBT leak. */
            release_heap_value(r->heap_flag, r->head_buf[r->head_off]);
            r->head_off = c->offset;
            r->head_cnt = c->count;
            memcpy(r->head_buf, c->elems, B * sizeof(int64_t));
            r->head_chain_len -= c->count;
            r->head_next = c->next;
            c->next = NULL;
            yona_rt_rc_dec(c);
            r->length--;
            return (int64_t*)r;
        }
        rbt_t* nr = rbt_copy_body(r);
        nr->length = r->length - 1;
        nr->head_off = c->offset;
        nr->head_cnt = c->count;
        memcpy(nr->head_buf, c->elems, B * sizeof(int64_t));
        retain_heap_range(nr->heap_flag, nr->head_buf + nr->head_off,
                          nr->head_cnt);
        nr->head_next = c->next;
        if (c->next) yona_rt_rc_inc(c->next);
        retain_chunk_values(nr->heap_flag, c->next);
        nr->head_chain_len = r->head_chain_len - c->count;
        return (int64_t*)nr;
    }

    /* Head chain empty. Try back trie (using back_off to skip consumed). */
    int64_t ta = trie_active(r);
    if (ta > 0) {
        const int unique = is_unique(r);
        rbt_t* nr = unique ? r : rbt_clone(r);
        if (unique)
            release_heap_value(r->heap_flag, r->head_buf[r->head_off]);
        else
            release_heap_value(nr->heap_flag, nr->head_buf[nr->head_off]);
        int64_t n = (ta >= B) ? B : ta;
        nr->head_off = 0;
        nr->head_cnt = n;
        for (int64_t i = 0; i < n; i++)
            nr->head_buf[i] = trie_get(r->back_root, r->back_shift, r->back_off + i);
        retain_heap_range(nr->heap_flag, nr->head_buf, n);
        nr->back_off += n;
        nr->length--;
        return (int64_t*)nr;
    }

    /* Back trie empty. Only tail_buf remains. */
    if (r->tail_cnt == 0) return yona_rt_seq_alloc(0);
    int64_t new_len = r->tail_cnt;
    int64_t* res = yona_rt_seq_alloc(new_len);
    res[1] = r->heap_flag;
    memcpy(res + SEQ_HDR_SIZE, r->tail_buf, (size_t)new_len * sizeof(int64_t));
    retain_heap_range(r->heap_flag, res + SEQ_HDR_SIZE, new_len);
    return res;
}

/* Callee-consumes variant: the caller transfers ownership of `seq`, and
 * gets back an owned tail. If seq_tail returned `seq` itself (in-place
 * offset bump on a unique seq), ownership passes through. Otherwise the
 * old seq is now dead from the caller's perspective, so we rc_dec it.
 *
 * Used by pattern-match head-tail (`case s of [h|t] -> … end`): binding
 * `t` to the tail of `s` doesn't semantically keep `s` alive in the arm.
 * With this variant, the callee drops `s` on the copy path instead of
 * the arm codegen needing to rc_inc-the-scrutinee-for-safety (which
 * forced every tail onto the copy path and regressed list_* perf).
 *
 * When the arm body DOES reference the scrutinee by name, the codegen
 * pre-rc_inc's before calling this — the rc_dec inside just balances
 * that inc, leaving the scrutinee alive for further uses. */
int64_t* yona_rt_seq_tail_consume(int64_t* seq) {
    int64_t* res = yona_rt_seq_tail(seq);
    if (res != seq) yona_rt_rc_dec(seq);
    return res;
}

/* ===== Snoc (append) — O(1) amortized ===== */

/* Callee-borrows (same as cons). */
int64_t* yona_rt_seq_snoc(int64_t* seq, int64_t elem) {
    int64_t len = seq[0];

    if (!is_rbt(seq)) {
        if (len < B) {
            int64_t* res = yona_rt_seq_alloc(len + 1);
            memcpy(res + SEQ_HDR_SIZE, seq + SEQ_HDR_SIZE,
                   (size_t)len * sizeof(int64_t));
            res[SEQ_HDR_SIZE + len] = elem;
            res[1] = seq[1];
            retain_heap_range(FLAT_HF(seq), res + SEQ_HDR_SIZE, len);
            return res;
        }
        return (int64_t*)flat_to_rbt_for_snoc(seq, elem);
    }

    rbt_t* r = (rbt_t*)seq;

    if (r->tail_cnt < B) {
        if (is_unique(r)) {
            r->tail_buf[r->tail_cnt++] = elem;
            r->length++;
            return (int64_t*)r;
        }
        rbt_t* nr = rbt_clone(r);
        nr->tail_buf[nr->tail_cnt++] = elem;
        nr->length++;
        return (int64_t*)nr;
    }

    /* Tail buf full: push into back trie */
    if (is_unique(r)) {
        void* old_root = r->back_root;
        trie_push_buf(r, r->tail_buf);
        if (old_root && old_root != r->back_root)
            yona_rt_rc_dec(old_root);
        r->tail_cnt = 1;
        r->tail_buf[0] = elem;
        r->length++;
        return (int64_t*)r;
    }
    rbt_t* nr = rbt_clone(r);
    trie_push_buf(nr, r->tail_buf);
    nr->tail_cnt = 1;
    nr->tail_buf[0] = elem;
    nr->length++;
    return (int64_t*)nr;
}

/* ===== Membership / difference ===== */

int64_t yona_rt_seq_contains(int64_t* seq, int64_t elem) {
    int64_t len = yona_rt_seq_length(seq);
    for (int64_t i = 0; i < len; i++) {
        if (yona_rt_seq_get(seq, i) == elem) return 1;
    }
    return 0;
}

/* Callee-borrows (same as join). Removes every element of `b` from `a`. */
int64_t* yona_rt_seq_difference(int64_t* a, int64_t* b) {
    int64_t la = yona_rt_seq_length(a);
    if (la == 0 || yona_rt_seq_length(b) == 0) {
        yona_rt_rc_inc(a);
        return a;
    }
    int64_t keep = 0;
    for (int64_t i = 0; i < la; i++) {
        if (!yona_rt_seq_contains(b, yona_rt_seq_get(a, i))) keep++;
    }
    if (keep == la) {
        yona_rt_rc_inc(a);
        return a;
    }
    int64_t* res = yona_rt_seq_alloc(keep);
    int64_t hf = is_rbt(a) ? ((rbt_t*)a)->heap_flag : FLAT_HF(a);
    res[1] = hf;
    int64_t j = 0;
    for (int64_t i = 0; i < la; i++) {
        int64_t e = yona_rt_seq_get(a, i);
        if (!yona_rt_seq_contains(b, e))
            res[SEQ_HDR_SIZE + j++] = e;
    }
    retain_heap_range(hf, res + SEQ_HDR_SIZE, keep);
    return res;
}

/* ===== Join (concat) — O(n) ===== */

/* Callee-borrows (same as cons). */
int64_t* yona_rt_seq_join(int64_t* a, int64_t* b) {
    int64_t la = yona_rt_seq_length(a), lb = yona_rt_seq_length(b);
    if (la == 0) {
        yona_rt_rc_inc(b);
        return b;
    }
    if (lb == 0) {
        yona_rt_rc_inc(a);
        return a;
    }
    int64_t* res = yona_rt_seq_alloc(la + lb);
    /* Propagate heap_flag from either operand */
    int64_t hf_a = is_rbt(a) ? ((rbt_t*)a)->heap_flag : a[1];
    int64_t hf_b = is_rbt(b) ? ((rbt_t*)b)->heap_flag : b[1];
    res[1] = hf_a | hf_b;
    for (int64_t i = 0; i < la; i++)
        res[SEQ_HDR_SIZE + i] = yona_rt_seq_get(a, i);
    for (int64_t i = 0; i < lb; i++)
        res[SEQ_HDR_SIZE + la + i] = yona_rt_seq_get(b, i);
    if (hf_a)
        retain_heap_range(1, res + SEQ_HDR_SIZE, la);
    if (hf_b)
        retain_heap_range(1, res + SEQ_HDR_SIZE + la, lb);
    return res;
}

/* ===== Print ===== */

/* Defined in compiled_runtime.c — dispatches on the RC type tag. */
extern void yona_rt_print_heap_value(int64_t val);

void yona_rt_print_seq(int64_t* seq) {
    int64_t len = yona_rt_seq_length(seq);
    int hf = is_rbt(seq) ? (int)((rbt_t*)seq)->heap_flag : FLAT_HF(seq);
    printf("[");
    for (int64_t i = 0; i < len; i++) {
        if (i > 0) printf(", ");
        int64_t elem = yona_rt_seq_get(seq, i);
        if (hf)
            yona_rt_print_heap_value(elem);
        else
            printf("%" PRId64, elem);
    }
    printf("]");
}
