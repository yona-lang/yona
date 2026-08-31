/*
 * macOS OS operations — console I/O, process, environment.
 *
 * Same POSIX process ABI as Linux. Pipe reads are blocking (AFN / thread
 * pool); kqueue is used for file/net submit paths, not process pipes.
 */

#ifndef __APPLE__
#error "os_macos.c is for macOS builds only"
#endif

#include "yona/Runtime/Collections/Sequence.h"
#include "yona/Runtime/Core/Api.h"
#include "yona/Runtime/Platform/Api.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <unistd.h>

#define YONA_RC_TYPE_PROCESS 17

/* ===== Process handle ===== */

typedef struct {
  int64_t ProcessId;
  int StandardInputFileDescriptor;  /* write end of stdin pipe (-1 if closed) */
  int StandardOutputFileDescriptor; /* read end of stdout pipe (-1 if closed) */
  int StandardErrorFileDescriptor;  /* read end of stderr pipe (-1 if closed) */
  int Exited;                       /* 1 if waitpid has been called */
  int ExitCode;                     /* exit status (valid if exited==1) */
} YonaProcess;

static char **buildPosixArgumentVector(const char *ArgumentZero,
                                       int64_t *ArgumentSequence) {
  int64_t ArgumentCount =
      ArgumentSequence ? YonaRuntimeSequenceLength(ArgumentSequence) : 0;
  if (ArgumentCount < 0)
    ArgumentCount = 0;
  char **ArgumentVector =
      (char **)calloc((size_t)ArgumentCount + 2, sizeof(char *));
  if (!ArgumentVector)
    return NULL;
  ArgumentVector[0] = (char *)ArgumentZero;
  for (int64_t Index = 0; Index < ArgumentCount; Index++) {
    ArgumentVector[Index + 1] =
        (char *)(intptr_t)YonaRuntimeSequenceGet(ArgumentSequence, Index);
  }
  return ArgumentVector;
}

/* ===== Console I/O ===== */

char *YonaRuntimePlatformReadLine(void) {
  char Buffer[4096];
  if (!fgets(Buffer, sizeof(Buffer), stdin)) {
    char *Result = (char *)YonaRuntimeAllocateString(1);
    Result[0] = '\0';
    return Result;
  }
  size_t Length = strlen(Buffer);
  if (Length > 0 && Buffer[Length - 1] == '\n')
    Buffer[--Length] = '\0';
  if (Length > 0 && Buffer[Length - 1] == '\r')
    Buffer[--Length] = '\0';
  char *Result = (char *)YonaRuntimeAllocateString(Length + 1);
  memcpy(Result, Buffer, Length + 1);
  return Result;
}

/* ===== Environment ===== */

char *YonaRuntimePlatformGetEnvironment(const char *Name) {
  const char *EnvironmentValue = getenv(Name);
  if (!EnvironmentValue)
    EnvironmentValue = "";
  size_t Length = strlen(EnvironmentValue);
  char *Result = (char *)YonaRuntimeAllocateString(Length + 1);
  memcpy(Result, EnvironmentValue, Length + 1);
  return Result;
}

char *YonaRuntimePlatformGetCurrentWorkingDirectory(void) {
  char Buffer[4096];
  if (!getcwd(Buffer, sizeof(Buffer)))
    Buffer[0] = '\0';
  size_t Length = strlen(Buffer);
  char *Result = (char *)YonaRuntimeAllocateString(Length + 1);
  memcpy(Result, Buffer, Length + 1);
  return Result;
}

int64_t YonaRuntimePlatformSetEnvironment(const char *Name, const char *Value) {
  return setenv(Name, Value, 1) == 0 ? 1 : 0;
}

char *YonaRuntimePlatformHostName(void) {
  char Buffer[256];
  if (gethostname(Buffer, sizeof(Buffer)) != 0)
    Buffer[0] = '\0';
  size_t Length = strlen(Buffer);
  char *Result = (char *)YonaRuntimeAllocateString(Length + 1);
  memcpy(Result, Buffer, Length + 1);
  return Result;
}

/* ===== Shell-free execution (blocking, exposed as AFN) ===== */

char *YonaRuntimePlatformExecute(const char *Executable,
                                 int64_t *ArgumentSequence) {
  void *Process = YonaStdProcessSpawn(Executable, ArgumentSequence);
  if (!Process) {
    char *Result = (char *)YonaRuntimeAllocateString(1);
    Result[0] = '\0';
    return Result;
  }
  (void)YonaStdProcessCloseStdin(Process);
  char *Result = YonaStdProcessReadAll(Process);
  (void)YonaStdProcessWait(Process);
  YonaRuntimeRelease(Process);
  return Result;
}

