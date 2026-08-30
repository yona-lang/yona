/*
 * Windows OS layer — console, environment, subprocess (sync pipes + Win32
 * process API). Phase 1: no pthread; uses CreateProcess, WaitForSingleObject,
 * CRT fds from HANDLEs.
 */

#ifndef _WIN32
#error "os_windows.c is for Windows builds only"
#endif

#ifndef _CRT_DECLARE_NONSTDC_NAMES
#define _CRT_DECLARE_NONSTDC_NAMES 1
#endif

#include "yona/Runtime/Collections/Sequence.h"
#include "yona/Runtime/Core/Api.h"
#include "yona/Runtime/Platform/Api.h"

#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <windows.h>

#define YONA_RC_TYPE_PROCESS 17

typedef struct {
  HANDLE ProcessHandle;
  DWORD ProcessId;
  int StandardInputFileDescriptor;
  int StandardOutputFileDescriptor;
  int StandardErrorFileDescriptor;
  int Exited;
  int ExitCode;
} YonaProcess;

static char *buildWindowsCommandLine(const char *Executable,
                                     int64_t *ArgumentSequence);

/* ----- Console ----- */

char *YonaRuntimePlatformReadLine(void) {
  char Buf[4096];
  if (!fgets(Buf, sizeof(Buf), stdin)) {
    char *R = (char *)YonaRuntimeAllocateString(1);
    R[0] = '\0';
    return R;
  }
  size_t Len = strlen(Buf);
  if (Len > 0 && Buf[Len - 1] == '\n')
    Buf[--Len] = '\0';
  if (Len > 0 && Buf[Len - 1] == '\r')
    Buf[--Len] = '\0';
  char *R = (char *)YonaRuntimeAllocateString(Len + 1);
  memcpy(R, Buf, Len + 1);
  return R;
}

/* ----- Environment ----- */

char *YonaRuntimePlatformGetEnvironment(const char *Name) {
  DWORD Need = GetEnvironmentVariableA(Name, NULL, 0);
  if (Need == 0) {
    char *R = (char *)YonaRuntimeAllocateString(1);
    R[0] = '\0';
    return R;
  }
  char *Tmp = (char *)malloc((size_t)Need);
  if (!Tmp) {
    char *R = (char *)YonaRuntimeAllocateString(1);
    R[0] = '\0';
    return R;
  }
  DWORD N = GetEnvironmentVariableA(Name, Tmp, Need);
  if (N == 0) {
    free(Tmp);
    char *R = (char *)YonaRuntimeAllocateString(1);
    R[0] = '\0';
    return R;
  }
  char *R = (char *)YonaRuntimeAllocateString((size_t)N + 1);
  memcpy(R, Tmp, (size_t)N + 1);
  free(Tmp);
  return R;
}

char *YonaRuntimePlatformGetCurrentWorkingDirectory(void) {
  char Buf[4096];
  if (!_getcwd(Buf, sizeof(Buf)))
    Buf[0] = '\0';
  size_t Len = strlen(Buf);
  char *R = (char *)YonaRuntimeAllocateString(Len + 1);
  memcpy(R, Buf, Len + 1);
  return R;
}

int64_t YonaRuntimePlatformSetEnvironment(const char *Name, const char *Value) {
  return _putenv_s(Name, Value) == 0 ? 1 : 0;
}

char *YonaRuntimePlatformHostName(void) {
  char Buf[256];
  DWORD N = sizeof(Buf);
  if (!GetComputerNameExA(ComputerNameDnsHostname, Buf, &N))
    Buf[0] = '\0';
  size_t Len = strlen(Buf);
  char *R = (char *)YonaRuntimeAllocateString(Len + 1);
  memcpy(R, Buf, Len + 1);
  return R;
}

/* ----- Shell-free execution ----- */

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

/* ----- spawn ----- */

static void closeFdIfValid(int *Fd) {
  if (*Fd >= 0) {
    _close(*Fd);
    *Fd = -1;
  }
}

