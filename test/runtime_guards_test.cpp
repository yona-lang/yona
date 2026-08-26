/*
 * Runtime safety guard tests.
 *
 * The Yona C runtime exposes yona_rt_seq_alloc, yona_rt_seq_get, and
 * hamt_alloc, each of which has explicit bounds/overflow guards (trap
 * via abort on invalid input). Here we exercise the valid range to
 * confirm the guards don't regress the fast path. The abort branches
 * are trivially correct by inspection and would require fork-based
 * death tests to exercise from doctest, which is out of scope.
 */

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>

extern "C" {
int64_t* yona_rt_seq_alloc(int64_t count);
int64_t yona_rt_seq_get(int64_t* seq, int64_t index);
int64_t yona_rt_seq_length(int64_t* seq);
void yona_rt_seq_set(int64_t* seq, int64_t index, int64_t value);
void yona_rt_seq_set_heap(int64_t* seq, int64_t flag);
int64_t* yona_rt_seq_tail(int64_t* seq);
int64_t* yona_rt_seq_tail_consume(int64_t* seq);
int64_t* yona_rt_seq_cons(int64_t value, int64_t* seq);
int64_t* yona_rt_seq_snoc(int64_t* seq, int64_t value);
int64_t* yona_rt_seq_join(int64_t* left, int64_t* right);
int64_t* yona_rt_seq_difference(int64_t* left, int64_t* right);
int64_t yona_rt_adt_get_field(void* node, int64_t index);
int64_t* yona_Std_Http__parseUrl(const char* url);
int64_t* yona_platform_list_dir(const char* path);
void* yona_rt_rc_alloc_string_len(size_t bytes, size_t string_length);
int64_t yona_Std_String__chars(const char* value);
int64_t yona_Std_String__length(const char* value);
int64_t yona_Std_String__charAt(const char* value, int64_t index);
const char* yona_Std_String__fromChars(int64_t* values);
int64_t yona_Std_Set__iterator(int64_t* set);
int64_t* yona_rt_set_alloc(int64_t count);
int64_t* yona_rt_set_insert(int64_t* set, int64_t value);
void yona_rt_rc_dec(void* ptr);
void yona_rt_rc_inc(void* ptr);
}

static int64_t runtime_rc_of(void* ptr) { return ((int64_t*)ptr)[-2]; }