int64_t YonaRuntimePlatformExecuteStatus(const char *Executable,
                                         int64_t *ArgumentSequence) {
  return YonaStdProcessRun(Executable, ArgumentSequence);
}

int64_t YonaRuntimePlatformExitProcess(int64_t Code) {
  exit((int)Code);
  return 0; /* unreachable */
}

/* ===== spawn: fork/exec with pipe setup ===== */

void *YonaStdProcessSpawn(const char *Executable, int64_t *ArgumentSequence) {
  if (!Executable || !Executable[0])
    return NULL;
  char **ArgumentVector =
      buildPosixArgumentVector(Executable, ArgumentSequence);
  if (!ArgumentVector)
    return NULL;
  int StandardInputPipe[2], StandardOutputPipe[2], StandardErrorPipe[2];
  if (pipe(StandardInputPipe) < 0) {
    free(ArgumentVector);
    return NULL;
  }
  if (pipe(StandardOutputPipe) < 0) {
    close(StandardInputPipe[0]);
    close(StandardInputPipe[1]);
    free(ArgumentVector);
    return NULL;
  }
  if (pipe(StandardErrorPipe) < 0) {
    close(StandardInputPipe[0]);
    close(StandardInputPipe[1]);
    close(StandardOutputPipe[0]);
    close(StandardOutputPipe[1]);
    free(ArgumentVector);
    return NULL;
  }

  pid_t ProcessId = fork();
  if (ProcessId < 0) {
    close(StandardInputPipe[0]);
    close(StandardInputPipe[1]);
    close(StandardOutputPipe[0]);
    close(StandardOutputPipe[1]);
    close(StandardErrorPipe[0]);
    close(StandardErrorPipe[1]);
    free(ArgumentVector);
    return NULL;
  }

  if (ProcessId == 0) {
    /* Child: wire up pipes */
    close(StandardInputPipe[1]);  /* close write end of stdin */
    close(StandardOutputPipe[0]); /* close read end of stdout */
    close(StandardErrorPipe[0]);  /* close read end of stderr */
    dup2(StandardInputPipe[0], STDIN_FILENO);
    dup2(StandardOutputPipe[1], STDOUT_FILENO);
    dup2(StandardErrorPipe[1], STDERR_FILENO);
    close(StandardInputPipe[0]);
    close(StandardOutputPipe[1]);
    close(StandardErrorPipe[1]);
    execvp(Executable, ArgumentVector);
    _exit(127); /* exec failed */
  }

  free(ArgumentVector);

  /* Parent: close child ends */
  close(StandardInputPipe[0]);
  close(StandardOutputPipe[1]);
  close(StandardErrorPipe[1]);

  fcntl(StandardOutputPipe[0], F_SETFL, O_NONBLOCK);
  fcntl(StandardErrorPipe[0], F_SETFL, O_NONBLOCK);

  YonaProcess *Process = (YonaProcess *)YonaRuntimeAllocate(
      YONA_RC_TYPE_PROCESS, sizeof(YonaProcess));
  Process->ProcessId = (int64_t)ProcessId;
  Process->StandardInputFileDescriptor = StandardInputPipe[1];
  Process->StandardOutputFileDescriptor = StandardOutputPipe[0];
  Process->StandardErrorFileDescriptor = StandardErrorPipe[0];
  Process->Exited = 0;
  Process->ExitCode = -1;
  return Process;
}

/* ===== readLine: read one line from subprocess stdout ===== */

