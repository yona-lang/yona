/*
 * Runtime safety guard tests.
 *
 * The Yona C runtime exposes YonaRuntimeSequenceAllocate,
 * YonaRuntimeSequenceGet, and hamt_alloc, each of which has explicit
 * bounds/overflow guards (trap via abort on invalid input). Here we exercise
 * the valid range to confirm the guards don't regress the fast path. The abort
 * branches are trivially correct by inspection and would require fork-based
 * death tests to exercise from doctest, which is out of scope.
 */

#include "yona/Runtime/Concurrency/Channel.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <memory>

extern "C" {
int64_t *YonaRuntimeSequenceAllocate(int64_t count);
int64_t YonaRuntimeSequenceGet(int64_t *seq, int64_t index);
int64_t YonaRuntimeSequenceLength(int64_t *seq);
void YonaRuntimeSequenceSet(int64_t *seq, int64_t index, int64_t value);
void YonaRuntimeSequenceSetHeap(int64_t *seq, int64_t flag);
int64_t *YonaRuntimeSequenceTail(int64_t *seq);
int64_t *YonaRuntimeSequenceConsumeTail(int64_t *seq);
int64_t *YonaRuntimeSequencePrepend(int64_t value, int64_t *seq);
int64_t *YonaRuntimeSequenceAppend(int64_t *seq, int64_t value);
int64_t *YonaRuntimeSequenceJoin(int64_t *left, int64_t *right);
int64_t *YonaRuntimeSequenceDifference(int64_t *left, int64_t *right);
int64_t YonaRuntimeAdtGetField(void *node, int64_t index);
int64_t *YonaStdHttpParseUrl(const char *url);
int64_t *YonaRuntimePlatformListDirectory(const char *path);
void *YonaRuntimeAllocateStringWithLength(size_t bytes, size_t string_length);
int64_t YonaStdStringChars(const char *value);
int64_t YonaStdStringLength(const char *value);
int64_t YonaStdStringCharAt(const char *value, int64_t index);
const char *YonaStdStringFromChars(int64_t *values);
int64_t YonaStdSetIterator(int64_t *set);
int64_t *YonaRuntimeSetAllocate(int64_t count);
int64_t *YonaRuntimeSetInsert(int64_t *set, int64_t value);
void YonaRuntimeRelease(void *ptr);
void YonaRuntimeRetain(void *ptr);
}

static int64_t runtime_rc_of(void *ptr) { return ((int64_t *)ptr)[-2]; }