TEST_SUITE("RuntimeGuards") {

TEST_CASE("seq_alloc accepts zero count") {
    int64_t* s = yona_rt_seq_alloc(0);
    REQUIRE(s != nullptr);
    CHECK(yona_rt_seq_length(s) == 0);
    yona_rt_rc_dec(s);
}

TEST_CASE("native iterators own and release their sources on early drop") {
    auto* text = static_cast<char*>(yona_rt_rc_alloc_string_len(4, 3));
    std::memcpy(text, "abc", 4);
    auto* chars = reinterpret_cast<int64_t*>(yona_Std_String__chars(text));
    REQUIRE(chars != nullptr);
    CHECK(runtime_rc_of(text) == 2);
    yona_rt_rc_dec(chars);
    CHECK(runtime_rc_of(text) == 1);
    yona_rt_rc_dec(text);

    auto* set = yona_rt_set_insert(yona_rt_set_alloc(0), 42);
    auto* elements = reinterpret_cast<int64_t*>(yona_Std_Set__iterator(set));
    REQUIRE(elements != nullptr);
    CHECK(runtime_rc_of(set) == 2);
    yona_rt_rc_dec(elements);
    CHECK(runtime_rc_of(set) == 1);
    yona_rt_rc_dec(set);
}

TEST_CASE("UTF-8 scalar APIs replace malformed native input deterministically") {
    const char malformed[] = {static_cast<char>(0xc3), '(', '\0'};
    CHECK(yona_Std_String__length(malformed) == 2);
    CHECK(yona_Std_String__charAt(malformed, 0) == 0xfffd);
    CHECK(yona_Std_String__charAt(malformed, 1) == '(');

    auto* scalars = yona_rt_seq_alloc(4);
    yona_rt_seq_set(scalars, 0, 0x61);
    yona_rt_seq_set(scalars, 1, 0x3b2);
    yona_rt_seq_set(scalars, 2, 0x1f600);
    yona_rt_seq_set(scalars, 3, 0x7a);
    const char* encoded = yona_Std_String__fromChars(scalars);
    CHECK(std::strcmp(encoded, "aβ😀z") == 0);
    yona_rt_rc_dec((void*)encoded);
    yona_rt_rc_dec(scalars);
}

TEST_CASE("seq_alloc + set + get round-trip on small seq") {
    int64_t* s = yona_rt_seq_alloc(4);
    REQUIRE(s != nullptr);
    CHECK(yona_rt_seq_length(s) == 4);
    for (int i = 0; i < 4; i++) yona_rt_seq_set(s, i, (int64_t)(i * 10));
    for (int i = 0; i < 4; i++) CHECK(yona_rt_seq_get(s, i) == i * 10);
    yona_rt_rc_dec(s);
}

TEST_CASE("seq_alloc accepts medium sizes") {
    int64_t* s = yona_rt_seq_alloc(1024);
    REQUIRE(s != nullptr);
    CHECK(yona_rt_seq_length(s) == 1024);
    yona_rt_seq_set(s, 1023, 42);
    CHECK(yona_rt_seq_get(s, 1023) == 42);
    yona_rt_rc_dec(s);
}

TEST_CASE("shared flat sequence tails retain heap elements") {
    auto* removed = yona_rt_seq_alloc(0);
    auto* kept = yona_rt_seq_alloc(0);
    yona_rt_rc_inc(removed); // observer + source sequence
    yona_rt_rc_inc(kept);    // observer + source sequence

    auto* source = yona_rt_seq_alloc(2);
    yona_rt_seq_set(source, 0, (int64_t)(intptr_t)removed);
    yona_rt_seq_set(source, 1, (int64_t)(intptr_t)kept);
    yona_rt_seq_set_heap(source, 1);
    yona_rt_rc_inc(source); // force the persistent copy path

    auto* tail = yona_rt_seq_tail(source);
    REQUIRE(tail != source);
    CHECK(runtime_rc_of(removed) == 2);
    CHECK(runtime_rc_of(kept) == 3); // observer + source + tail

    yona_rt_rc_dec(source);
    yona_rt_rc_dec(source);
    CHECK(runtime_rc_of(removed) == 1);
    CHECK(runtime_rc_of(kept) == 2);
    yona_rt_rc_dec(tail);
    CHECK(runtime_rc_of(kept) == 1);
    yona_rt_rc_dec(removed);
    yona_rt_rc_dec(kept);
}

TEST_CASE("consuming a unique flat tail releases only its removed heap head") {
    auto* removed = yona_rt_seq_alloc(0);
    auto* kept = yona_rt_seq_alloc(0);
    yona_rt_rc_inc(removed); // observer + source sequence
    yona_rt_rc_inc(kept);    // observer + source sequence

    auto* source = yona_rt_seq_alloc(2);
    yona_rt_seq_set(source, 0, (int64_t)(intptr_t)removed);
    yona_rt_seq_set(source, 1, (int64_t)(intptr_t)kept);
    yona_rt_seq_set_heap(source, 1);

    auto* tail = yona_rt_seq_tail_consume(source);
    REQUIRE(tail == source);
    CHECK(runtime_rc_of(removed) == 1);
    CHECK(runtime_rc_of(kept) == 2);
    yona_rt_rc_dec(tail);
    CHECK(runtime_rc_of(kept) == 1);
    yona_rt_rc_dec(removed);
    yona_rt_rc_dec(kept);
}

TEST_CASE("persistent sequence transformations retain copied heap elements") {
    SUBCASE("cons") {
        auto* existing = yona_rt_seq_alloc(0);
        auto* added = yona_rt_seq_alloc(0);
        yona_rt_rc_inc(existing); // source ownership
        yona_rt_rc_inc(added);    // result ownership supplied by caller
        auto* source = yona_rt_seq_alloc(1);
        yona_rt_seq_set(source, 0, (int64_t)(intptr_t)existing);
        yona_rt_seq_set_heap(source, 1);

        auto* result = yona_rt_seq_cons((int64_t)(intptr_t)added, source);
        yona_rt_seq_set_heap(result, 1);
        CHECK(runtime_rc_of(existing) == 3);
        yona_rt_rc_dec(source);
        CHECK(runtime_rc_of(existing) == 2);
        yona_rt_rc_dec(result);
        CHECK(runtime_rc_of(existing) == 1);
        CHECK(runtime_rc_of(added) == 1);
        yona_rt_rc_dec(existing);
        yona_rt_rc_dec(added);
    }

    SUBCASE("snoc") {
        auto* existing = yona_rt_seq_alloc(0);
        auto* added = yona_rt_seq_alloc(0);
        yona_rt_rc_inc(existing);
        yona_rt_rc_inc(added);
        auto* source = yona_rt_seq_alloc(1);
        yona_rt_seq_set(source, 0, (int64_t)(intptr_t)existing);
        yona_rt_seq_set_heap(source, 1);

        auto* result = yona_rt_seq_snoc(source, (int64_t)(intptr_t)added);
        yona_rt_seq_set_heap(result, 1);
        CHECK(runtime_rc_of(existing) == 3);
        yona_rt_rc_dec(source);
        yona_rt_rc_dec(result);
        CHECK(runtime_rc_of(existing) == 1);
        CHECK(runtime_rc_of(added) == 1);
        yona_rt_rc_dec(existing);
        yona_rt_rc_dec(added);
    }

    SUBCASE("join") {
        auto* left_value = yona_rt_seq_alloc(0);
        auto* right_value = yona_rt_seq_alloc(0);
        yona_rt_rc_inc(left_value);
        yona_rt_rc_inc(right_value);
        auto* left = yona_rt_seq_alloc(1);
        auto* right = yona_rt_seq_alloc(1);
        yona_rt_seq_set(left, 0, (int64_t)(intptr_t)left_value);
        yona_rt_seq_set(right, 0, (int64_t)(intptr_t)right_value);
        yona_rt_seq_set_heap(left, 1);
        yona_rt_seq_set_heap(right, 1);

        auto* result = yona_rt_seq_join(left, right);
        CHECK(runtime_rc_of(left_value) == 3);
        CHECK(runtime_rc_of(right_value) == 3);
        yona_rt_rc_dec(left);
        yona_rt_rc_dec(right);
        yona_rt_rc_dec(result);
        CHECK(runtime_rc_of(left_value) == 1);
        CHECK(runtime_rc_of(right_value) == 1);
        yona_rt_rc_dec(left_value);
        yona_rt_rc_dec(right_value);
    }

    SUBCASE("difference") {
        auto* kept = yona_rt_seq_alloc(0);
        auto* removed = yona_rt_seq_alloc(0);
        yona_rt_rc_inc(kept);
        yona_rt_rc_inc(removed);
        auto* left = yona_rt_seq_alloc(2);
        auto* right = yona_rt_seq_alloc(1);
        yona_rt_seq_set(left, 0, (int64_t)(intptr_t)kept);
        yona_rt_seq_set(left, 1, (int64_t)(intptr_t)removed);
        yona_rt_seq_set(right, 0, (int64_t)(intptr_t)removed);
        yona_rt_seq_set_heap(left, 1);
        yona_rt_seq_set_heap(right, 1);
        yona_rt_rc_inc(removed); // right also owns the repeated pointer

        auto* result = yona_rt_seq_difference(left, right);
        CHECK(runtime_rc_of(kept) == 3);
        yona_rt_rc_dec(left);
        yona_rt_rc_dec(right);
        yona_rt_rc_dec(result);
        CHECK(runtime_rc_of(kept) == 1);
        CHECK(runtime_rc_of(removed) == 1);
        yona_rt_rc_dec(kept);
        yona_rt_rc_dec(removed);
    }
}

TEST_CASE("shared RBT tails retain direct and chunked heap elements") {
    auto* child = yona_rt_seq_alloc(0); // observer reference
    auto* flat = yona_rt_seq_alloc(40);
    for (int64_t i = 0; i < 40; ++i) {
        yona_rt_rc_inc(child); // one ownership reference per element slot
        yona_rt_seq_set(flat, i, (int64_t)(intptr_t)child);
    }
    yona_rt_seq_set_heap(flat, 1);

    auto* rbt = yona_rt_seq_tail(flat); // 39 copied values
    REQUIRE(rbt != flat);
    CHECK(runtime_rc_of(child) == 80); // observer + flat(40) + rbt(39)
    yona_rt_rc_dec(flat);
    CHECK(runtime_rc_of(child) == 40);

    yona_rt_rc_inc(rbt); // force the shared RBT clone path
    auto* tail = yona_rt_seq_tail(rbt);
    REQUIRE(tail != rbt);
    CHECK(runtime_rc_of(child) == 78); // observer + rbt(39) + tail(38)
    yona_rt_rc_dec(rbt);
    yona_rt_rc_dec(rbt);
    CHECK(runtime_rc_of(child) == 39);
    yona_rt_rc_dec(tail);
    CHECK(runtime_rc_of(child) == 1);
    yona_rt_rc_dec(child);
}

TEST_CASE("HTTP URL parser uses a mixed-field runtime value") {
    int64_t* parsed = yona_Std_Http__parseUrl("https://example.com:8443/api");
    REQUIRE(parsed != nullptr);

    auto* host = reinterpret_cast<const char*>(yona_rt_adt_get_field(parsed, 0));
    CHECK(std::strcmp(host, "example.com") == 0);
    CHECK(yona_rt_adt_get_field(parsed, 1) == 8443);
    auto* path = reinterpret_cast<const char*>(yona_rt_adt_get_field(parsed, 2));
    CHECK(std::strcmp(path, "/api") == 0);

    yona_rt_rc_dec(parsed);
}

TEST_CASE("platform listDir stores every name after the sequence header") {
    int64_t* entries = yona_platform_list_dir(".");
    REQUIRE(entries != nullptr);
    const int64_t count = yona_rt_seq_length(entries);
    REQUIRE(count > 0);
    for (int64_t i = 0; i < count; ++i) {
        auto* name = reinterpret_cast<const char*>(yona_rt_seq_get(entries, i));
        REQUIRE(name != nullptr);
        CHECK(name[0] != '\0');
    }
    yona_rt_rc_dec(entries);
}

} // TEST_SUITE("RuntimeGuards")