char *YonaStdProcessReadLine(void *ProcessHandle) {
  if (!ProcessHandle) {
    char *Result = (char *)YonaRuntimeAllocateString(1);
    Result[0] = '\0';
    return Result;
  }
  YonaProcess *Process = (YonaProcess *)ProcessHandle;
  if (Process->StandardOutputFileDescriptor < 0) {
    char *Result = (char *)YonaRuntimeAllocateString(1);
    Result[0] = '\0';
    return Result;
  }

  /* Read one byte at a time until newline or EOF.
   * Use blocking read (remove O_NONBLOCK temporarily). */
  int Flags = fcntl(Process->StandardOutputFileDescriptor, F_GETFL);
  fcntl(Process->StandardOutputFileDescriptor, F_SETFL, Flags & ~O_NONBLOCK);

  size_t Capacity = 256, Length = 0;
  char *Buffer = (char *)malloc(Capacity);
  while (1) {
    char Character;
    ssize_t Count = read(Process->StandardOutputFileDescriptor, &Character, 1);
    if (Count <= 0)
      break; /* EOF or error */
    if (Character == '\n')
      break;
    if (Length >= Capacity - 1) {
      Capacity *= 2;
      Buffer = (char *)realloc(Buffer, Capacity);
    }
    Buffer[Length++] = Character;
  }

  /* Restore non-blocking */
  fcntl(Process->StandardOutputFileDescriptor, F_SETFL, Flags);

  if (Length > 0 && Buffer[Length - 1] == '\r')
    Length--;
  char *Result = (char *)YonaRuntimeAllocateString(Length + 1);
  memcpy(Result, Buffer, Length);
  Result[Length] = '\0';
  free(Buffer);
  return Result;
}

/* ===== readAll: read all remaining stdout ===== */

char *YonaStdProcessReadAll(void *ProcessHandle) {
  if (!ProcessHandle)
    return 0;
  YonaProcess *Process = (YonaProcess *)ProcessHandle;
  if (Process->StandardOutputFileDescriptor < 0)
    return 0;

  /* Blocking read of remaining stdout (AFN / thread pool). */
  int FileDescriptor = Process->StandardOutputFileDescriptor;
  /* Make blocking for the read */
  int Flags = fcntl(FileDescriptor, F_GETFL);
  fcntl(FileDescriptor, F_SETFL, Flags & ~O_NONBLOCK);

  size_t Capacity = 4096, Length = 0;
  char *Buffer = (char *)malloc(Capacity);
  while (1) {
    ssize_t Count = read(FileDescriptor, Buffer + Length, Capacity - Length);
    if (Count <= 0)
      break;
    Length += Count;
    if (Length >= Capacity) {
      Capacity *= 2;
      Buffer = (char *)realloc(Buffer, Capacity);
    }
  }
  fcntl(FileDescriptor, F_SETFL, Flags);

  if (Length > 0 && Buffer[Length - 1] == '\n')
    Length--;
  if (Length > 0 && Buffer[Length - 1] == '\r')
    Length--;
  char *Result = (char *)YonaRuntimeAllocateString(Length + 1);
  memcpy(Result, Buffer, Length);
  Result[Length] = '\0';
  free(Buffer);
  return Result;
}

/* ===== wait: wait for subprocess to exit, return exit code ===== */

int64_t YonaStdProcessWait(void *ProcessHandle) {
  if (!ProcessHandle)
    return -1;
  YonaProcess *Process = (YonaProcess *)ProcessHandle;
  if (Process->Exited)
    return Process->ExitCode;

  int Status;
  pid_t WaitResult = waitpid((pid_t)Process->ProcessId, &Status, 0);
  if (WaitResult < 0)
    return -1;

  Process->Exited = 1;
  if (WIFEXITED(Status))
    Process->ExitCode = WEXITSTATUS(Status);
  else if (WIFSIGNALED(Status))
    Process->ExitCode = -WTERMSIG(Status);
  else
    Process->ExitCode = -1;

  return Process->ExitCode;
}

/* ===== kill: send signal to subprocess ===== */

int64_t YonaStdProcessKill(void *ProcessHandle, int64_t Signal) {
  if (!ProcessHandle)
    return -1;
  YonaProcess *Process = (YonaProcess *)ProcessHandle;
  return kill((pid_t)Process->ProcessId, (int)Signal) == 0 ? 1 : 0;
}

/* ===== writeStdin: write data to subprocess stdin pipe ===== */

int64_t YonaStdProcessWriteStdin(void *ProcessHandle, const char *Data) {
  if (!ProcessHandle || !Data)
    return 0;
  YonaProcess *Process = (YonaProcess *)ProcessHandle;
  if (Process->StandardInputFileDescriptor < 0)
    return 0;

  size_t Length = strlen(Data);
  ssize_t Written = write(Process->StandardInputFileDescriptor, Data, Length);
  return (Written == (ssize_t)Length) ? 1 : 0;
}

/* ===== closeStdin: close subprocess stdin pipe (signal EOF) ===== */

