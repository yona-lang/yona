/*
 * Pipe-safe Std\Io.readExact: stream read(), not seek/pread.
 * Content-Length framing on stdin needs this.
 */

#include "yona/Runtime/Core/Api.h"
#include "yona/Support/Process.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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
char *YonaStdFileReadExactBytes(int64_t handle, int64_t n);
int64_t YonaStdFileReadExact(int64_t handle, int64_t n);
int64_t YonaStdFileCloseFileHandle(int64_t handle);
int64_t YonaStdFileWriteBytes(int64_t handle, int64_t bytes);
int64_t YonaRuntimeIoAwait(int64_t io_id);
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

int native_file_descriptor(std::FILE *file) {
#if defined(_WIN32)
  return _fileno(file);
#else
  return fileno(file);
#endif
}

int64_t runtime_reference_count(void *value) {
  return static_cast<int64_t *>(value)[-2];
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

  TEST_CASE("File readExactBytes extracts an unwrapped FileHandle") {
    int fds[2];
    REQUIRE(make_pipe(fds) == 0);
    REQUIRE(pipe_write(fds[1], "typed", 5) == 5);
    REQUIRE(pipe_close(fds[1]) == 0);
    int64_t handle[] = {0, 1, 0, fds[0]};
    CHECK(take_rc(YonaStdFileReadExactBytes(
              static_cast<int64_t>(reinterpret_cast<intptr_t>(handle)), 5)) ==
          "typed");
    REQUIRE(pipe_close(fds[0]) == 0);
  }

  TEST_CASE("File readExact reports short EOF through Result") {
    int fds[2];
    REQUIRE(make_pipe(fds) == 0);
    REQUIRE(pipe_write(fds[1], "xy", 2) == 2);
    REQUIRE(pipe_close(fds[1]) == 0);
    int64_t handle[] = {0, 1, 0, fds[0]};
    int64_t r = YonaStdFileReadExact(
        static_cast<int64_t>(reinterpret_cast<intptr_t>(handle)), 4);
    REQUIRE(r != 0);
    auto *adt = reinterpret_cast<int64_t *>(static_cast<intptr_t>(r));
    CHECK(adt[0] == 1);
    YonaRuntimeRelease(reinterpret_cast<void *>(static_cast<intptr_t>(r)));
    REQUIRE(pipe_close(fds[0]) == 0);
  }

#if defined(__linux__)
  TEST_CASE("closeFileHandle releases its transferred FileHandle") {
    constexpr const char *ChildEnvironment =
        "YONA_TEST_FILE_CLOSE_OWNERSHIP_CHILD";
    if (std::getenv(ChildEnvironment)) {
      int descriptors[2];
      REQUIRE(make_pipe(descriptors) == 0);
      auto *handle = static_cast<int64_t *>(YonaRuntimeAdtAllocate(0, 1));
      REQUIRE(handle != nullptr);
      YonaRuntimeAdtSetField(handle, 0, descriptors[0]);
      YonaRuntimeAdtSetHeapMask(handle, 0);

      CHECK(YonaStdFileCloseFileHandle(
                static_cast<int64_t>(reinterpret_cast<intptr_t>(handle))) == 0);
      CHECK(pipe_close(descriptors[0]) == -1);
      REQUIRE(pipe_close(descriptors[1]) == 0);
      return;
    }

    const auto executable = std::filesystem::canonical("/proc/self/exe");
    const auto result = yona::support::executeProcess(
        executable, {"-tc=closeFileHandle releases its transferred FileHandle"},
        {.CaptureStdout = true,
         .CaptureStderr = true,
         .EnvironmentOverrides = {{ChildEnvironment, "1"},
                                  {"YONA_ALLOC_STATS", "1"}}});
    REQUIRE_FALSE(result.ExecutionFailed);
    REQUIRE(result.ExitCode == 0);
    INFO(result.StandardError);
    CHECK(result.StandardError.find("tag=ADT allocs=1 frees=1 leaked=0") !=
          std::string::npos);
  }

  TEST_CASE("Length-tagged allocation registers its stats reporter") {
    constexpr const char *ChildEnvironment =
        "YONA_TEST_LENGTH_TAGGED_STATS_CHILD";
    if (std::getenv(ChildEnvironment)) {
      auto *value =
          static_cast<char *>(YonaRuntimeAllocateStringWithLength(1, 0));
      REQUIRE(value != nullptr);
      value[0] = '\0';
      YonaRuntimeRelease(value);
      return;
    }

    const auto executable = std::filesystem::canonical("/proc/self/exe");
    const auto result = yona::support::executeProcess(
        executable,
        {"-tc=Length-tagged allocation registers its stats reporter"},
        {.CaptureStdout = true,
         .CaptureStderr = true,
         .EnvironmentOverrides = {{ChildEnvironment, "1"},
                                  {"YONA_ALLOC_STATS", "1"}}});
    REQUIRE_FALSE(result.ExecutionFailed);
    REQUIRE(result.ExitCode == 0);
    INFO(result.StandardError);
    CHECK(result.StandardError.find("[alloc-stats] allocs=1 frees=1") !=
          std::string::npos);
    CHECK(result.StandardError.find("tag=STRING allocs=1 frees=1 leaked=0") !=
          std::string::npos);
  }

  TEST_CASE("File readExact releases its discarded short string") {
    constexpr const char *ChildEnvironment =
        "YONA_TEST_FILE_READ_EXACT_LEAK_CHILD";
    if (std::getenv(ChildEnvironment)) {
      int fds[2];
      REQUIRE(make_pipe(fds) == 0);
      REQUIRE(pipe_write(fds[1], "xy", 2) == 2);
      REQUIRE(pipe_close(fds[1]) == 0);
      int64_t handle[] = {0, 1, 0, fds[0]};
      int64_t result = YonaStdFileReadExact(
          static_cast<int64_t>(reinterpret_cast<intptr_t>(handle)), 4);
      REQUIRE(result != 0);
      YonaRuntimeRelease(
          reinterpret_cast<void *>(static_cast<intptr_t>(result)));
      REQUIRE(pipe_close(fds[0]) == 0);
      return;
    }

    const auto executable = std::filesystem::canonical("/proc/self/exe");
    const auto result = yona::support::executeProcess(
        executable, {"-tc=File readExact releases its discarded short string"},
        {.CaptureStdout = true,
         .CaptureStderr = true,
         .EnvironmentOverrides = {{ChildEnvironment, "1"},
                                  {"YONA_ALLOC_STATS", "1"}}});
    REQUIRE_FALSE(result.ExecutionFailed);
    REQUIRE(result.ExitCode == 0);
    INFO(result.StandardError);
    CHECK(result.StandardError.find("tag=STRING allocs=2 frees=2 leaked=0") !=
          std::string::npos);
  }
#endif

  TEST_CASE("writeBytes keeps its asynchronous ByteArray pin internal") {
    std::FILE *file = std::tmpfile();
    REQUIRE(file != nullptr);
    const int file_descriptor = native_file_descriptor(file);
    REQUIRE(file_descriptor >= 0);
    int64_t handle[] = {0, 1, 0, file_descriptor};
    void *bytes = YonaRuntimeByteArrayFromString("abc");
    REQUIRE(bytes != nullptr);
    REQUIRE(runtime_reference_count(bytes) == 1);

    const int64_t io_id = YonaStdFileWriteBytes(
        static_cast<int64_t>(reinterpret_cast<intptr_t>(handle)),
        static_cast<int64_t>(reinterpret_cast<intptr_t>(bytes)));
    REQUIRE(io_id > 0);
    CHECK(YonaRuntimeIoAwait(io_id) == 3);
    CHECK(runtime_reference_count(bytes) == 1);

    YonaRuntimeRelease(bytes);
    CHECK(std::fclose(file) == 0);
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
