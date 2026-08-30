#include "yona/Runtime/Concurrency/Async.h"

#include <doctest/doctest.h>

#include <atomic>
#include <cstdint>

namespace {

struct TrackedPayload {
  std::atomic<int> ReferenceCount{1};
  int64_t Value;
};

std::atomic<int> RetainCount{0};
std::atomic<int> ReleaseCount{0};
std::atomic<int> DestroyCount{0};
std::atomic<int> InvocationCount{0};

void retainTracked(int64_t Value) {
  auto *Payload = reinterpret_cast<TrackedPayload *>(Value);
  Payload->ReferenceCount.fetch_add(1, std::memory_order_relaxed);
  RetainCount.fetch_add(1, std::memory_order_relaxed);
}

void releaseTracked(int64_t Value) {
  auto *Payload = reinterpret_cast<TrackedPayload *>(Value);
  ReleaseCount.fetch_add(1, std::memory_order_relaxed);
  if (Payload->ReferenceCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    DestroyCount.fetch_add(1, std::memory_order_relaxed);
    delete Payload;
  }
}

const YonaTypeDescriptor TrackedDescriptor = {retainTracked, releaseTracked, 1};

void resetCounts() {
  RetainCount.store(0, std::memory_order_relaxed);
  ReleaseCount.store(0, std::memory_order_relaxed);
  DestroyCount.store(0, std::memory_order_relaxed);
  InvocationCount.store(0, std::memory_order_relaxed);
}

TrackedPayload *makePayload(int64_t Value) {
  return new TrackedPayload{{1}, Value};
}

int64_t makePayloadAsync(int64_t Value) {
  InvocationCount.fetch_add(1, std::memory_order_relaxed);
  return reinterpret_cast<int64_t>(makePayload(Value));
}

struct PayloadContext {
  TrackedPayload *Payload;
};

int64_t returnContextPayload(int64_t ContextValue) {
  InvocationCount.fetch_add(1, std::memory_order_relaxed);
  const auto *Context = reinterpret_cast<const PayloadContext *>(ContextValue);
  return reinterpret_cast<int64_t>(Context->Payload);
}

} // namespace

