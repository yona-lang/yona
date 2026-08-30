/*
 * HAMT destructor must rc_dec heap keys/values (KEY_HEAP / VAL_HEAP).
 * Extra rc_inc keeps a live ref so we can observe the child's remaining
 * count after the set/dict is dropped.
 */

#include <doctest/doctest.h>

#include <cstdint>

extern "C" {
int64_t *YonaRuntimeSequenceAllocate(int64_t count);
int64_t *YonaRuntimeSetAllocate(int64_t count);
int64_t *YonaRuntimeSetInsert(int64_t *set, int64_t elem);
int64_t *YonaRuntimeSetElements(int64_t *set);
int64_t *YonaRuntimeSetUnion(int64_t *a, int64_t *b);
int64_t *YonaRuntimeSetIntersection(int64_t *a, int64_t *b);
int64_t *YonaRuntimeSetDifference(int64_t *a, int64_t *b);
void YonaRuntimeSetSetHeap(int64_t *set, int64_t flag);
int64_t YonaRuntimeSequenceGet(int64_t *seq, int64_t index);
int64_t *YonaRuntimeDictionaryAllocate(int64_t count);
int64_t *YonaRuntimeDictionaryPut(int64_t *dict, int64_t key, int64_t value);
void YonaRuntimeDictionarySetHeap(int64_t *dict, int64_t key_heap,
                                  int64_t val_heap);
void YonaRuntimeRetain(void *ptr);
void YonaRuntimeRelease(void *ptr);
}

static int64_t rc_of(void *ptr) { return ((int64_t *)ptr)[-2]; }

