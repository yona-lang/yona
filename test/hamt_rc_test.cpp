/*
 * HAMT destructor must rc_dec heap keys/values (KEY_HEAP / VAL_HEAP).
 * Extra rc_inc keeps a live ref so we can observe the child's remaining
 * count after the set/dict is dropped.
 */

#include <cstdint>
#include <doctest/doctest.h>

extern "C" {
int64_t* yona_rt_seq_alloc(int64_t count);
int64_t* yona_rt_set_alloc(int64_t count);
int64_t* yona_rt_set_insert(int64_t* set, int64_t elem);
int64_t* yona_rt_set_elements(int64_t* set);
int64_t* yona_rt_set_union(int64_t* a, int64_t* b);
int64_t* yona_rt_set_intersection(int64_t* a, int64_t* b);
int64_t* yona_rt_set_difference(int64_t* a, int64_t* b);
void yona_rt_set_put(int64_t* set, int64_t index, int64_t value);
void yona_rt_set_set_heap(int64_t* set, int64_t flag);
int64_t yona_rt_seq_get(int64_t* seq, int64_t index);
int64_t* yona_rt_dict_alloc(int64_t count);
int64_t* yona_rt_dict_put(int64_t* dict, int64_t key, int64_t value);
void yona_rt_dict_set_heap(int64_t* dict, int64_t key_heap, int64_t val_heap);
void yona_rt_rc_inc(void* ptr);
void yona_rt_rc_dec(void* ptr);
}

static int64_t rc_of(void* ptr) { return ((int64_t*)ptr)[-2]; }