int64_t YonaStdProcessCloseStdin(void *ProcessHandle) {
  if (!ProcessHandle)
    return 0;
  YonaProcess *Process = (YonaProcess *)ProcessHandle;
  if (Process->StandardInputFileDescriptor >= 0) {
    close(Process->StandardInputFileDescriptor);
    Process->StandardInputFileDescriptor = -1;
  }
  return 1;
}

/* ===== pid: get subprocess PID ===== */

int64_t YonaStdProcessPid(void *ProcessHandle) {
  if (!ProcessHandle)
    return -1;
  return ((YonaProcess *)ProcessHandle)->ProcessId;
}

/* ===== Process handle destructor (called from rc_dec) ===== */

void YonaRuntimeProcessDestroy(void *ProcessHandle) {
  if (!ProcessHandle)
    return;
  YonaProcess *Process = (YonaProcess *)ProcessHandle;
  if (Process->StandardInputFileDescriptor >= 0)
    close(Process->StandardInputFileDescriptor);
  if (Process->StandardOutputFileDescriptor >= 0)
    close(Process->StandardOutputFileDescriptor);
  if (Process->StandardErrorFileDescriptor >= 0)
    close(Process->StandardErrorFileDescriptor);
  /* Reap zombie if not already waited */
  if (!Process->Exited) {
    int Status;
    waitpid((pid_t)Process->ProcessId, &Status, WNOHANG);
  }
}

static char *copyRuntimeString(const char *Source) {
  if (!Source)
    Source = "";
  size_t Count = strlen(Source);
  char *Result = (char *)YonaRuntimeAllocateString(Count + 1);
  memcpy(Result, Source, Count + 1);
  return Result;
}

static int isValidTemporaryFilePrefix(const char *Prefix) {
  if (!Prefix || !Prefix[0])
    return 0;
  for (const char *Character = Prefix; *Character; Character++) {
    if (*Character == '/' || *Character == '\\')
      return 0;
  }
  return 1;
}

char *YonaStdProcessExecutablePath(void) {
  uint32_t Size = 0;
  _NSGetExecutablePath(NULL, &Size);
  if (Size == 0)
    return copyRuntimeString("");
  char *TemporaryPath = (char *)malloc(Size);
  if (!TemporaryPath || _NSGetExecutablePath(TemporaryPath, &Size) != 0) {
    free(TemporaryPath);
    return copyRuntimeString("");
  }
  char ResolvedPath[PATH_MAX];
  if (realpath(TemporaryPath, ResolvedPath)) {
    free(TemporaryPath);
    return copyRuntimeString(ResolvedPath);
  }
  char *Result = copyRuntimeString(TemporaryPath);
  free(TemporaryPath);
  return Result;
}

char *YonaStdProcessTempDir(void) {
  const char *Directory = getenv("TMPDIR");
  if (!Directory || !Directory[0])
    Directory = getenv("TMP");
  if (!Directory || !Directory[0])
    Directory = getenv("TEMP");
  if (!Directory || !Directory[0])
    Directory = "/tmp";
  return copyRuntimeString(Directory);
}

char *YonaStdProcessTempFile(const char *Prefix, const char *Suffix) {
  if (!isValidTemporaryFilePrefix(Prefix))
    Prefix = "yona";
  if (!Suffix)
    Suffix = "";
  char *Directory = YonaStdProcessTempDir();
  size_t RequiredSize = strlen(Directory) + 1 + strlen(Prefix) + 6 + 1;
  char *TemplatePath = (char *)malloc(RequiredSize);
  if (!TemplatePath) {
    YonaRuntimeRelease(Directory);
    return copyRuntimeString("");
  }
  snprintf(TemplatePath, RequiredSize, "%s/%sXXXXXX", Directory, Prefix);
  YonaRuntimeRelease(Directory);
  int FileDescriptor = mkstemp(TemplatePath);
  if (FileDescriptor < 0) {
    free(TemplatePath);
    return copyRuntimeString("");
  }
  close(FileDescriptor);
  if (Suffix[0]) {
    size_t FinalLength = strlen(TemplatePath) + strlen(Suffix) + 1;
    char *NamedPath = (char *)malloc(FinalLength);
    if (!NamedPath) {
      unlink(TemplatePath);
      free(TemplatePath);
      return copyRuntimeString("");
    }
    snprintf(NamedPath, FinalLength, "%s%s", TemplatePath, Suffix);
    if (rename(TemplatePath, NamedPath) != 0) {
      unlink(TemplatePath);
      free(NamedPath);
      free(TemplatePath);
      return copyRuntimeString("");
    }
    char *Result = copyRuntimeString(NamedPath);
    free(NamedPath);
    free(TemplatePath);
    return Result;
  }
  char *Result = copyRuntimeString(TemplatePath);
  free(TemplatePath);
  return Result;
}

