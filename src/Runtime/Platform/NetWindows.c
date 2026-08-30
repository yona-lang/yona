/*
 * Windows networking — overlapped Winsock + IOCP submit/await integration.
 */

#ifndef _WIN32
#error "net_windows.c is for Windows builds only"
#endif

#include "yona/Runtime/Core/Api.h"
#include "yona/Runtime/Platform/Api.h"
#include "yona/Runtime/Platform/WindowsSockets.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int64_t YonaRuntimeIoRegisterDirectResult(void *Result);
extern int64_t YonaRuntimeWindowsRegisterIoContext(void *Ctx);
extern const char *YonaStdHttpBuildRequest(const char *Method, const char *Host,
                                           const char *Path, const char *Body);
extern int64_t *YonaStdHttpParseUrl(const char *Url);

#define YONA_RC_TYPE_STRING 6
#define YONA_RC_TYPE_BYTE_ARRAY 8

static INIT_ONCE WsaOnce = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK wsaInitCb(PINIT_ONCE O, PVOID P, PVOID *C) {
  WSADATA W;
  (void)O;
  (void)P;
  (void)C;
  return WSAStartup(MAKEWORD(2, 2), &W) == 0;
}

static void netEnsureWsa(void) {
  InitOnceExecuteOnce(&WsaOnce, wsaInitCb, NULL, NULL);
}

static SOCKET sockFromI64(int64_t Fd) { return (SOCKET)(intptr_t)Fd; }

/* Must match file_windows.c YonaIoContext layout. */
typedef struct {
  int Type;
  int Fd;
  char *Buf;
  size_t BufSize;
  int CloseFd;
} YonaIoContext;

#define YONA_IO_OP_DIRECT_RESULT 99

typedef enum {
  NetIoSendStr = 1,
  NetIoRecvStr = 2,
  NetIoSendBytes = 3,
  NetIoRecvBytes = 4,
  NetIoConnect = 7,
  NetIoAccept = 8,
} YonaNetworkIoKind;

typedef struct {
  OVERLAPPED Ov;
  HANDLE HDone;
  SOCKET S;
  WSABUF Wbuf;
  struct sockaddr_in Addr;
  char AcceptAddrBuf[(sizeof(struct sockaddr_in) + 16) * 2];
  int AddrLen;
  YonaIoContext *Ctx;
  YonaNetworkIoKind Kind;
  void *PinnedObj;
  void *IoBuf;
} YonaWindowsNetworkOperation;

static HANDLE NetIocpPort;
static HANDLE NetIocpThread;
static INIT_ONCE NetIocpOnce = INIT_ONCE_STATIC_INIT;
static LPFN_CONNECTEX PConnectEx = NULL;
static LPFN_ACCEPTEX PAcceptEx = NULL;

