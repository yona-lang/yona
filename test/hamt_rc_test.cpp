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
void yona_rt_set_set_heap(int64_t* set, int64_t flag);
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