void *YonaStdProcessSpawn(const char *Executable, int64_t *ArgumentSequence) {
  if (!Executable || !Executable[0])
    return NULL;

  SECURITY_ATTRIBUTES Sa;
  Sa.nLength = sizeof(Sa);
  Sa.lpSecurityDescriptor = NULL;
  Sa.bInheritHandle = TRUE;

  HANDLE HChildStdInRd = NULL, HChildStdInWr = NULL;
  HANDLE HChildStdOutRd = NULL, HChildStdOutWr = NULL;
  HANDLE HChildStdErrRd = NULL, HChildStdErrWr = NULL;

  if (!CreatePipe(&HChildStdOutRd, &HChildStdOutWr, &Sa, 0))
    return NULL;
  if (!SetHandleInformation(HChildStdOutRd, HANDLE_FLAG_INHERIT, 0))
    goto fail_all_pipes;
  if (!CreatePipe(&HChildStdErrRd, &HChildStdErrWr, &Sa, 0))
    goto fail_all_pipes;
  if (!SetHandleInformation(HChildStdErrRd, HANDLE_FLAG_INHERIT, 0))
    goto fail_all_pipes;
  if (!CreatePipe(&HChildStdInRd, &HChildStdInWr, &Sa, 0))
    goto fail_all_pipes;
  if (!SetHandleInformation(HChildStdInWr, HANDLE_FLAG_INHERIT, 0))
    goto fail_all_pipes;

  char *CommandLine = buildWindowsCommandLine(Executable, ArgumentSequence);
  if (!CommandLine)
    goto fail_all_pipes;

  STARTUPINFOA Si;
  memset(&Si, 0, sizeof(Si));
  Si.cb = sizeof(Si);
  Si.dwFlags = STARTF_USESTDHANDLES;
  Si.hStdInput = HChildStdInRd;
  Si.hStdOutput = HChildStdOutWr;
  Si.hStdError = HChildStdErrWr;

  PROCESS_INFORMATION Pi;
  memset(&Pi, 0, sizeof(Pi));

  BOOL Ok = CreateProcessA(Executable, CommandLine, NULL, NULL, TRUE,
                           CREATE_NO_WINDOW, NULL, NULL, &Si, &Pi);
  free(CommandLine);
  if (!Ok) {
    goto fail_all_pipes;
  }

  CloseHandle(HChildStdInRd);
  CloseHandle(HChildStdOutWr);
  CloseHandle(HChildStdErrWr);
  HChildStdInRd = HChildStdOutWr = HChildStdErrWr = NULL;

  int InFd = _open_osfhandle((intptr_t)HChildStdInWr, _O_WRONLY | _O_BINARY);
  int OutFd = _open_osfhandle((intptr_t)HChildStdOutRd, _O_RDONLY | _O_BINARY);
  int ErrFd = _open_osfhandle((intptr_t)HChildStdErrRd, _O_RDONLY | _O_BINARY);
  if (InFd < 0 || OutFd < 0 || ErrFd < 0) {
    if (InFd >= 0)
      _close(InFd);
    else if (HChildStdInWr)
      CloseHandle(HChildStdInWr);
    if (OutFd >= 0)
      _close(OutFd);
    else if (HChildStdOutRd)
      CloseHandle(HChildStdOutRd);
    if (ErrFd >= 0)
      _close(ErrFd);
    else if (HChildStdErrRd)
      CloseHandle(HChildStdErrRd);
    TerminateProcess(Pi.hProcess, 1);
    CloseHandle(Pi.hThread);
    CloseHandle(Pi.hProcess);
    return NULL;
  }

  YonaProcess *Proc = (YonaProcess *)YonaRuntimeAllocate(YONA_RC_TYPE_PROCESS,
                                                         sizeof(YonaProcess));
  Proc->ProcessHandle = Pi.hProcess;
  Proc->ProcessId = Pi.dwProcessId;
  Proc->StandardInputFileDescriptor = InFd;
  Proc->StandardOutputFileDescriptor = OutFd;
  Proc->StandardErrorFileDescriptor = ErrFd;
  Proc->Exited = 0;
  Proc->ExitCode = -1;
  CloseHandle(Pi.hThread);
  return Proc;

fail_all_pipes:
  if (HChildStdInRd)
    CloseHandle(HChildStdInRd);
  if (HChildStdInWr)
    CloseHandle(HChildStdInWr);
  if (HChildStdErrRd)
    CloseHandle(HChildStdErrRd);
  if (HChildStdErrWr)
    CloseHandle(HChildStdErrWr);
  if (HChildStdOutRd)
    CloseHandle(HChildStdOutRd);
  if (HChildStdOutWr)
    CloseHandle(HChildStdOutWr);
  return NULL;
}

