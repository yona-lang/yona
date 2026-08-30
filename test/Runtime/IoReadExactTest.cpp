/*
 * Pipe-safe Std\Io.readExact: stream read(), not seek/pread.
 * Content-Length framing on stdin needs this.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#elif defined(__APPLE__)
#include "yona/Runtime/Platform/Kqueue.h"

#include <sys/socket.h>
#include <unistd.h>
#else
#include "yona/Runtime/Platform/IoUring.h"

#include <sys/socket.h>
#include <unistd.h>
#endif

extern "C" {
char *YonaStdIoReadExactBytes(int64_t fd, int64_t n);
int64_t YonaStdIoReadExact(int64_t fd, int64_t n);
void YonaStdIoWriteBytes(int64_t fd, const char *s);
int64_t YonaStdStringLength(const char *s);
void YonaRuntimeRelease(void *ptr);
}

namespace {

#if defined(_WIN32)
int make_pipe(int fds[2]) { return _pipe(fds, 4096, _O_BINARY); }
int pipe_write(int fd, const void *buf, unsigned n) {
  return (int)_write(fd, buf, n);
}
int pipe_close(int fd) { return _close(fd); }
#else
int make_pipe(int fds[2]) { return pipe(fds); }
int pipe_write(int fd, const void *buf, unsigned n) {
  return (int)write(fd, buf, n);
}
int pipe_close(int fd) { return close(fd); }
#endif

std::string take_rc(char *s) {
  REQUIRE(s != nullptr);
  std::string out(s, static_cast<size_t>(YonaStdStringLength(s)));
  YonaRuntimeRelease(s);
  return out;
}

} // namespace

TEST_SUITE("IoReadExact") {

  TEST_CASE("readExact reads exactly n bytes from a pipe") {
    int fds[2];
    REQUIRE(make_pipe(fds) == 0);
    REQUIRE(pipe_write(fds[1], "hello", 5) == 5);
    REQUIRE(pipe_close(fds[1]) == 0);
    CHECK(take_rc(YonaStdIoReadExactBytes(fds[0], 5)) == "hello");
    REQUIRE(pipe_close(fds[0]) == 0);
  }

  TEST_CASE("readExact loops across short stream reads") {
    int fds[2];
    REQUIRE(make_pipe(fds) == 0);
    REQUIRE(pipe_write(fds[1], "ab", 2) == 2);
    REQUIRE(pipe_write(fds[1], "cde", 3) == 3);
    REQUIRE(pipe_close(fds[1]) == 0);
    CHECK(take_rc(YonaStdIoReadExactBytes(fds[0], 5)) == "abcde");
    REQUIRE(pipe_close(fds[0]) == 0);
  }

  TEST_CASE("readExact returns a short string at unexpected EOF") {
    int fds[2];
    REQUIRE(make_pipe(fds) == 0);
    REQUIRE(pipe_write(fds[1], "xy", 2) == 2);
    REQUIRE(pipe_close(fds[1]) == 0);
    CHECK(take_rc(YonaStdIoReadExactBytes(fds[0], 8)) == "xy");
    REQUIRE(pipe_close(fds[0]) == 0);
  }

  TEST_CASE("readExact of zero bytes is empty") {
    int fds[2];
    REQUIRE(make_pipe(fds) == 0);
    REQUIRE(pipe_close(fds[1]) == 0);
    CHECK(take_rc(YonaStdIoReadExactBytes(fds[0], 0)) == "");
    REQUIRE(pipe_close(fds[0]) == 0);
  }

  TEST_CASE("readExact Result is Ok for a full pipe read") {
    int fds[2];
    REQUIRE(make_pipe(fds) == 0);
    REQUIRE(pipe_write(fds[1], "frame", 5) == 5);
    REQUIRE(pipe_close(fds[1]) == 0);
    int64_t r = YonaStdIoReadExact(fds[0], 5);
    REQUIRE(r != 0);
    int64_t *adt = reinterpret_cast<int64_t *>(static_cast<intptr_t>(r));
    CHECK(adt[0] == 0);
    auto *s = reinterpret_cast<char *>(static_cast<intptr_t>(adt[3]));
    CHECK(take_rc(s) == "frame");
    REQUIRE(pipe_close(fds[0]) == 0);
  }

  TEST_CASE("readExact Result is Err on short EOF") {
    int fds[2];
    REQUIRE(make_pipe(fds) == 0);
    REQUIRE(pipe_write(fds[1], "xy", 2) == 2);
    REQUIRE(pipe_close(fds[1]) == 0);
    int64_t r = YonaStdIoReadExact(fds[0], 8);
    REQUIRE(r != 0);
    int64_t *adt = reinterpret_cast<int64_t *>(static_cast<intptr_t>(r));
    CHECK(adt[0] == 1);
    REQUIRE(pipe_close(fds[0]) == 0);
  }

#if defined(_WIN32)
  TEST_CASE("readExact and writeBytes set CRT stdio to binary mode") {
    int old_in = _setmode(0, _O_TEXT);
    int old_out = _setmode(1, _O_TEXT);
    REQUIRE(old_in != -1);
    REQUIRE(old_out != -1);

    char *z = YonaStdIoReadExactBytes(0, 0);
    CHECK(take_rc(z) == "");
    CHECK(_setmode(0, _O_BINARY) == _O_BINARY);

    YonaStdIoWriteBytes(1, "");
    CHECK(_setmode(1, _O_BINARY) == _O_BINARY);

    _setmode(0, old_in);
    _setmode(1, old_out);
  }
#endif

#if !defined(_WIN32)
  TEST_CASE("readExact works on a socketpair (not a seekable file)") {
    int fds[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    REQUIRE(pipe_write(fds[1], "lsp!", 4) == 4);
    REQUIRE(pipe_close(fds[1]) == 0);
    CHECK(take_rc(YonaStdIoReadExactBytes(fds[0], 4)) == "lsp!");
    REQUIRE(pipe_close(fds[0]) == 0);
  }

  TEST_CASE("POSIX I/O registry preserves a colliding context after deletion") {
    YonaIoContext first{};
    YonaIoContext colliding{};
    constexpr uint64_t first_id = 17;
    constexpr uint64_t colliding_id = first_id + YONA_IO_CONTEXT_TABLE_SIZE;

    YonaRuntimeIoContextPut(first_id, &first);
    YonaRuntimeIoContextPut(colliding_id, &colliding);
    CHECK(YonaRuntimeIoContextTake(first_id) == &first);
    CHECK(YonaRuntimeIoContextTake(colliding_id) == &colliding);
  }
#endif

} // TEST_SUITE("IoReadExact")
