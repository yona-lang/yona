/*
 * Reproduce the Linux io_uring-unavailable byte-I/O fallback paths.
 *
 * The seccomp filter makes io_uring_setup fail with EPERM before the runtime
 * initializes its ring.  Both submitters must therefore use their synchronous
 * direct-result paths and release every transient context they create.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "yona/Runtime/Collections/Arrays.h"
#include "yona/Runtime/Core/Api.h"
#include "yona/Runtime/Platform/Api.h"

#include <errno.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

static int installIoUringSetupBlocker(void) {
  struct sock_filter Filter[] = {
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_io_uring_setup, 0, 1),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
  };
  struct sock_fprog Program = {
      .len = (unsigned short)(sizeof(Filter) / sizeof(Filter[0])),
      .filter = Filter,
  };

  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
    return -1;
  return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &Program);
}

int main(void) {
  char Path[] = "/tmp/yona-io-fallback-leak-XXXXXX";
  int Fd = mkstemp(Path);
  if (Fd < 0)
    return 1;

  const char Initial[] = "read fallback payload";
  if (write(Fd, Initial, sizeof(Initial) - 1) !=
      (ssize_t)(sizeof(Initial) - 1)) {
    close(Fd);
    unlink(Path);
    return 2;
  }

  if (installIoUringSetupBlocker() != 0) {
    close(Fd);
    unlink(Path);
    return 3;
  }

  int64_t ReadId =
      YonaRuntimePlatformSubmitFileDescriptorByteRead(Fd, 32, 0);
  int64_t *ReadBytes = (int64_t *)(intptr_t)YonaRuntimeIoAwait(ReadId);
  if (!ReadBytes || ReadBytes[0] != (int64_t)(sizeof(Initial) - 1) ||
      memcmp(ReadBytes + 1, Initial, sizeof(Initial) - 1) != 0) {
    YonaRuntimeRelease(ReadBytes);
    close(Fd);
    unlink(Path);
    return 4;
  }
  YonaRuntimeRelease(ReadBytes);

  void *WriteBytes = YonaRuntimeByteArrayFromString("write fallback payload");
  int64_t WriteId =
      YonaRuntimePlatformSubmitFileDescriptorByteWrite(Fd, WriteBytes, 0);
  int64_t WriteResult = YonaRuntimeIoAwait(WriteId);
  YonaRuntimeRelease(WriteBytes);
  if (WriteResult != (int64_t)strlen("write fallback payload")) {
    close(Fd);
    unlink(Path);
    return 5;
  }

  close(Fd);
  unlink(Path);
  return 0;
}