static DWORD WINAPI yonaNetIocpWorker(void *Unused) {
  (void)Unused;
  for (;;) {
    DWORD Nbytes = 0;
    ULONG_PTR Key = 0;
    LPOVERLAPPED Ov = NULL;
    BOOL Ok =
        GetQueuedCompletionStatus(NetIocpPort, &Nbytes, &Key, &Ov, INFINITE);
    (void)Key;
    if (!Ov)
      continue;
    YonaWindowsNetworkOperation *Op =
        (YonaWindowsNetworkOperation *)((char *)Ov -
                                        offsetof(YonaWindowsNetworkOperation,
                                                 Ov));
    if (!Op || !Op->Ctx)
      continue;

    if (Op->Kind == NetIoSendStr || Op->Kind == NetIoSendBytes) {
      if (Op->PinnedObj) {
        YonaRuntimeRelease(Op->PinnedObj);
        Op->PinnedObj = NULL;
      }
      Op->Ctx->Buf = (char *)(intptr_t)(Ok ? (int64_t)Nbytes : -1);
    } else if (Op->Kind == NetIoRecvStr) {
      char *Buf = (char *)Op->IoBuf;
      if (Buf) {
        if (Ok && Nbytes > 0 && Nbytes < Op->Wbuf.len)
          Buf[Nbytes] = '\0';
        else
          Buf[0] = '\0';
      }
      Op->Ctx->Buf = (char *)(intptr_t)Buf;
    } else if (Op->Kind == NetIoRecvBytes) {
      int64_t *B = (int64_t *)Op->IoBuf;
      if (B)
        B[0] = (Ok && Nbytes > 0) ? (int64_t)Nbytes : 0;
      Op->Ctx->Buf = (char *)(intptr_t)B;
    } else if (Op->Kind == NetIoConnect) {
      if (Ok) {
        setsockopt(Op->S, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
        Op->Ctx->Buf = (char *)(intptr_t)Op->S;
      } else {
        closesocket(Op->S);
        Op->Ctx->Buf = (char *)(intptr_t)0;
      }
    } else if (Op->Kind == NetIoAccept) {
      SOCKET Accepted = (SOCKET)(uintptr_t)Op->IoBuf;
      if (Ok) {
        setsockopt(Accepted, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                   (const char *)&Op->S, sizeof(Op->S));
        Op->Ctx->Buf = (char *)(intptr_t)Accepted;
      } else {
        closesocket(Accepted);
        Op->Ctx->Buf = (char *)(intptr_t)0;
      }
    } else {
      Op->Ctx->Buf = (char *)(intptr_t)-1;
    }
    Op->Ctx->Type = YONA_IO_OP_DIRECT_RESULT;
    SetEvent(Op->HDone);
    free(Op);
  }
}

static BOOL CALLBACK netIocpInitCb(PINIT_ONCE O, PVOID P, PVOID *C) {
  (void)O;
  (void)P;
  (void)C;
  NetIocpPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
  if (!NetIocpPort)
    return TRUE;
  NetIocpThread = CreateThread(NULL, 0, yonaNetIocpWorker, NULL, 0, NULL);
  if (!NetIocpThread) {
    CloseHandle(NetIocpPort);
    NetIocpPort = NULL;
  }
  /* Resolve Winsock extension entry points once (ConnectEx/AcceptEx). */
  SOCKET Probe = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                           WSA_FLAG_OVERLAPPED);
  if (Probe != INVALID_SOCKET) {
    DWORD Bytes = 0;
    GUID GuidConnectEx = WSAID_CONNECTEX;
    GUID GuidAcceptEx = WSAID_ACCEPTEX;
    (void)WSAIoctl(Probe, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidConnectEx,
                   sizeof(GuidConnectEx), &PConnectEx, sizeof(PConnectEx),
                   &Bytes, NULL, NULL);
    (void)WSAIoctl(Probe, SIO_GET_EXTENSION_FUNCTION_POINTER, &GuidAcceptEx,
                   sizeof(GuidAcceptEx), &PAcceptEx, sizeof(PAcceptEx), &Bytes,
                   NULL, NULL);
    closesocket(Probe);
  }
  return TRUE;
}

static void netIocpEnsure(void) {
  InitOnceExecuteOnce(&NetIocpOnce, netIocpInitCb, NULL, NULL);
}