TEST_CASE("task await transfers its heap result") {
  resetCounts();
  auto *Payload = makePayload(42);
  YonaTaskRef Task = YonaRuntimeTaskCreate(&TrackedDescriptor);
  REQUIRE(Task != nullptr);

  YonaRuntimeTaskComplete(Task, reinterpret_cast<int64_t>(Payload), 0, nullptr);
  auto *Result = reinterpret_cast<TrackedPayload *>(YonaRuntimeTaskAwait(Task));

  REQUIRE(Result == Payload);
  CHECK(Result->Value == 42);
  CHECK(RetainCount.load(std::memory_order_relaxed) == 0);
  CHECK(ReleaseCount.load(std::memory_order_relaxed) == 0);
  YonaRuntimeTypeDescriptorRelease(&TrackedDescriptor,
                                   reinterpret_cast<int64_t>(Result));
  CHECK(ReleaseCount.load(std::memory_order_relaxed) == 1);
  CHECK(DestroyCount.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("task awaitKeep retains independently of group ownership") {
  resetCounts();
  auto *Payload = makePayload(7);
  YonaTaskGroupRef Group = YonaRuntimeTaskGroupBegin();
  YonaTaskRef Task = YonaRuntimeTaskCreate(&TrackedDescriptor);
  REQUIRE(Group != nullptr);
  REQUIRE(Task != nullptr);
  REQUIRE(YonaRuntimeTaskGroupRegister(Group, Task) == 1);
  YonaRuntimeTaskComplete(Task, reinterpret_cast<int64_t>(Payload), 0, Group);

  auto *Result =
      reinterpret_cast<TrackedPayload *>(YonaRuntimeTaskAwaitKeep(Task));
  CHECK(Result == Payload);
  CHECK(RetainCount.load(std::memory_order_relaxed) == 1);

  YonaRuntimeTaskGroupEnd(Group);
  CHECK(ReleaseCount.load(std::memory_order_relaxed) == 1);
  CHECK(DestroyCount.load(std::memory_order_relaxed) == 0);
  YonaRuntimeTypeDescriptorRelease(&TrackedDescriptor,
                                   reinterpret_cast<int64_t>(Result));
  CHECK(ReleaseCount.load(std::memory_order_relaxed) == 2);
  CHECK(DestroyCount.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("task error and repeated completion release every owned payload") {
  resetCounts();
  auto *ErrorPayload = makePayload(-1);
  auto *RejectedPayload = makePayload(-2);
  YonaTaskGroupRef Group = YonaRuntimeTaskGroupBegin();
  YonaTaskRef Task = YonaRuntimeTaskCreate(&TrackedDescriptor);
  REQUIRE(Group != nullptr);
  REQUIRE(Task != nullptr);
  REQUIRE(YonaRuntimeTaskGroupRegister(Group, Task) == 1);

  YonaRuntimeTaskComplete(Task, reinterpret_cast<int64_t>(ErrorPayload), 1,
                          Group);
  YonaRuntimeTaskComplete(Task, reinterpret_cast<int64_t>(RejectedPayload), 1,
                          Group);
  CHECK(ReleaseCount.load(std::memory_order_relaxed) == 1);
  CHECK(DestroyCount.load(std::memory_order_relaxed) == 1);

  YonaRuntimeTaskGroupEnd(Group);
  CHECK(ReleaseCount.load(std::memory_order_relaxed) == 2);
  CHECK(DestroyCount.load(std::memory_order_relaxed) == 2);
}

TEST_CASE("cancelled grouped work does not manufacture a heap result") {
  resetCounts();
  YonaTaskGroupRef Group = YonaRuntimeTaskGroupBegin();
  REQUIRE(Group != nullptr);
  YonaRuntimeTaskGroupCancel(Group);
  YonaTaskRef Task = YonaRuntimeAsyncCallGrouped(makePayloadAsync, 99,
                                                 &TrackedDescriptor, Group);
  REQUIRE(Task != nullptr);

  CHECK(YonaRuntimeTaskGroupAwaitAll(Group) == 0);
  CHECK(YonaRuntimeTaskAwaitKeep(Task) == 0);
  YonaRuntimeTaskGroupEnd(Group);
  CHECK(InvocationCount.load(std::memory_order_relaxed) == 0);
  CHECK(RetainCount.load(std::memory_order_relaxed) == 0);
  CHECK(ReleaseCount.load(std::memory_order_relaxed) == 0);
  CHECK(DestroyCount.load(std::memory_order_relaxed) == 0);
}

TEST_CASE("pooled call and owned context transfer heap results") {
  resetCounts();
  YonaTaskRef Direct =
      YonaRuntimeAsyncCall(makePayloadAsync, 11, &TrackedDescriptor);
  REQUIRE(Direct != nullptr);
  auto *DirectResult =
      reinterpret_cast<TrackedPayload *>(YonaRuntimeTaskAwait(Direct));
  REQUIRE(DirectResult != nullptr);
  CHECK(DirectResult->Value == 11);
  YonaRuntimeTypeDescriptorRelease(&TrackedDescriptor,
                                   reinterpret_cast<int64_t>(DirectResult));

  auto *Context = static_cast<PayloadContext *>(
      YonaRuntimeAsyncContextAllocate(sizeof(PayloadContext)));
  REQUIRE(Context != nullptr);
  Context->Payload = makePayload(23);
  YonaTaskRef ContextTask = YonaRuntimeAsyncCallContext(
      returnContextPayload, Context, &TrackedDescriptor);
  REQUIRE(ContextTask != nullptr);
  auto *ContextResult =
      reinterpret_cast<TrackedPayload *>(YonaRuntimeTaskAwait(ContextTask));
  REQUIRE(ContextResult != nullptr);
  CHECK(ContextResult->Value == 23);
  YonaRuntimeTypeDescriptorRelease(&TrackedDescriptor,
                                   reinterpret_cast<int64_t>(ContextResult));

  CHECK(InvocationCount.load(std::memory_order_relaxed) == 2);
  CHECK(RetainCount.load(std::memory_order_relaxed) == 0);
  CHECK(ReleaseCount.load(std::memory_order_relaxed) == 2);
  CHECK(DestroyCount.load(std::memory_order_relaxed) == 2);
}