TEST_SUITE("RuntimeGuards") {

  TEST_CASE("seq_alloc accepts zero count") {
    int64_t *s = YonaRuntimeSequenceAllocate(0);
    REQUIRE(s != nullptr);
    CHECK(YonaRuntimeSequenceLength(s) == 0);
    YonaRuntimeRelease(s);
  }

  TEST_CASE("native iterators own and release their sources on early drop") {
    auto *text = static_cast<char *>(YonaRuntimeAllocateStringWithLength(4, 3));
    std::memcpy(text, "abc", 4);
    auto *chars = reinterpret_cast<int64_t *>(YonaStdStringChars(text));
    REQUIRE(chars != nullptr);
    CHECK(runtime_rc_of(text) == 2);
    YonaRuntimeRelease(chars);
    CHECK(runtime_rc_of(text) == 1);
    YonaRuntimeRelease(text);

    auto *set = YonaRuntimeSetInsert(YonaRuntimeSetAllocate(0), 42);
    auto *elements = reinterpret_cast<int64_t *>(YonaStdSetIterator(set));
    REQUIRE(elements != nullptr);
    CHECK(runtime_rc_of(set) == 2);
    YonaRuntimeRelease(elements);
    CHECK(runtime_rc_of(set) == 1);
    YonaRuntimeRelease(set);
  }

  TEST_CASE("typed channels release buffered reference-counted payloads") {
    auto *child = YonaRuntimeSequenceAllocate(0); // observer reference
    YonaRuntimeRetain(child); // ownership transferred to the channel

    auto *channel =
        YonaRuntimeChannelCreate(1, &YonaRuntimeReferenceTypeDescriptor);
    YonaRuntimeChannelSend(
        channel, static_cast<int64_t>(reinterpret_cast<intptr_t>(child)));
    CHECK(runtime_rc_of(child) == 2);

    YonaRuntimeRelease(channel);
    CHECK(runtime_rc_of(child) == 1);
    YonaRuntimeRelease(child);
  }

  TEST_CASE("typed channel receive transfers payload ownership to Option") {
    auto *child = YonaRuntimeSequenceAllocate(0); // observer reference
    YonaRuntimeRetain(child); // ownership transferred to the channel

    auto *channel =
        YonaRuntimeChannelCreate(1, &YonaRuntimeReferenceTypeDescriptor);
    YonaRuntimeChannelSend(
        channel, static_cast<int64_t>(reinterpret_cast<intptr_t>(child)));

    auto *option =
        reinterpret_cast<int64_t *>(YonaRuntimeChannelReceive(channel));
    REQUIRE(option != nullptr);
    CHECK(option[0] == 0);
    CHECK(option[1] == 1);
    CHECK(option[2] == 1);
    CHECK(option[3] == static_cast<int64_t>(reinterpret_cast<intptr_t>(child)));

    YonaRuntimeRelease(option);
    CHECK(runtime_rc_of(child) == 1);
    YonaRuntimeRelease(channel);
    YonaRuntimeRelease(child);
  }

  TEST_CASE("channel constructor returns owning linear endpoint wrappers") {
    YonaTypeDescriptor descriptor = YonaRuntimeReferenceTypeDescriptor;
    std::unique_ptr<void, decltype(&YonaRuntimeRelease)> tuple(
        YonaStdChannelChannel(4, &descriptor), &YonaRuntimeRelease);
    REQUIRE(tuple != nullptr);

    auto *tuple_value = static_cast<int64_t *>(tuple.get());
    CHECK(runtime_rc_of(tuple_value) == 1);
    REQUIRE(tuple_value[0] == 2);
    CHECK(tuple_value[1] == 3);

    auto *sender_linear = reinterpret_cast<int64_t *>(tuple_value[2]);
    auto *receiver_linear = reinterpret_cast<int64_t *>(tuple_value[3]);
    REQUIRE(sender_linear != nullptr);
    REQUIRE(receiver_linear != nullptr);
    CHECK(runtime_rc_of(sender_linear) == 1);
    CHECK(runtime_rc_of(receiver_linear) == 1);
    REQUIRE(sender_linear[0] == 0);
    REQUIRE(receiver_linear[0] == 0);
    REQUIRE(sender_linear[1] == 1);
    REQUIRE(receiver_linear[1] == 1);
    CHECK(sender_linear[2] == 1);
    CHECK(receiver_linear[2] == 1);

    auto *sender = reinterpret_cast<int64_t *>(sender_linear[3]);
    auto *receiver = reinterpret_cast<int64_t *>(receiver_linear[3]);
    REQUIRE(sender != nullptr);
    REQUIRE(receiver != nullptr);
    CHECK(runtime_rc_of(sender) == 1);
    CHECK(runtime_rc_of(receiver) == 1);
    REQUIRE(sender[0] == 0);
    REQUIRE(receiver[0] == 0);
    REQUIRE(sender[1] == 1);
    REQUIRE(receiver[1] == 1);
    CHECK(sender[2] == 1);
    CHECK(receiver[2] == 1);

    auto *sender_raw = reinterpret_cast<void *>(sender[3]);
    auto *receiver_raw = reinterpret_cast<void *>(receiver[3]);
    REQUIRE(sender_raw != nullptr);
    CHECK(receiver_raw == sender_raw);
    CHECK(runtime_rc_of(sender_raw) == 2);

    descriptor = YonaRuntimeUnmanagedTypeDescriptor;
    auto *child = YonaRuntimeSequenceAllocate(0);
    YonaRuntimeRetain(child);
    YonaRuntimeChannelSend(
        static_cast<YonaChannelRef>(sender_raw),
        static_cast<int64_t>(reinterpret_cast<intptr_t>(child)));
    CHECK(runtime_rc_of(child) == 2);

    tuple.reset();
    CHECK(runtime_rc_of(child) == 1);
    YonaRuntimeRelease(child);
  }

  TEST_CASE(
      "UTF-8 scalar APIs replace malformed native input deterministically") {
    const char malformed[] = {static_cast<char>(0xc3), '(', '\0'};
    CHECK(YonaStdStringLength(malformed) == 2);
    CHECK(YonaStdStringCharAt(malformed, 0) == 0xfffd);
    CHECK(YonaStdStringCharAt(malformed, 1) == '(');

    auto *scalars = YonaRuntimeSequenceAllocate(4);
    YonaRuntimeSequenceSet(scalars, 0, 0x61);
    YonaRuntimeSequenceSet(scalars, 1, 0x3b2);
    YonaRuntimeSequenceSet(scalars, 2, 0x1f600);
    YonaRuntimeSequenceSet(scalars, 3, 0x7a);
    const char *encoded = YonaStdStringFromChars(scalars);
    CHECK(std::strcmp(encoded, "aβ😀z") == 0);
    YonaRuntimeRelease((void *)encoded);
    YonaRuntimeRelease(scalars);
  }

  TEST_CASE("seq_alloc + set + get round-trip on small seq") {
    int64_t *s = YonaRuntimeSequenceAllocate(4);
    REQUIRE(s != nullptr);
    CHECK(YonaRuntimeSequenceLength(s) == 4);
    for (int i = 0; i < 4; i++)
      YonaRuntimeSequenceSet(s, i, (int64_t)(i * 10));
    for (int i = 0; i < 4; i++)
      CHECK(YonaRuntimeSequenceGet(s, i) == i * 10);
    YonaRuntimeRelease(s);
  }

  TEST_CASE("seq_alloc accepts medium sizes") {
    int64_t *s = YonaRuntimeSequenceAllocate(1024);
    REQUIRE(s != nullptr);
    CHECK(YonaRuntimeSequenceLength(s) == 1024);
    YonaRuntimeSequenceSet(s, 1023, 42);
    CHECK(YonaRuntimeSequenceGet(s, 1023) == 42);
    YonaRuntimeRelease(s);
  }

  TEST_CASE("shared flat sequence tails retain heap elements") {
    auto *removed = YonaRuntimeSequenceAllocate(0);
    auto *kept = YonaRuntimeSequenceAllocate(0);
    YonaRuntimeRetain(removed); // observer + source sequence
    YonaRuntimeRetain(kept);    // observer + source sequence

    auto *source = YonaRuntimeSequenceAllocate(2);
    YonaRuntimeSequenceSet(source, 0, (int64_t)(intptr_t)removed);
    YonaRuntimeSequenceSet(source, 1, (int64_t)(intptr_t)kept);
    YonaRuntimeSequenceSetHeap(source, 1);
    YonaRuntimeRetain(source); // force the persistent copy path

    auto *tail = YonaRuntimeSequenceTail(source);
    REQUIRE(tail != source);
    CHECK(runtime_rc_of(removed) == 2);
    CHECK(runtime_rc_of(kept) == 3); // observer + source + tail

    YonaRuntimeRelease(source);
    YonaRuntimeRelease(source);
    CHECK(runtime_rc_of(removed) == 1);
    CHECK(runtime_rc_of(kept) == 2);
    YonaRuntimeRelease(tail);
    CHECK(runtime_rc_of(kept) == 1);
    YonaRuntimeRelease(removed);
    YonaRuntimeRelease(kept);
  }

  TEST_CASE(
      "consuming a unique flat tail releases only its removed heap head") {
    auto *removed = YonaRuntimeSequenceAllocate(0);
    auto *kept = YonaRuntimeSequenceAllocate(0);
    YonaRuntimeRetain(removed); // observer + source sequence
    YonaRuntimeRetain(kept);    // observer + source sequence

    auto *source = YonaRuntimeSequenceAllocate(2);
    YonaRuntimeSequenceSet(source, 0, (int64_t)(intptr_t)removed);
    YonaRuntimeSequenceSet(source, 1, (int64_t)(intptr_t)kept);
    YonaRuntimeSequenceSetHeap(source, 1);

    auto *tail = YonaRuntimeSequenceConsumeTail(source);
    REQUIRE(tail == source);
    CHECK(runtime_rc_of(removed) == 1);
    CHECK(runtime_rc_of(kept) == 2);
    YonaRuntimeRelease(tail);
    CHECK(runtime_rc_of(kept) == 1);
    YonaRuntimeRelease(removed);
    YonaRuntimeRelease(kept);
  }

  TEST_CASE("persistent sequence transformations retain copied heap elements") {
    SUBCASE("cons") {
      auto *existing = YonaRuntimeSequenceAllocate(0);
      auto *added = YonaRuntimeSequenceAllocate(0);
      YonaRuntimeRetain(existing); // source ownership
      YonaRuntimeRetain(added);    // result ownership supplied by caller
      auto *source = YonaRuntimeSequenceAllocate(1);
      YonaRuntimeSequenceSet(source, 0, (int64_t)(intptr_t)existing);
      YonaRuntimeSequenceSetHeap(source, 1);

      auto *result =
          YonaRuntimeSequencePrepend((int64_t)(intptr_t)added, source);
      YonaRuntimeSequenceSetHeap(result, 1);
      CHECK(runtime_rc_of(existing) == 3);
      YonaRuntimeRelease(source);
      CHECK(runtime_rc_of(existing) == 2);
      YonaRuntimeRelease(result);
      CHECK(runtime_rc_of(existing) == 1);
      CHECK(runtime_rc_of(added) == 1);
      YonaRuntimeRelease(existing);
      YonaRuntimeRelease(added);
    }

    SUBCASE("snoc") {
      auto *existing = YonaRuntimeSequenceAllocate(0);
      auto *added = YonaRuntimeSequenceAllocate(0);
      YonaRuntimeRetain(existing);
      YonaRuntimeRetain(added);
      auto *source = YonaRuntimeSequenceAllocate(1);
      YonaRuntimeSequenceSet(source, 0, (int64_t)(intptr_t)existing);
      YonaRuntimeSequenceSetHeap(source, 1);

      auto *result =
          YonaRuntimeSequenceAppend(source, (int64_t)(intptr_t)added);
      YonaRuntimeSequenceSetHeap(result, 1);
      CHECK(runtime_rc_of(existing) == 3);
      YonaRuntimeRelease(source);
      YonaRuntimeRelease(result);
      CHECK(runtime_rc_of(existing) == 1);
      CHECK(runtime_rc_of(added) == 1);
      YonaRuntimeRelease(existing);
      YonaRuntimeRelease(added);
    }

    SUBCASE("join") {
      auto *left_value = YonaRuntimeSequenceAllocate(0);
      auto *right_value = YonaRuntimeSequenceAllocate(0);
      YonaRuntimeRetain(left_value);
      YonaRuntimeRetain(right_value);
      auto *left = YonaRuntimeSequenceAllocate(1);
      auto *right = YonaRuntimeSequenceAllocate(1);
      YonaRuntimeSequenceSet(left, 0, (int64_t)(intptr_t)left_value);
      YonaRuntimeSequenceSet(right, 0, (int64_t)(intptr_t)right_value);
      YonaRuntimeSequenceSetHeap(left, 1);
      YonaRuntimeSequenceSetHeap(right, 1);

      auto *result = YonaRuntimeSequenceJoin(left, right);
      CHECK(runtime_rc_of(left_value) == 3);
      CHECK(runtime_rc_of(right_value) == 3);
      YonaRuntimeRelease(left);
      YonaRuntimeRelease(right);
      YonaRuntimeRelease(result);
      CHECK(runtime_rc_of(left_value) == 1);
      CHECK(runtime_rc_of(right_value) == 1);
      YonaRuntimeRelease(left_value);
      YonaRuntimeRelease(right_value);
    }

    SUBCASE("difference") {
      auto *kept = YonaRuntimeSequenceAllocate(0);
      auto *removed = YonaRuntimeSequenceAllocate(0);
      YonaRuntimeRetain(kept);
      YonaRuntimeRetain(removed);
      auto *left = YonaRuntimeSequenceAllocate(2);
      auto *right = YonaRuntimeSequenceAllocate(1);
      YonaRuntimeSequenceSet(left, 0, (int64_t)(intptr_t)kept);
      YonaRuntimeSequenceSet(left, 1, (int64_t)(intptr_t)removed);
      YonaRuntimeSequenceSet(right, 0, (int64_t)(intptr_t)removed);
      YonaRuntimeSequenceSetHeap(left, 1);
      YonaRuntimeSequenceSetHeap(right, 1);
      YonaRuntimeRetain(removed); // right also owns the repeated pointer

      auto *result = YonaRuntimeSequenceDifference(left, right);
      CHECK(runtime_rc_of(kept) == 3);
      YonaRuntimeRelease(left);
      YonaRuntimeRelease(right);
      YonaRuntimeRelease(result);
      CHECK(runtime_rc_of(kept) == 1);
      CHECK(runtime_rc_of(removed) == 1);
      YonaRuntimeRelease(kept);
      YonaRuntimeRelease(removed);
    }
  }

  TEST_CASE("shared RBT tails retain direct and chunked heap elements") {
    auto *child = YonaRuntimeSequenceAllocate(0); // observer reference
    auto *flat = YonaRuntimeSequenceAllocate(40);
    for (int64_t i = 0; i < 40; ++i) {
      YonaRuntimeRetain(child); // one ownership reference per element slot
      YonaRuntimeSequenceSet(flat, i, (int64_t)(intptr_t)child);
    }
    YonaRuntimeSequenceSetHeap(flat, 1);

    auto *rbt = YonaRuntimeSequenceTail(flat); // 39 copied values
    REQUIRE(rbt != flat);
    CHECK(runtime_rc_of(child) == 80); // observer + flat(40) + rbt(39)
    YonaRuntimeRelease(flat);
    CHECK(runtime_rc_of(child) == 40);

    YonaRuntimeRetain(rbt); // force the shared RBT clone path
    auto *tail = YonaRuntimeSequenceTail(rbt);
    REQUIRE(tail != rbt);
    CHECK(runtime_rc_of(child) == 78); // observer + rbt(39) + tail(38)
    YonaRuntimeRelease(rbt);
    YonaRuntimeRelease(rbt);
    CHECK(runtime_rc_of(child) == 39);
    YonaRuntimeRelease(tail);
    CHECK(runtime_rc_of(child) == 1);
    YonaRuntimeRelease(child);
  }

  TEST_CASE("HTTP URL parser uses a mixed-field runtime value") {
    int64_t *parsed = YonaStdHttpParseUrl("https://example.com:8443/api");
    REQUIRE(parsed != nullptr);

    auto *host =
        reinterpret_cast<const char *>(YonaRuntimeAdtGetField(parsed, 0));
    CHECK(std::strcmp(host, "example.com") == 0);
    CHECK(YonaRuntimeAdtGetField(parsed, 1) == 8443);
    auto *path =
        reinterpret_cast<const char *>(YonaRuntimeAdtGetField(parsed, 2));
    CHECK(std::strcmp(path, "/api") == 0);

    YonaRuntimeRelease(parsed);
  }

  TEST_CASE("platform listDir stores every name after the sequence header") {
    int64_t *entries = YonaRuntimePlatformListDirectory(".");
    REQUIRE(entries != nullptr);
    const int64_t count = YonaRuntimeSequenceLength(entries);
    REQUIRE(count > 0);
    for (int64_t i = 0; i < count; ++i) {
      auto *name =
          reinterpret_cast<const char *>(YonaRuntimeSequenceGet(entries, i));
      REQUIRE(name != nullptr);
      CHECK(name[0] != '\0');
    }
    YonaRuntimeRelease(entries);
  }

} // TEST_SUITE("RuntimeGuards")