char *YonaStdProcessReadLine(void *ProcHandle) {
  if (!ProcHandle) {
    char *R = (char *)YonaRuntimeAllocateString(1);
    R[0] = '\0';
    return R;
  }
  YonaProcess *Proc = (YonaProcess *)ProcHandle;
  if (Proc->StandardOutputFileDescriptor < 0) {
    char *R = (char *)YonaRuntimeAllocateString(1);
    R[0] = '\0';
    return R;
  }
  size_t Cap = 256, Len = 0;
  char *Buf = (char *)malloc(Cap);
  if (!Buf) {
    char *R = (char *)YonaRuntimeAllocateString(1);
    R[0] = '\0';
    return R;
  }
  for (;;) {
    char C;
    int N = (int)read(Proc->StandardOutputFileDescriptor, &C, 1);
    if (N <= 0)
      break;
    if (C == '\n')
      break;
    if (Len >= Cap - 1) {
      Cap *= 2;
      char *Nb = (char *)realloc(Buf, Cap);
      if (!Nb)
        break;
      Buf = Nb;
    }
    Buf[Len++] = C;
  }
  if (Len > 0 && Buf[Len - 1] == '\r')
    Len--;
  while (Len > 0 && (Buf[Len - 1] == ' ' || Buf[Len - 1] == '\t'))
    Len--;
  char *R = (char *)YonaRuntimeAllocateString(Len + 1);
  memcpy(R, Buf, Len);
  R[Len] = '\0';
  free(Buf);
  return R;
}

static char *readAllStdoutBlocking(YonaProcess *Proc) {
  if (Proc->StandardOutputFileDescriptor < 0) {
    char *R = (char *)YonaRuntimeAllocateString(1);
    R[0] = '\0';
    return R;
  }
  size_t Cap = 4096, Len = 0;
  char *Buf = (char *)malloc(Cap);
  if (!Buf) {
    char *R = (char *)YonaRuntimeAllocateString(1);
    R[0] = '\0';
    return R;
  }
  for (;;) {
    if (Len >= Cap) {
      Cap *= 2;
      char *Nb = (char *)realloc(Buf, Cap);
      if (!Nb)
        break;
      Buf = Nb;
    }
    int N = (int)read(Proc->StandardOutputFileDescriptor, Buf + Len,
                      (unsigned)(Cap - Len));
    if (N <= 0)
      break;
    Len += (size_t)N;
  }
  /* Normalize CRLF -> LF for cross-platform output parity. */
  size_t Out = 0;
  for (size_t I = 0; I < Len; ++I) {
    if (Buf[I] != '\r')
      Buf[Out++] = Buf[I];
  }
  Len = Out;
  if (Len > 0 && Buf[Len - 1] == '\n')
    Len--;
  if (Len > 0 && Buf[Len - 1] == '\r')
    Len--;
  char *R = (char *)YonaRuntimeAllocateString(Len + 1);
  memcpy(R, Buf, Len);
  R[Len] = '\0';
  free(Buf);
  return R;
}

char *YonaStdProcessReadAll(void *ProcHandle) {
  return readAllStdoutBlocking((YonaProcess *)ProcHandle);
}

int64_t YonaStdProcessWait(void *ProcHandle) {
  if (!ProcHandle)
    return -1;
  YonaProcess *Proc = (YonaProcess *)ProcHandle;
  if (Proc->Exited)
    return Proc->ExitCode;
  if (WaitForSingleObject(Proc->ProcessHandle, INFINITE) != WAIT_OBJECT_0)
    return -1;
  DWORD Code = 0;
  if (!GetExitCodeProcess(Proc->ProcessHandle, &Code))
    return -1;
  Proc->Exited = 1;
  Proc->ExitCode = (int)Code;
  return Proc->ExitCode;
}