static int64_t runPosixProcess(const char *File, const char *ArgumentZero,
                               int64_t *ArgumentSequence) {
  if (!File || !File[0] || !ArgumentZero)
    return -1;
  char **ArgumentVector =
      buildPosixArgumentVector(ArgumentZero, ArgumentSequence);
  if (!ArgumentVector)
    return -1;
  pid_t ProcessId = fork();
  if (ProcessId < 0) {
    free(ArgumentVector);
    return -1;
  }
  if (ProcessId == 0) {
    execvp(File, ArgumentVector);
    _exit(127);
  }
  free(ArgumentVector);
  int Status = 0;
  if (waitpid(ProcessId, &Status, 0) < 0)
    return -1;
  if (WIFEXITED(Status))
    return WEXITSTATUS(Status);
  if (WIFSIGNALED(Status))
    return -(int64_t)WTERMSIG(Status);
  return -1;
}

int64_t YonaStdProcessRun(const char *File, int64_t *ArgumentSequence) {
  return runPosixProcess(File, File, ArgumentSequence);
}

int64_t YonaStdProcessRunWithArgv0(const char *File, const char *ArgumentZero,
                                   int64_t *ArgumentSequence) {
  return runPosixProcess(File, ArgumentZero, ArgumentSequence);
}

int64_t YonaStdProcessExecArgs(const char *File, int64_t *ArgumentSequence) {
  if (!File || !File[0])
    return 127;
  char **ArgumentVector = buildPosixArgumentVector(File, ArgumentSequence);
  if (!ArgumentVector)
    return 127;
  execvp(File, ArgumentVector);
  free(ArgumentVector);
  return 127;
}

/* ===== Platform constants ===== */
/* Constants exposed to Std\Constants\Platform. These are read once
 * at runtime and returned as Int/String; Yona's CAFs memoize the
 * first read so there's no per-call syscall overhead. */

#include <limits.h>
#include <sys/utsname.h>

int64_t YonaRuntimePlatformPageSize(void) {
  long Value = sysconf(_SC_PAGESIZE);
  return Value > 0 ? (int64_t)Value : 4096;
}

int64_t YonaRuntimePlatformCacheLineSize(void) {
  size_t CacheLineSize = 64;
  size_t SizeBytes = sizeof(CacheLineSize);
  if (sysctlbyname("hw.cachelinesize", &CacheLineSize, &SizeBytes, NULL, 0) ==
          0 &&
      CacheLineSize > 0)
    return (int64_t)CacheLineSize;
  return 64;
}

int64_t YonaRuntimePlatformMaximumPathLength(void) {
  long Value = pathconf("/", _PC_PATH_MAX);
  return Value > 0 ? (int64_t)Value : 4096;
}

int64_t YonaRuntimePlatformMaximumNameLength(void) {
  long Value = pathconf("/", _PC_NAME_MAX);
  return Value > 0 ? (int64_t)Value : 255;
}

int64_t YonaRuntimePlatformCpuCount(void) {
  long Value = sysconf(_SC_NPROCESSORS_ONLN);
  return Value > 0 ? (int64_t)Value : 1;
}

/* 1 = little-endian (x86_64, aarch64-le), 0 = big-endian. */
int64_t YonaRuntimePlatformIsLittleEndian(void) {
  uint16_t EndianProbe = 1;
  return (*(uint8_t *)&EndianProbe) == 1 ? 1 : 0;
}

const char *YonaRuntimePlatformOsName(void) {
  struct utsname SystemInfo;
  const char *Source = "unknown";
  if (uname(&SystemInfo) == 0)
    Source = SystemInfo.sysname;
  size_t Count = strlen(Source);
  char *Result = (char *)YonaRuntimeAllocateString(Count + 1);
  memcpy(Result, Source, Count + 1);
  return Result;
}

const char *YonaRuntimePlatformArchitecture(void) {
  struct utsname SystemInfo;
  const char *Source = "unknown";
  if (uname(&SystemInfo) == 0)
    Source = SystemInfo.machine;
  size_t Count = strlen(Source);
  char *Result = (char *)YonaRuntimeAllocateString(Count + 1);
  memcpy(Result, Source, Count + 1);
  return Result;
}