static int64_t netSubmitIocp(SOCKET S, YonaNetworkIoKind Kind, void *WireBuf,
                             size_t Len, void *PinnedObj, void *IoOwner) {
  netIocpEnsure();
  if (!NetIocpPort) {
    if (PinnedObj)
      YonaRuntimeRelease(PinnedObj);
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)-1);
  }
  if (!CreateIoCompletionPort((HANDLE)S, NetIocpPort, 0, 0)) {
    if (PinnedObj)
      YonaRuntimeRelease(PinnedObj);
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)-1);
  }

  YonaIoContext *Ctx = (YonaIoContext *)calloc(1, sizeof(YonaIoContext));
  YonaWindowsNetworkOperation *Op = (YonaWindowsNetworkOperation *)calloc(
      1, sizeof(YonaWindowsNetworkOperation));
  if (!Ctx || !Op) {
    if (Ctx)
      free(Ctx);
    if (Op)
      free(Op);
    if (PinnedObj)
      YonaRuntimeRelease(PinnedObj);
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)-1);
  }

  Op->HDone = CreateEvent(NULL, TRUE, FALSE, NULL);
  if (!Op->HDone) {
    if (PinnedObj)
      YonaRuntimeRelease(PinnedObj);
    free(Op);
    free(Ctx);
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)-1);
  }
  Op->S = S;
  Op->Ctx = Ctx;
  Op->Kind = Kind;
  Op->PinnedObj = PinnedObj;
  Op->IoBuf = IoOwner ? IoOwner : WireBuf;
  Op->Wbuf.buf = (CHAR *)WireBuf;
  Op->Wbuf.len = (ULONG)Len;
  Op->AddrLen = sizeof(Op->Addr);

  Ctx->Type = 0; /* pending */
  Ctx->Fd = -1;
  Ctx->Buf = NULL;
  Ctx->BufSize = (size_t)(uintptr_t)Op->HDone;
  Ctx->CloseFd = 7; /* file_windows.c treats this as wait-handle pending */
  int64_t Id = YonaRuntimeWindowsRegisterIoContext(Ctx);
  if (Id <= 0) {
    if (PinnedObj)
      YonaRuntimeRelease(PinnedObj);
    if (Op->HDone)
      CloseHandle(Op->HDone);
    free(Op);
    free(Ctx);
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)-1);
  }

  DWORD Flags = 0, Sent = 0;
  int Rc;
  if (Kind == NetIoSendStr || Kind == NetIoSendBytes)
    Rc = WSASend(S, &Op->Wbuf, 1, &Sent, 0, &Op->Ov, NULL);
  else
    Rc = WSARecv(S, &Op->Wbuf, 1, &Sent, &Flags, &Op->Ov, NULL);
  if (Rc == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
    if (PinnedObj) {
      YonaRuntimeRelease(PinnedObj);
      Op->PinnedObj = NULL;
    }
    Ctx->Buf = (char *)(intptr_t)-1;
    Ctx->Type = YONA_IO_OP_DIRECT_RESULT;
    SetEvent(Op->HDone);
    free(Op);
  }
  return Id;
}

static int64_t netSubmitConnectIocp(const char *Host, int64_t Port) {
  netIocpEnsure();
  if (!NetIocpPort || !PConnectEx || !Host)
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)0);

  struct addrinfo Hints, *Res = NULL;
  memset(&Hints, 0, sizeof(Hints));
  Hints.ai_family = AF_INET;
  Hints.ai_socktype = SOCK_STREAM;
  char PortStr[16];
  snprintf(PortStr, sizeof(PortStr), "%lld", (long long)Port);
  if (getaddrinfo(Host, PortStr, &Hints, &Res) != 0 || !Res)
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)0);

  SOCKET S = WSASocket(Res->ai_family, Res->ai_socktype, Res->ai_protocol, NULL,
                       0, WSA_FLAG_OVERLAPPED);
  if (S == INVALID_SOCKET) {
    freeaddrinfo(Res);
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)0);
  }
  if (!CreateIoCompletionPort((HANDLE)S, NetIocpPort, 0, 0)) {
    closesocket(S);
    freeaddrinfo(Res);
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)0);
  }

  struct sockaddr_in Local;
  memset(&Local, 0, sizeof(Local));
  Local.sin_family = AF_INET;
  Local.sin_addr.s_addr = htonl(INADDR_ANY);
  Local.sin_port = 0;
  if (bind(S, (SOCKADDR *)&Local, sizeof(Local)) == SOCKET_ERROR) {
    closesocket(S);
    freeaddrinfo(Res);
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)0);
  }

  YonaIoContext *Ctx = (YonaIoContext *)calloc(1, sizeof(YonaIoContext));
  YonaWindowsNetworkOperation *Op = (YonaWindowsNetworkOperation *)calloc(
      1, sizeof(YonaWindowsNetworkOperation));
  if (!Ctx || !Op) {
    if (Ctx)
      free(Ctx);
    if (Op)
      free(Op);
    closesocket(S);
    freeaddrinfo(Res);
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)0);
  }
  Op->HDone = CreateEvent(NULL, TRUE, FALSE, NULL);
  if (!Op->HDone) {
    free(Op);
    free(Ctx);
    closesocket(S);
    freeaddrinfo(Res);
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)0);
  }
  Op->S = S;
  Op->Ctx = Ctx;
  Op->Kind = NetIoConnect;
  Ctx->Type = 0;
  Ctx->Fd = -1;
  Ctx->Buf = NULL;
  Ctx->BufSize = (size_t)(uintptr_t)Op->HDone;
  Ctx->CloseFd = 7;
  int64_t Id = YonaRuntimeWindowsRegisterIoContext(Ctx);
  if (Id <= 0) {
    if (Op->HDone)
      CloseHandle(Op->HDone);
    free(Op);
    free(Ctx);
    closesocket(S);
    freeaddrinfo(Res);
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)0);
  }
  BOOL Ok =
      PConnectEx(S, Res->ai_addr, (int)Res->ai_addrlen, NULL, 0, NULL, &Op->Ov);
  freeaddrinfo(Res);
  if (!Ok && WSAGetLastError() != ERROR_IO_PENDING) {
    Ctx->Buf = (char *)(intptr_t)0;
    Ctx->Type = YONA_IO_OP_DIRECT_RESULT;
    SetEvent(Op->HDone);
    free(Op);
  }
  return Id;
}