int64_t YonaStdProcessKill(void *ProcHandle, int64_t Signal) {
  (void)Signal;
  if (!ProcHandle)
    return -1;
  YonaProcess *Proc = (YonaProcess *)ProcHandle;
  return TerminateProcess(Proc->ProcessHandle, 1) ? 1 : 0;
}

int64_t YonaStdProcessWriteStdin(void *ProcHandle, const char *Data) {
  if (!ProcHandle || !Data)
    return 0;
  YonaProcess *Proc = (YonaProcess *)ProcHandle;
  if (Proc->StandardInputFileDescriptor < 0)
    return 0;
  size_t Len = strlen(Data);
  int W = (int)write(Proc->StandardInputFileDescriptor, Data, (unsigned)Len);
  return (W == (int)Len) ? 1 : 0;
}

int64_t YonaStdProcessCloseStdin(void *ProcHandle) {
  if (!ProcHandle)
    return 0;
  YonaProcess *Proc = (YonaProcess *)ProcHandle;
  closeFdIfValid(&Proc->StandardInputFileDescriptor);
  return 1;
}

int64_t YonaStdProcessPid(void *ProcHandle) {
  if (!ProcHandle)
    return -1;
  return (int64_t)((YonaProcess *)ProcHandle)->ProcessId;
}

void YonaRuntimeProcessDestroy(void *ProcHandle) {
  if (!ProcHandle)
    return;
  YonaProcess *Proc = (YonaProcess *)ProcHandle;
  closeFdIfValid(&Proc->StandardInputFileDescriptor);
  closeFdIfValid(&Proc->StandardOutputFileDescriptor);
  closeFdIfValid(&Proc->StandardErrorFileDescriptor);
  if (Proc->ProcessHandle) {
    CloseHandle(Proc->ProcessHandle);
    Proc->ProcessHandle = NULL;
  }
}

static char *copyRuntimeString(const char *Src) {
  if (!Src)
    Src = "";
  size_t N = strlen(Src);
  char *R = (char *)YonaRuntimeAllocateString(N + 1);
  memcpy(R, Src, N + 1);
  return R;
}

static int isValidTemporaryFilePrefix(const char *Prefix) {
  if (!Prefix || !Prefix[0])
    return 0;
  for (const char *P = Prefix; *P; P++) {
    if (*P == '/' || *P == '\\')
      return 0;
  }
  return 1;
}

static char *quoteWindowsArgument(const char *Argument) {
  if (!Argument)
    Argument = "";
  int NeedsQuotes = Argument[0] == '\0';
  for (const char *Cursor = Argument; *Cursor; Cursor++) {
    if (*Cursor == ' ' || *Cursor == '\t' || *Cursor == '"') {
      NeedsQuotes = 1;
      break;
    }
  }
  if (!NeedsQuotes) {
    size_t Length = strlen(Argument);
    char *Result = (char *)malloc(Length + 1);
    if (!Result)
      return NULL;
    memcpy(Result, Argument, Length + 1);
    return Result;
  }
  size_t Length = strlen(Argument);
  char *Result = (char *)malloc(Length * 2 + 3);
  if (!Result)
    return NULL;
  char *Output = Result;
  *Output++ = '"';
  const char *Cursor = Argument;
  while (*Cursor) {
    size_t BackslashCount = 0;
    while (*Cursor == '\\') {
      BackslashCount++;
      Cursor++;
    }
    if (*Cursor == '"') {
      for (size_t Index = 0; Index < BackslashCount * 2 + 1; Index++)
        *Output++ = '\\';
      *Output++ = *Cursor++;
    } else if (!*Cursor) {
      for (size_t Index = 0; Index < BackslashCount * 2; Index++)
        *Output++ = '\\';
    } else {
      for (size_t Index = 0; Index < BackslashCount; Index++)
        *Output++ = '\\';
      *Output++ = *Cursor++;
    }
  }
  *Output++ = '"';
  *Output = '\0';
  return Result;
}