TEST_SUITE("HamtRc") {

TEST_CASE("dropping a HAMT set of seqs rc_decs the keys") {
    int64_t* a = yona_rt_seq_alloc(1);
    int64_t* b = yona_rt_seq_alloc(1);
    REQUIRE(a);
    REQUIRE(b);
    yona_rt_rc_inc(a);
    yona_rt_rc_inc(b);
    CHECK(rc_of(a) == 2);
    CHECK(rc_of(b) == 2);

    int64_t* set = yona_rt_set_alloc(0);
    set = yona_rt_set_insert(set, (int64_t)(intptr_t)a);
    set = yona_rt_set_insert(set, (int64_t)(intptr_t)b);
    yona_rt_set_set_heap(set, 1);

    yona_rt_rc_dec(set);

    CHECK(rc_of(a) == 1);
    CHECK(rc_of(b) == 1);
    yona_rt_rc_dec(a);
    yona_rt_rc_dec(b);
}

TEST_CASE("flat set elements retains heap keys for the returned sequence") {
    int64_t* child = yona_rt_seq_alloc(1);
    REQUIRE(child);
    yona_rt_rc_inc(child); // observer reference

    int64_t* set = yona_rt_set_alloc(1);
    yona_rt_set_put(set, 0, (int64_t)(intptr_t)child);
    yona_rt_set_set_heap(set, 1);

    int64_t* elements = yona_rt_set_elements(set);
    REQUIRE(elements);
    CHECK(yona_rt_seq_get(elements, 0) == (int64_t)(intptr_t)child);
    CHECK(rc_of(child) == 3); // observer + set + returned sequence

    yona_rt_rc_dec(set);
    CHECK(rc_of(child) == 2);
    yona_rt_rc_dec(elements);
    CHECK(rc_of(child) == 1);
    yona_rt_rc_dec(child);
}

TEST_CASE("HAMT set elements retains heap keys for the returned sequence") {
    int64_t* child = yona_rt_seq_alloc(1);
    REQUIRE(child);
    yona_rt_rc_inc(child); // observer reference

    int64_t* set = yona_rt_set_alloc(0);
    set = yona_rt_set_insert(set, (int64_t)(intptr_t)child);
    yona_rt_set_set_heap(set, 1);

    int64_t* elements = yona_rt_set_elements(set);
    REQUIRE(elements);
    CHECK(yona_rt_seq_get(elements, 0) == (int64_t)(intptr_t)child);
    CHECK(rc_of(child) == 3); // observer + set + returned sequence

    yona_rt_rc_dec(set);
    CHECK(rc_of(child) == 2);
    yona_rt_rc_dec(elements);
    CHECK(rc_of(child) == 1);
    yona_rt_rc_dec(child);
}

TEST_CASE("set union transfers temporary heap-key ownership to the result") {
    int64_t* left_key = yona_rt_seq_alloc(1);
    int64_t* right_key = yona_rt_seq_alloc(1);
    yona_rt_rc_inc(left_key);  // observer references
    yona_rt_rc_inc(right_key);

    int64_t* left = yona_rt_set_insert(yona_rt_set_alloc(0), (int64_t)(intptr_t)left_key);
    int64_t* right = yona_rt_set_insert(yona_rt_set_alloc(0), (int64_t)(intptr_t)right_key);
    yona_rt_set_set_heap(left, 1);
    yona_rt_set_set_heap(right, 1);

    int64_t* result = yona_rt_set_union(left, right);
    CHECK(rc_of(left_key) == 2);  // observer + result (which reuses left)
    CHECK(rc_of(right_key) == 3); // observer + right + result

    yona_rt_rc_dec(right);
    yona_rt_rc_dec(result);
    CHECK(rc_of(left_key) == 1);
    CHECK(rc_of(right_key) == 1);
    yona_rt_rc_dec(left_key);
    yona_rt_rc_dec(right_key);
}

TEST_CASE("set intersection owns retained heap keys after temporaries are released") {
    int64_t* key = yona_rt_seq_alloc(1);
    yona_rt_rc_inc(key); // observer
    int64_t* left = yona_rt_set_insert(yona_rt_set_alloc(0), (int64_t)(intptr_t)key);
    yona_rt_rc_inc(key); // ownership transferred to the second set
    int64_t* right = yona_rt_set_insert(yona_rt_set_alloc(0), (int64_t)(intptr_t)key);
    yona_rt_set_set_heap(left, 1);
    yona_rt_set_set_heap(right, 1);

    int64_t* result = yona_rt_set_intersection(left, right);
    CHECK(rc_of(key) == 3); // observer + borrowed right + result

    yona_rt_rc_dec(right);
    yona_rt_rc_dec(result);
    CHECK(rc_of(key) == 1);
    yona_rt_rc_dec(key);
}

TEST_CASE("set difference owns retained heap keys after temporaries are released") {
    int64_t* kept = yona_rt_seq_alloc(1);
    int64_t* removed = yona_rt_seq_alloc(1);
    yona_rt_rc_inc(kept); // observer references
    yona_rt_rc_inc(removed);
    int64_t* left = yona_rt_set_insert(yona_rt_set_alloc(0), (int64_t)(intptr_t)kept);
    left = yona_rt_set_insert(left, (int64_t)(intptr_t)removed);
    yona_rt_rc_inc(removed); // ownership transferred to the second set
    int64_t* right = yona_rt_set_insert(yona_rt_set_alloc(0), (int64_t)(intptr_t)removed);
    yona_rt_set_set_heap(left, 1);
    yona_rt_set_set_heap(right, 1);

    int64_t* result = yona_rt_set_difference(left, right);
    CHECK(rc_of(kept) == 2);    // observer + result
    CHECK(rc_of(removed) == 2); // observer + borrowed right

    yona_rt_rc_dec(right);
    yona_rt_rc_dec(result);
    CHECK(rc_of(kept) == 1);
    CHECK(rc_of(removed) == 1);
    yona_rt_rc_dec(kept);
    yona_rt_rc_dec(removed);
}

TEST_CASE("dropping a HAMT dict of seqs rc_decs the values") {
    int64_t* a = yona_rt_seq_alloc(1);
    int64_t* b = yona_rt_seq_alloc(1);
    REQUIRE(a);
    REQUIRE(b);
    yona_rt_rc_inc(a);
    yona_rt_rc_inc(b);

    int64_t* dict = yona_rt_dict_alloc(0);
    dict = yona_rt_dict_put(dict, 1, (int64_t)(intptr_t)a);
    dict = yona_rt_dict_put(dict, 2, (int64_t)(intptr_t)b);
    yona_rt_dict_set_heap(dict, 0, 1);

    yona_rt_rc_dec(dict);

    CHECK(rc_of(a) == 1);
    CHECK(rc_of(b) == 1);
    yona_rt_rc_dec(a);
    yona_rt_rc_dec(b);
}

TEST_CASE("path-copy insert after heap flags does not double-free existing values") {
    int64_t* a = yona_rt_seq_alloc(1);
    int64_t* b = yona_rt_seq_alloc(1);
    REQUIRE(a);
    REQUIRE(b);
    yona_rt_rc_inc(a);
    yona_rt_rc_inc(b);

    int64_t* dict = yona_rt_dict_alloc(0);
    dict = yona_rt_dict_put(dict, 1, (int64_t)(intptr_t)a);
    yona_rt_dict_set_heap(dict, 0, 1);
    dict = yona_rt_dict_put(dict, 2, (int64_t)(intptr_t)b);

    CHECK(rc_of(a) == 2);
    CHECK(rc_of(b) == 2);
    yona_rt_rc_dec(dict);
    CHECK(rc_of(a) == 1);
    CHECK(rc_of(b) == 1);
    yona_rt_rc_dec(a);
    yona_rt_rc_dec(b);
}

TEST_CASE("in-place same-key HAMT put does not double-free the old value") {
    int64_t* oldv = yona_rt_seq_alloc(1);
    int64_t* newv = yona_rt_seq_alloc(1);
    REQUIRE(oldv);
    REQUIRE(newv);
    yona_rt_rc_inc(oldv);
    yona_rt_rc_inc(newv);

    int64_t* dict = yona_rt_dict_alloc(0);
    dict = yona_rt_dict_put(dict, 1, (int64_t)(intptr_t)oldv);
    yona_rt_dict_set_heap(dict, 0, 1);
    dict = yona_rt_dict_put(dict, 1, (int64_t)(intptr_t)newv);

    CHECK(rc_of(oldv) == 1);
    CHECK(rc_of(newv) == 2);
    yona_rt_rc_dec(dict);
    CHECK(rc_of(newv) == 1);
    yona_rt_rc_dec(oldv);
    yona_rt_rc_dec(newv);
}

} // TEST_SUITE("HamtRc")