static int64_t netSubmitAcceptIocp(SOCKET Listener) {
  netIocpEnsure();
  if (!NetIocpPort || !PAcceptEx || Listener == INVALID_SOCKET)
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)0);
  SOCKET Accepted = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                              WSA_FLAG_OVERLAPPED);
  if (Accepted == INVALID_SOCKET)
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)0);
  if (!CreateIoCompletionPort((HANDLE)Listener, NetIocpPort, 0, 0) ||
      !CreateIoCompletionPort((HANDLE)Accepted, NetIocpPort, 0, 0)) {
    closesocket(Accepted);
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)0);
  }

  YonaIoContext *Ctx = (YonaIoContext *)calloc(1, sizeof(YonaIoContext));
  YonaWindowsNetworkOperation *Op = (YonaWindowsNetworkOperation *)calloc(
      1, sizeof(YonaWindowsNetworkOperation));
  if (!Ctx || !Op) {
    if (Ctx)
      free(Ctx);
    if (Op)
      free(Op);
    closesocket(Accepted);
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)0);
  }
  Op->HDone = CreateEvent(NULL, TRUE, FALSE, NULL);
  if (!Op->HDone) {
    free(Op);
    free(Ctx);
    closesocket(Accepted);
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)0);
  }
  Op->S = Listener;
  Op->Ctx = Ctx;
  Op->Kind = NetIoAccept;
  Op->IoBuf = (void *)(uintptr_t)Accepted;
  Ctx->Type = 0;
  Ctx->Fd = -1;
  Ctx->Buf = NULL;
  Ctx->BufSize = (size_t)(uintptr_t)Op->HDone;
  Ctx->CloseFd = 7;
  int64_t Id = YonaRuntimeWindowsRegisterIoContext(Ctx);
  if (Id <= 0) {
    if (Op->HDone)
      CloseHandle(Op->HDone);
    free(Op);
    free(Ctx);
    closesocket(Accepted);
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)0);
  }
  DWORD Bytes = 0;
  BOOL Ok = PAcceptEx(Listener, Accepted, Op->AcceptAddrBuf, 0,
                      sizeof(struct sockaddr_in) + 16,
                      sizeof(struct sockaddr_in) + 16, &Bytes, &Op->Ov);
  if (!Ok && WSAGetLastError() != ERROR_IO_PENDING) {
    Ctx->Buf = (char *)(intptr_t)0;
    Ctx->Type = YONA_IO_OP_DIRECT_RESULT;
    SetEvent(Op->HDone);
    free(Op);
  }
  return Id;
}

int64_t YonaStdNetTcpConnect(const char *Host, int64_t Port) {
  netEnsureWsa();
  return netSubmitConnectIocp(Host, Port);
}

int64_t YonaStdNetTcpListen(const char *Host, int64_t Port) {
  netEnsureWsa();
  struct sockaddr_in Addr;
  memset(&Addr, 0, sizeof(Addr));
  Addr.sin_family = AF_INET;
  Addr.sin_port = htons((uint16_t)Port);
  if (Host && Host[0] != '\0') {
    if (InetPtonA(AF_INET, Host, &Addr.sin_addr) != 1)
      return -1;
  } else {
    Addr.sin_addr.s_addr = htonl(INADDR_ANY);
  }
  SOCKET Fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (Fd == INVALID_SOCKET)
    return -1;
  BOOL Opt = TRUE;
  setsockopt(Fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&Opt, sizeof(Opt));
  if (bind(Fd, (struct sockaddr *)&Addr, sizeof(Addr)) != 0) {
    closesocket(Fd);
    return -1;
  }
  if (listen(Fd, SOMAXCONN) != 0) {
    closesocket(Fd);
    return -1;
  }
  return (int64_t)(intptr_t)Fd;
}