TEST_SUITE("HamtRc") {

  TEST_CASE("dropping a HAMT set of seqs rc_decs the keys") {
    int64_t *a = YonaRuntimeSequenceAllocate(1);
    int64_t *b = YonaRuntimeSequenceAllocate(1);
    REQUIRE(a);
    REQUIRE(b);
    YonaRuntimeRetain(a);
    YonaRuntimeRetain(b);
    CHECK(rc_of(a) == 2);
    CHECK(rc_of(b) == 2);

    int64_t *set = YonaRuntimeSetAllocate(0);
    set = YonaRuntimeSetInsert(set, (int64_t)(intptr_t)a);
    set = YonaRuntimeSetInsert(set, (int64_t)(intptr_t)b);
    YonaRuntimeSetSetHeap(set, 1);

    YonaRuntimeRelease(set);

    CHECK(rc_of(a) == 1);
    CHECK(rc_of(b) == 1);
    YonaRuntimeRelease(a);
    YonaRuntimeRelease(b);
  }

  TEST_CASE("set elements retains heap keys for the returned sequence") {
    int64_t *child = YonaRuntimeSequenceAllocate(1);
    REQUIRE(child);
    YonaRuntimeRetain(child); // observer reference

    int64_t *set = YonaRuntimeSetAllocate(1);
    set = YonaRuntimeSetInsert(set, (int64_t)(intptr_t)child);
    YonaRuntimeSetSetHeap(set, 1);

    int64_t *elements = YonaRuntimeSetElements(set);
    REQUIRE(elements);
    CHECK(YonaRuntimeSequenceGet(elements, 0) == (int64_t)(intptr_t)child);
    CHECK(rc_of(child) == 3); // observer + set + returned sequence

    YonaRuntimeRelease(set);
    CHECK(rc_of(child) == 2);
    YonaRuntimeRelease(elements);
    CHECK(rc_of(child) == 1);
    YonaRuntimeRelease(child);
  }

  TEST_CASE("HAMT set elements retains heap keys for the returned sequence") {
    int64_t *child = YonaRuntimeSequenceAllocate(1);
    REQUIRE(child);
    YonaRuntimeRetain(child); // observer reference

    int64_t *set = YonaRuntimeSetAllocate(0);
    set = YonaRuntimeSetInsert(set, (int64_t)(intptr_t)child);
    YonaRuntimeSetSetHeap(set, 1);

    int64_t *elements = YonaRuntimeSetElements(set);
    REQUIRE(elements);
    CHECK(YonaRuntimeSequenceGet(elements, 0) == (int64_t)(intptr_t)child);
    CHECK(rc_of(child) == 3); // observer + set + returned sequence

    YonaRuntimeRelease(set);
    CHECK(rc_of(child) == 2);
    YonaRuntimeRelease(elements);
    CHECK(rc_of(child) == 1);
    YonaRuntimeRelease(child);
  }

  TEST_CASE("set union transfers temporary heap-key ownership to the result") {
    int64_t *left_key = YonaRuntimeSequenceAllocate(1);
    int64_t *right_key = YonaRuntimeSequenceAllocate(1);
    YonaRuntimeRetain(left_key); // observer references
    YonaRuntimeRetain(right_key);

    int64_t *left = YonaRuntimeSetInsert(YonaRuntimeSetAllocate(0),
                                         (int64_t)(intptr_t)left_key);
    int64_t *right = YonaRuntimeSetInsert(YonaRuntimeSetAllocate(0),
                                          (int64_t)(intptr_t)right_key);
    YonaRuntimeSetSetHeap(left, 1);
    YonaRuntimeSetSetHeap(right, 1);

    int64_t *result = YonaRuntimeSetUnion(left, right);
    CHECK(rc_of(left_key) == 2);  // observer + result (which reuses left)
    CHECK(rc_of(right_key) == 3); // observer + right + result

    YonaRuntimeRelease(right);
    YonaRuntimeRelease(result);
    CHECK(rc_of(left_key) == 1);
    CHECK(rc_of(right_key) == 1);
    YonaRuntimeRelease(left_key);
    YonaRuntimeRelease(right_key);
  }

  TEST_CASE("set intersection owns retained heap keys after temporaries are "
            "released") {
    int64_t *key = YonaRuntimeSequenceAllocate(1);
    YonaRuntimeRetain(key); // observer
    int64_t *left =
        YonaRuntimeSetInsert(YonaRuntimeSetAllocate(0), (int64_t)(intptr_t)key);
    YonaRuntimeRetain(key); // ownership transferred to the second set
    int64_t *right =
        YonaRuntimeSetInsert(YonaRuntimeSetAllocate(0), (int64_t)(intptr_t)key);
    YonaRuntimeSetSetHeap(left, 1);
    YonaRuntimeSetSetHeap(right, 1);

    int64_t *result = YonaRuntimeSetIntersection(left, right);
    CHECK(rc_of(key) == 3); // observer + borrowed right + result

    YonaRuntimeRelease(right);
    YonaRuntimeRelease(result);
    CHECK(rc_of(key) == 1);
    YonaRuntimeRelease(key);
  }

  TEST_CASE(
      "set difference owns retained heap keys after temporaries are released") {
    int64_t *kept = YonaRuntimeSequenceAllocate(1);
    int64_t *removed = YonaRuntimeSequenceAllocate(1);
    YonaRuntimeRetain(kept); // observer references
    YonaRuntimeRetain(removed);
    int64_t *left = YonaRuntimeSetInsert(YonaRuntimeSetAllocate(0),
                                         (int64_t)(intptr_t)kept);
    left = YonaRuntimeSetInsert(left, (int64_t)(intptr_t)removed);
    YonaRuntimeRetain(removed); // ownership transferred to the second set
    int64_t *right = YonaRuntimeSetInsert(YonaRuntimeSetAllocate(0),
                                          (int64_t)(intptr_t)removed);
    YonaRuntimeSetSetHeap(left, 1);
    YonaRuntimeSetSetHeap(right, 1);

    int64_t *result = YonaRuntimeSetDifference(left, right);
    CHECK(rc_of(kept) == 2);    // observer + result
    CHECK(rc_of(removed) == 2); // observer + borrowed right

    YonaRuntimeRelease(right);
    YonaRuntimeRelease(result);
    CHECK(rc_of(kept) == 1);
    CHECK(rc_of(removed) == 1);
    YonaRuntimeRelease(kept);
    YonaRuntimeRelease(removed);
  }

  TEST_CASE("dropping a HAMT dict of seqs rc_decs the values") {
    int64_t *a = YonaRuntimeSequenceAllocate(1);
    int64_t *b = YonaRuntimeSequenceAllocate(1);
    REQUIRE(a);
    REQUIRE(b);
    YonaRuntimeRetain(a);
    YonaRuntimeRetain(b);

    int64_t *dict = YonaRuntimeDictionaryAllocate(0);
    dict = YonaRuntimeDictionaryPut(dict, 1, (int64_t)(intptr_t)a);
    dict = YonaRuntimeDictionaryPut(dict, 2, (int64_t)(intptr_t)b);
    YonaRuntimeDictionarySetHeap(dict, 0, 1);

    YonaRuntimeRelease(dict);

    CHECK(rc_of(a) == 1);
    CHECK(rc_of(b) == 1);
    YonaRuntimeRelease(a);
    YonaRuntimeRelease(b);
  }

  TEST_CASE("path-copy insert after heap flags does not double-free existing "
            "values") {
    int64_t *a = YonaRuntimeSequenceAllocate(1);
    int64_t *b = YonaRuntimeSequenceAllocate(1);
    REQUIRE(a);
    REQUIRE(b);
    YonaRuntimeRetain(a);
    YonaRuntimeRetain(b);

    int64_t *dict = YonaRuntimeDictionaryAllocate(0);
    dict = YonaRuntimeDictionaryPut(dict, 1, (int64_t)(intptr_t)a);
    YonaRuntimeDictionarySetHeap(dict, 0, 1);
    dict = YonaRuntimeDictionaryPut(dict, 2, (int64_t)(intptr_t)b);

    CHECK(rc_of(a) == 2);
    CHECK(rc_of(b) == 2);
    YonaRuntimeRelease(dict);
    CHECK(rc_of(a) == 1);
    CHECK(rc_of(b) == 1);
    YonaRuntimeRelease(a);
    YonaRuntimeRelease(b);
  }

  TEST_CASE("in-place same-key HAMT put does not double-free the old value") {
    int64_t *oldv = YonaRuntimeSequenceAllocate(1);
    int64_t *newv = YonaRuntimeSequenceAllocate(1);
    REQUIRE(oldv);
    REQUIRE(newv);
    YonaRuntimeRetain(oldv);
    YonaRuntimeRetain(newv);

    int64_t *dict = YonaRuntimeDictionaryAllocate(0);
    dict = YonaRuntimeDictionaryPut(dict, 1, (int64_t)(intptr_t)oldv);
    YonaRuntimeDictionarySetHeap(dict, 0, 1);
    dict = YonaRuntimeDictionaryPut(dict, 1, (int64_t)(intptr_t)newv);

    CHECK(rc_of(oldv) == 1);
    CHECK(rc_of(newv) == 2);
    YonaRuntimeRelease(dict);
    CHECK(rc_of(newv) == 1);
    YonaRuntimeRelease(oldv);
    YonaRuntimeRelease(newv);
  }

} // TEST_SUITE("HamtRc")