static char *buildWindowsCommandLine(const char *Executable,
                                     int64_t *ArgumentSequence) {
  int64_t ArgumentCount =
      ArgumentSequence ? YonaRuntimeSequenceLength(ArgumentSequence) : 0;
  if (ArgumentCount < 0)
    ArgumentCount = 0;
  size_t Cap = 256, Len = 0;
  char *Out = (char *)malloc(Cap);
  if (!Out)
    return NULL;
  Out[0] = '\0';
  for (int64_t I = 0; I <= ArgumentCount; I++) {
    const char *Argument = I == 0
                               ? Executable
                               : (const char *)(intptr_t)YonaRuntimeSequenceGet(
                                     ArgumentSequence, I - 1);
    char *Q = quoteWindowsArgument(Argument);
    if (!Q) {
      free(Out);
      return NULL;
    }
    size_t Qn = strlen(Q);
    size_t Extra = Qn + (I ? 1 : 0);
    if (Len + Extra + 1 > Cap) {
      Cap = (Len + Extra + 1) * 2;
      char *Nb = (char *)realloc(Out, Cap);
      if (!Nb) {
        free(Q);
        free(Out);
        return NULL;
      }
      Out = Nb;
    }
    if (I)
      Out[Len++] = ' ';
    memcpy(Out + Len, Q, Qn + 1);
    Len += Qn;
    free(Q);
  }
  return Out;
}

char *YonaStdProcessExecutablePath(void) {
  wchar_t Wbuf[32768];
  DWORD N = GetModuleFileNameW(NULL, Wbuf, 32768);
  if (N == 0 || N >= 32768)
    return copyRuntimeString("");
  int Utf8 = WideCharToMultiByte(CP_UTF8, 0, Wbuf, (int)N, NULL, 0, NULL, NULL);
  if (Utf8 <= 0)
    return copyRuntimeString("");
  char *Tmp = (char *)malloc((size_t)Utf8 + 1);
  if (!Tmp)
    return copyRuntimeString("");
  WideCharToMultiByte(CP_UTF8, 0, Wbuf, (int)N, Tmp, Utf8, NULL, NULL);
  Tmp[Utf8] = '\0';
  /* Std\Path historically split on `/` only; keep a slash-form so dirname of
   * this value finds the real sibling directory, not cwd. */
  for (int I = 0; I < Utf8; I++) {
    if (Tmp[I] == '\\')
      Tmp[I] = '/';
  }
  char *R = copyRuntimeString(Tmp);
  free(Tmp);
  return R;
}

char *YonaStdProcessTempDir(void) {
  char Dir[MAX_PATH];
  DWORD N = GetTempPathA(MAX_PATH, Dir);
  if (N == 0 || N >= MAX_PATH)
    return copyRuntimeString("");
  if (N > 0 && (Dir[N - 1] == '\\' || Dir[N - 1] == '/'))
    Dir[N - 1] = '\0';
  return copyRuntimeString(Dir);
}

char *YonaStdProcessTempFile(const char *Prefix, const char *Suffix) {
  if (!isValidTemporaryFilePrefix(Prefix))
    Prefix = "yon";
  if (!Suffix)
    Suffix = "";
  char Dir[MAX_PATH], Name[MAX_PATH];
  if (GetTempPathA(MAX_PATH, Dir) == 0)
    return copyRuntimeString("");
  char Pre[4] = {0};
  Pre[0] = Prefix[0];
  if (Prefix[1])
    Pre[1] = Prefix[1];
  if (Prefix[1] && Prefix[2])
    Pre[2] = Prefix[2];
  if (GetTempFileNameA(Dir, Pre, 0, Name) == 0)
    return copyRuntimeString("");
  if (!Suffix[0])
    return copyRuntimeString(Name);
  char Named[MAX_PATH];
  if (snprintf(Named, MAX_PATH, "%s%s", Name, Suffix) >= MAX_PATH) {
    DeleteFileA(Name);
    return copyRuntimeString("");
  }
  if (!MoveFileA(Name, Named)) {
    DeleteFileA(Name);
    return copyRuntimeString("");
  }
  return copyRuntimeString(Named);
}