int64_t YonaStdNetTcpAccept(int64_t ListenerFd) {
  netEnsureWsa();
  return netSubmitAcceptIocp(sockFromI64(ListenerFd));
}

int64_t YonaStdNetSend(int64_t Fd, const char *Data) {
  netEnsureWsa();
  if (!Data)
    Data = "";
  size_t Len = strlen(Data);
  char *Pinned = (char *)YonaRuntimeAllocateString(Len + 1);
  memcpy(Pinned, Data, Len + 1);
  return netSubmitIocp(sockFromI64(Fd), NetIoSendStr, Pinned, Len, Pinned,
                       Pinned);
}

int64_t YonaStdNetRecv(int64_t Fd, int64_t MaxBytes) {
  netEnsureWsa();
  if (MaxBytes <= 0)
    MaxBytes = 4096;
  char *Buf = (char *)YonaRuntimeAllocateString((size_t)MaxBytes + 1);
  return netSubmitIocp(sockFromI64(Fd), NetIoRecvStr, Buf, (size_t)MaxBytes,
                       NULL, Buf);
}

int64_t YonaStdNetSendBytes(int64_t Fd, void *Bytes) {
  netEnsureWsa();
  if (!Bytes)
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)-1);
  int64_t *B = (int64_t *)Bytes;
  int64_t Len = B[0] < 0 ? 0 : B[0];
  YonaRuntimeRetain(Bytes);
  uint8_t *Data = (uint8_t *)(B + 1);
  return netSubmitIocp(sockFromI64(Fd), NetIoSendBytes, Data, (size_t)Len,
                       Bytes, Bytes);
}

int64_t YonaStdNetRecvBytes(int64_t Fd, int64_t MaxBytes) {
  netEnsureWsa();
  if (MaxBytes <= 0)
    MaxBytes = 4096;
  int64_t *Buf = (int64_t *)YonaRuntimeAllocate(
      YONA_RC_TYPE_BYTE_ARRAY, sizeof(int64_t) + (size_t)MaxBytes);
  Buf[0] = 0;
  return netSubmitIocp(sockFromI64(Fd), NetIoRecvBytes, (void *)(Buf + 1),
                       (size_t)MaxBytes, NULL, Buf);
}

int64_t YonaStdNetClose(int64_t Fd) {
  netEnsureWsa();
  closesocket(sockFromI64(Fd));
  return 0;
}

int64_t YonaStdHttpHttpGet(const char *Url) {
  netEnsureWsa();
  if (!Url)
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)0);
  int64_t *Parsed = YonaStdHttpParseUrl(Url);
  const char *Host = (const char *)(intptr_t)YonaRuntimeAdtGetField(Parsed, 0);
  int64_t Port = YonaRuntimeAdtGetField(Parsed, 1);
  const char *Path = (const char *)(intptr_t)YonaRuntimeAdtGetField(Parsed, 2);
  if (!Host || !Path) {
    YonaRuntimeRelease(Parsed);
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)0);
  }

  int64_t ConnectId = YonaStdNetTcpConnect(Host, Port);
  SOCKET Sock = (SOCKET)(intptr_t)YonaRuntimeIoAwait(ConnectId);
  if (Sock == INVALID_SOCKET || Sock == 0) {
    YonaRuntimeRelease(Parsed);
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)0);
  }

  const char *Req = YonaStdHttpBuildRequest("GET", Host, Path, NULL);
  YonaRuntimeRelease(Parsed);
  int64_t SendId = YonaStdNetSend((int64_t)(intptr_t)Sock, Req);
  int64_t Sent = YonaRuntimeIoAwait(SendId);
  YonaRuntimeRelease((void *)Req);
  if (Sent <= 0) {
    closesocket(Sock);
    return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)0);
  }

  size_t BufSize = 16384;
  size_t Total = 0;
  char *Buf = (char *)YonaRuntimeAllocate(YONA_RC_TYPE_STRING, BufSize);
  for (;;) {
    int64_t RecvId = YonaStdNetRecv((int64_t)(intptr_t)Sock, 4096);
    char *Chunk = (char *)(intptr_t)YonaRuntimeIoAwait(RecvId);
    if (!Chunk || Chunk[0] == '\0') {
      if (Chunk)
        YonaRuntimeRelease(Chunk);
      break;
    }
    size_t N = strlen(Chunk);
    if (N == 0) {
      YonaRuntimeRelease(Chunk);
      break;
    }
    if (Total + N + 1 > BufSize) {
      size_t NewSize = BufSize;
      while (Total + N + 1 > NewSize)
        NewSize *= 2;
      char *NewBuf = (char *)YonaRuntimeAllocate(YONA_RC_TYPE_STRING, NewSize);
      memcpy(NewBuf, Buf, Total);
      YonaRuntimeRelease(Buf);
      Buf = NewBuf;
      BufSize = NewSize;
    }
    memcpy(Buf + Total, Chunk, N);
    Total += N;
    YonaRuntimeRelease(Chunk);
  }
  Buf[Total] = '\0';
  closesocket(Sock);
  return YonaRuntimeIoRegisterDirectResult((void *)(intptr_t)Buf);
}

