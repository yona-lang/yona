#include "yona/Support/ThreadPool.h"

#include <doctest/doctest.h>

#include <atomic>

TEST_SUITE("ThreadPool") {

  TEST_CASE("void async tasks fulfill their future") {
    yona::runtime::async::ThreadPool Pool(1);
    std::atomic<bool> Ran = false;

    auto Result = Pool.submit_async<void>([&Ran] { Ran.store(true); });

    CHECK_NOTHROW(Result.get());
    CHECK(Ran.load());
  }

} // TEST_SUITE("ThreadPool")
