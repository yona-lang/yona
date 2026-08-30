#include <doctest/doctest.h>

#include "yona/Support/Terminal.h"

TEST_SUITE("Terminal") {

#if defined(_WIN32)
TEST_CASE("terminal size query rejects an invalid Windows handle") {
  CHECK(yona::terminal::detail::getTerminalSizeFromHandle(INVALID_HANDLE_VALUE) ==
        std::pair<std::size_t, std::size_t>{0, 0});
}
#endif

} // TEST_SUITE("Terminal")