int64_t YonaStdNetUdpBind(const char *Host, int64_t Port) {
  netEnsureWsa();
  struct sockaddr_in Addr;
  memset(&Addr, 0, sizeof(Addr));
  Addr.sin_family = AF_INET;
  Addr.sin_port = htons((uint16_t)Port);
  if (Host && Host[0] != '\0') {
    if (InetPtonA(AF_INET, Host, &Addr.sin_addr) != 1)
      return -1;
  } else {
    Addr.sin_addr.s_addr = htonl(INADDR_ANY);
  }
  SOCKET Fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (Fd == INVALID_SOCKET)
    return -1;
  if (bind(Fd, (struct sockaddr *)&Addr, sizeof(Addr)) != 0) {
    closesocket(Fd);
    return -1;
  }
  return (int64_t)(intptr_t)Fd;
}

/* UDP is FN (sync) in Net.yonai — same contract as Linux/macOS. Returning
 * an IOCP cookie here made the C test (and Yona callers) treat a completion
 * id as a string pointer and SIGSEGV. */
int64_t YonaStdNetUdpSendTo(int64_t Fd, const char *Host, int64_t Port,
                            const char *Data) {
  netEnsureWsa();
  if (!Host)
    return -1;
  struct sockaddr_in Addr;
  memset(&Addr, 0, sizeof(Addr));
  Addr.sin_family = AF_INET;
  Addr.sin_port = htons((uint16_t)Port);
  if (InetPtonA(AF_INET, Host, &Addr.sin_addr) != 1)
    return -1;
  const char *Payload = Data ? Data : "";
  int N = sendto(sockFromI64(Fd), Payload, (int)strlen(Payload), 0,
                 (struct sockaddr *)&Addr, sizeof(Addr));
  return (int64_t)N;
}

int64_t YonaStdNetUdpRecv(int64_t Fd, int64_t MaxBytes) {
  netEnsureWsa();
  if (MaxBytes <= 0)
    MaxBytes = 4096;
  char *Buf = (char *)YonaRuntimeAllocateString((size_t)MaxBytes + 1);
  struct sockaddr_in From;
  int FromLen = sizeof(From);
  int N = recvfrom(sockFromI64(Fd), Buf, (int)MaxBytes, 0,
                   (struct sockaddr *)&From, &FromLen);
  if (N <= 0) {
    Buf[0] = '\0';
    return (int64_t)(intptr_t)Buf;
  }
  Buf[N] = '\0';
  return (int64_t)(intptr_t)Buf;
}

const char *YonaStdNetPeerAddress(int64_t Fd) {
  netEnsureWsa();
  struct sockaddr_in Addr;
  int Len = sizeof(Addr);
  SOCKET S = sockFromI64(Fd);
  if (getpeername(S, (struct sockaddr *)&Addr, &Len) != 0) {
    char *R = (char *)YonaRuntimeAllocateString(8);
    memcpy(R, "unknown", 8);
    return R;
  }
  char Ip[INET_ADDRSTRLEN];
  if (!InetNtopA(AF_INET, &Addr.sin_addr, Ip, sizeof(Ip))) {
    char *R = (char *)YonaRuntimeAllocateString(8);
    memcpy(R, "unknown", 8);
    return R;
  }
  char *R = (char *)YonaRuntimeAllocateString(64);
  snprintf(R, 64, "%s:%u", Ip, (unsigned)ntohs(Addr.sin_port));
  return R;
}