int64_t YonaStdProcessRun(const char *File, int64_t *ArgumentSequence) {
  if (!File || !File[0])
    return -1;
  size_t Flen = strlen(File);
  char *Native = (char *)malloc(Flen + 1);
  if (!Native)
    return -1;
  memcpy(Native, File, Flen + 1);
  for (char *P = Native; *P; P++) {
    if (*P == '/')
      *P = '\\';
  }
  char *CommandLine = buildWindowsCommandLine(File, ArgumentSequence);
  if (!CommandLine) {
    free(Native);
    return -1;
  }
  STARTUPINFOA Si;
  memset(&Si, 0, sizeof(Si));
  Si.cb = sizeof(Si);
  PROCESS_INFORMATION Pi;
  memset(&Pi, 0, sizeof(Pi));
  BOOL Ok = CreateProcessA(Native, CommandLine, NULL, NULL, TRUE, 0, NULL, NULL,
                           &Si, &Pi);
  if (!Ok) {
    /* CreateProcess may mutate lpCommandLine; rebuild for PATH search. */
    free(CommandLine);
    CommandLine = buildWindowsCommandLine(File, ArgumentSequence);
    if (!CommandLine) {
      free(Native);
      return -1;
    }
    Ok = CreateProcessA(NULL, CommandLine, NULL, NULL, TRUE, 0, NULL, NULL, &Si,
                        &Pi);
  }
  free(Native);
  free(CommandLine);
  if (!Ok)
    return -1;
  CloseHandle(Pi.hThread);
  if (WaitForSingleObject(Pi.hProcess, INFINITE) != WAIT_OBJECT_0) {
    CloseHandle(Pi.hProcess);
    return -1;
  }
  DWORD Code = 0;
  if (!GetExitCodeProcess(Pi.hProcess, &Code)) {
    CloseHandle(Pi.hProcess);
    return -1;
  }
  CloseHandle(Pi.hProcess);
  return (int64_t)(int)Code;
}

int64_t YonaStdProcessExecArgs(const char *File, int64_t *ArgumentSequence) {
  int64_t Code = YonaStdProcessRun(File, ArgumentSequence);
  ExitProcess(Code < 0 ? 127 : (UINT)Code);
  return Code;
}

/* ----- Platform constants (Std\Constants\Platform) ----- */

int64_t YonaRuntimePlatformPageSize(void) {
  SYSTEM_INFO Si;
  GetSystemInfo(&Si);
  return (int64_t)Si.dwPageSize;
}

int64_t YonaRuntimePlatformCacheLineSize(void) { return 64; }

int64_t YonaRuntimePlatformMaximumPathLength(void) { return (int64_t)MAX_PATH; }

int64_t YonaRuntimePlatformMaximumNameLength(void) { return 255; }

int64_t YonaRuntimePlatformCpuCount(void) {
  DWORD N = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  return N > 0 ? (int64_t)N : 1;
}

int64_t YonaRuntimePlatformIsLittleEndian(void) {
  uint16_t X = 1;
  return (*(uint8_t *)&X) == 1 ? 1 : 0;
}

const char *YonaRuntimePlatformOsName(void) {
  char *R = (char *)YonaRuntimeAllocateString(8);
  memcpy(R, "Windows", 8);
  return R;
}

const char *YonaRuntimePlatformArchitecture(void) {
  SYSTEM_INFO Si;
  GetNativeSystemInfo(&Si);
  const char *Src = "unknown";
  switch (Si.wProcessorArchitecture) {
  case PROCESSOR_ARCHITECTURE_AMD64:
    Src = "x86_64";
    break;
  case PROCESSOR_ARCHITECTURE_ARM64:
    Src = "aarch64";
    break;
  case PROCESSOR_ARCHITECTURE_INTEL:
    Src = "x86";
    break;
  default:
    break;
  }
  size_t N = strlen(Src);
  char *R = (char *)YonaRuntimeAllocateString(N + 1);
  memcpy(R, Src, N + 1);
  return R;
}
