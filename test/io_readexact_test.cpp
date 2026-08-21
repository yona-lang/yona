/*
 * Pipe-safe Std\IO.readExact: stream read(), not seek/pread.
 * Content-Length framing on stdin needs this.
 */

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

extern "C" {
char* yona_Std_IO__readExactBytes(int64_t fd, int64_t n);
int64_t yona_Std_IO__readExact(int64_t fd, int64_t n);
int64_t yona_Std_String__length(const char* s);
void yona_rt_rc_dec(void* ptr);
}

namespace {

#if defined(_WIN32)
int make_pipe(int fds[2]) { return _pipe(fds, 4096, _O_BINARY); }
int pipe_write(int fd, const void* buf, unsigned n) { return (int)_write(fd, buf, n); }
int pipe_close(int fd) { return _close(fd); }
#else
int make_pipe(int fds[2]) { return pipe(fds); }
int pipe_write(int fd, const void* buf, unsigned n) { return (int)write(fd, buf, n); }
int pipe_close(int fd) { return close(fd); }
#endif

std::string take_rc(char* s) {
    REQUIRE(s != nullptr);
    std::string out(s, static_cast<size_t>(yona_Std_String__length(s)));
    yona_rt_rc_dec(s);
    return out;
}

} // namespace

TEST_SUITE("IoReadExact") {

TEST_CASE("readExact reads exactly n bytes from a pipe") {
    int fds[2];
    REQUIRE(make_pipe(fds) == 0);
    REQUIRE(pipe_write(fds[1], "hello", 5) == 5);
    REQUIRE(pipe_close(fds[1]) == 0);
    CHECK(take_rc(yona_Std_IO__readExactBytes(fds[0], 5)) == "hello");
    REQUIRE(pipe_close(fds[0]) == 0);
}

TEST_CASE("readExact loops across short stream reads") {
    int fds[2];
    REQUIRE(make_pipe(fds) == 0);
    REQUIRE(pipe_write(fds[1], "ab", 2) == 2);
    REQUIRE(pipe_write(fds[1], "cde", 3) == 3);
    REQUIRE(pipe_close(fds[1]) == 0);
    CHECK(take_rc(yona_Std_IO__readExactBytes(fds[0], 5)) == "abcde");
    REQUIRE(pipe_close(fds[0]) == 0);
}

TEST_CASE("readExact returns a short string at unexpected EOF") {
    int fds[2];
    REQUIRE(make_pipe(fds) == 0);
    REQUIRE(pipe_write(fds[1], "xy", 2) == 2);
    REQUIRE(pipe_close(fds[1]) == 0);
    CHECK(take_rc(yona_Std_IO__readExactBytes(fds[0], 8)) == "xy");
    REQUIRE(pipe_close(fds[0]) == 0);
}

TEST_CASE("readExact of zero bytes is empty") {
    int fds[2];
    REQUIRE(make_pipe(fds) == 0);
    REQUIRE(pipe_close(fds[1]) == 0);
    CHECK(take_rc(yona_Std_IO__readExactBytes(fds[0], 0)) == "");
    REQUIRE(pipe_close(fds[0]) == 0);
}

TEST_CASE("readExact Result is Ok for a full pipe read") {
    int fds[2];
    REQUIRE(make_pipe(fds) == 0);
    REQUIRE(pipe_write(fds[1], "frame", 5) == 5);
    REQUIRE(pipe_close(fds[1]) == 0);
    int64_t r = yona_Std_IO__readExact(fds[0], 5);
    REQUIRE(r != 0);
    int64_t* adt = reinterpret_cast<int64_t*>(static_cast<intptr_t>(r));
    CHECK(adt[0] == 0);
    auto* s = reinterpret_cast<char*>(static_cast<intptr_t>(adt[3]));
    CHECK(take_rc(s) == "frame");
    REQUIRE(pipe_close(fds[0]) == 0);
}

TEST_CASE("readExact Result is Err on short EOF") {
    int fds[2];
    REQUIRE(make_pipe(fds) == 0);
    REQUIRE(pipe_write(fds[1], "xy", 2) == 2);
    REQUIRE(pipe_close(fds[1]) == 0);
    int64_t r = yona_Std_IO__readExact(fds[0], 8);
    REQUIRE(r != 0);
    int64_t* adt = reinterpret_cast<int64_t*>(static_cast<intptr_t>(r));
    CHECK(adt[0] == 1);
    REQUIRE(pipe_close(fds[0]) == 0);
}

#if !defined(_WIN32)
TEST_CASE("readExact works on a socketpair (not a seekable file)") {
    int fds[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    REQUIRE(pipe_write(fds[1], "lsp!", 4) == 4);
    REQUIRE(pipe_close(fds[1]) == 0);
    CHECK(take_rc(yona_Std_IO__readExactBytes(fds[0], 4)) == "lsp!");
    REQUIRE(pipe_close(fds[0]) == 0);
}
#endif

} // TEST_SUITE("IoReadExact")
