/*
 * Linux networking — submit-and-return via io_uring.
 *
 * Socket creation/bind/listen use POSIX (fast, no I/O wait).
 * Data transfer (accept, connect, send, recv) submits to io_uring
 * and returns the uring ID immediately. Completion via YonaRuntimeIoAwait().
 */

#include "yona/Runtime/Core/Api.h"
#include "yona/Runtime/Platform/Api.h"
#include "yona/Runtime/Platform/IoUring.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ===== TCP ===== */

int64_t YonaStdNetTcpConnect(const char *Host, int64_t Port) {
  struct addrinfo Hints, *Res;
  memset(&Hints, 0, sizeof(Hints));
  Hints.ai_family = AF_INET;
  Hints.ai_socktype = SOCK_STREAM;
  char PortString[8];
  snprintf(PortString, sizeof(PortString), "%" PRId64, Port);
  if (getaddrinfo(Host, PortString, &Hints, &Res) != 0)
    return 0;
  int Fd = socket(Res->ai_family, Res->ai_socktype, Res->ai_protocol);
  if (Fd < 0) {
    freeaddrinfo(Res);
    return 0;
  }

  struct sockaddr_storage *AddressCopy =
      (struct sockaddr_storage *)malloc(sizeof(struct sockaddr_storage));
  memcpy(AddressCopy, Res->ai_addr, Res->ai_addrlen);
  socklen_t AddressLength = Res->ai_addrlen;
  freeaddrinfo(Res);

  YonaIoContext *Ctx = (YonaIoContext *)malloc(sizeof(YonaIoContext));
  Ctx->Kind = YonaIoOperationConnect;
  Ctx->FileDescriptor = Fd;
  Ctx->Buffer = (char *)AddressCopy;
  Ctx->BufferSize = 0;
  Ctx->CloseFileDescriptor = 1;

  struct io_uring_sqe Sqe;
  memset(&Sqe, 0, sizeof(Sqe));
  Sqe.opcode = IORING_OP_CONNECT;
  Sqe.fd = Fd;
  Sqe.addr = (unsigned long)AddressCopy;
  Sqe.off = (uint64_t)AddressLength;

  uint64_t Id = YonaRuntimeIoUringSubmit(&Sqe);
  if (Id == 0) {
    YonaRuntimeIoContextCleanupCancelled(Ctx);
    return 0;
  }
  YonaRuntimeIoContextPut(Id, Ctx);
  return (int64_t)Id;
}

int64_t YonaStdNetTcpListen(const char *Host, int64_t Port) {
  struct sockaddr_in Addr;
  memset(&Addr, 0, sizeof(Addr));
  Addr.sin_family = AF_INET;
  Addr.sin_port = htons((uint16_t)Port);
  if (Host && Host[0] != '\0')
    inet_pton(AF_INET, Host, &Addr.sin_addr);
  else
    Addr.sin_addr.s_addr = INADDR_ANY;
  int Fd = socket(AF_INET, SOCK_STREAM, 0);
  if (Fd < 0)
    return -1;
  int Opt = 1;
  setsockopt(Fd, SOL_SOCKET, SO_REUSEADDR, &Opt, sizeof(Opt));
  if (bind(Fd, (struct sockaddr *)&Addr, sizeof(Addr)) < 0) {
    close(Fd);
    return -1;
  }
  if (listen(Fd, 128) < 0) {
    close(Fd);
    return -1;
  }
  return (int64_t)Fd;
}

int64_t YonaStdNetTcpAccept(int64_t ListenerFileDescriptor) {
  struct sockaddr_in *ClientAddress = (struct sockaddr_in *)calloc(
      1, sizeof(struct sockaddr_in) + sizeof(socklen_t));
  socklen_t *AddressLengthPointer =
      (socklen_t *)((char *)ClientAddress + sizeof(struct sockaddr_in));
  *AddressLengthPointer = sizeof(struct sockaddr_in);

  YonaIoContext *Ctx = (YonaIoContext *)malloc(sizeof(YonaIoContext));
  Ctx->Kind = YonaIoOperationAccept;
  Ctx->FileDescriptor = (int)ListenerFileDescriptor;
  Ctx->Buffer = (char *)ClientAddress;
  Ctx->BufferSize = 0;
  Ctx->CloseFileDescriptor = 0;

  struct io_uring_sqe Sqe;
  memset(&Sqe, 0, sizeof(Sqe));
  Sqe.opcode = IORING_OP_ACCEPT;
  Sqe.fd = (int)ListenerFileDescriptor;
  /* Kernel/liburing use addr = sockaddr *, off = socklen_t * (NOT addr2). */
  Sqe.addr = (unsigned long)ClientAddress;
  Sqe.off = (uint64_t)(unsigned long)AddressLengthPointer;

  uint64_t Id = YonaRuntimeIoUringSubmit(&Sqe);
  if (Id == 0) {
    free(ClientAddress);
    free(Ctx);
    return 0;
  }
  YonaRuntimeIoContextPut(Id, Ctx);
  return (int64_t)Id;
}

int64_t YonaStdNetSend(int64_t Fd, const char *Data) {
  size_t Len = strlen(Data);

  /* Copy to RC-managed buffer so it survives until I/O completes */
  char *Pinned = (char *)YonaRuntimeAllocateString(Len + 1);
  memcpy(Pinned, Data, Len + 1);

  YonaIoContext *Ctx = (YonaIoContext *)malloc(sizeof(YonaIoContext));
  Ctx->Kind = YonaIoOperationSend;
  Ctx->FileDescriptor = (int)Fd;
  Ctx->Buffer = Pinned; /* rc_dec'd in completer */
  Ctx->BufferSize = Len;
  Ctx->CloseFileDescriptor = 0;

  struct io_uring_sqe Sqe;
  memset(&Sqe, 0, sizeof(Sqe));
  Sqe.opcode = IORING_OP_SEND;
  Sqe.fd = (int)Fd;
  Sqe.addr = (unsigned long)Pinned;
  Sqe.len = (unsigned)Len;

  uint64_t Id = YonaRuntimeIoUringSubmit(&Sqe);
  if (Id == 0) {
    YonaRuntimeRelease(Pinned);
    free(Ctx);
    return 0;
  }
  YonaRuntimeIoContextPut(Id, Ctx);
  return (int64_t)Id;
}

int64_t YonaStdNetRecv(int64_t Fd, int64_t MaximumBytes) {
  if (MaximumBytes <= 0)
    MaximumBytes = 4096;
  char *Buf = (char *)YonaRuntimeAllocateString((size_t)MaximumBytes + 1);

  YonaIoContext *Ctx = (YonaIoContext *)malloc(sizeof(YonaIoContext));
  Ctx->Kind = YonaIoOperationReceive;
  Ctx->FileDescriptor = (int)Fd;
  Ctx->Buffer = Buf;
  Ctx->BufferSize = (size_t)MaximumBytes;
  Ctx->CloseFileDescriptor = 0;

  struct io_uring_sqe Sqe;
  memset(&Sqe, 0, sizeof(Sqe));
  Sqe.opcode = IORING_OP_RECV;
  Sqe.fd = (int)Fd;
  Sqe.addr = (unsigned long)Buf;
  Sqe.len = (unsigned)MaximumBytes;

  uint64_t Id = YonaRuntimeIoUringSubmit(&Sqe);
  if (Id == 0) {
    YonaRuntimeIoContextCleanupCancelled(Ctx);
    return 0;
  }
  YonaRuntimeIoContextPut(Id, Ctx);
  return (int64_t)Id;
}

/* sendBytes: send Bytes buffer via io_uring */
int64_t YonaStdNetSendBytes(int64_t Fd, void *Bytes) {
  int64_t *B = (int64_t *)Bytes;
  int64_t Len = B[0];
  uint8_t *Data = (uint8_t *)(B + 1);
  /* Pin the Bytes buffer until I/O completes */
  YonaRuntimeRetain(Bytes);

  YonaIoContext *Ctx = (YonaIoContext *)malloc(sizeof(YonaIoContext));
  Ctx->Kind = YonaIoOperationSend;
  Ctx->FileDescriptor = (int)Fd;
  Ctx->Buffer = (char *)Bytes; /* store for rc_dec in completer */
  Ctx->BufferSize = (size_t)Len;
  Ctx->CloseFileDescriptor = 0;

  struct io_uring_sqe Sqe;
  memset(&Sqe, 0, sizeof(Sqe));
  Sqe.opcode = IORING_OP_SEND;
  Sqe.fd = (int)Fd;
  Sqe.addr = (unsigned long)Data;
  Sqe.len = (unsigned)Len;

  uint64_t Id = YonaRuntimeIoUringSubmit(&Sqe);
  if (Id == 0) {
    YonaRuntimeRelease(Bytes);
    free(Ctx);
    return 0;
  }
  YonaRuntimeIoContextPut(Id, Ctx);
  return (int64_t)Id;
}

/* recvBytes: receive into Bytes buffer via io_uring */
int64_t YonaStdNetRecvBytes(int64_t Fd, int64_t MaximumBytes) {
  if (MaximumBytes <= 0)
    MaximumBytes = 4096;
  /* Allocate Bytes buffer: [length][data...] with RC header */
  int64_t *Buf = (int64_t *)YonaRuntimeAllocate(
      8 /* RC_TYPE_BYTE_ARRAY */, sizeof(int64_t) + (size_t)MaximumBytes);
  Buf[0] = 0; /* length set by completer */

  YonaIoContext *Ctx = (YonaIoContext *)malloc(sizeof(YonaIoContext));
  Ctx->Kind = YonaIoOperationReceiveBytes;
  Ctx->FileDescriptor = (int)Fd;
  Ctx->Buffer = (char *)Buf;
  Ctx->BufferSize = (size_t)MaximumBytes;
  Ctx->CloseFileDescriptor = 0;

  struct io_uring_sqe Sqe;
  memset(&Sqe, 0, sizeof(Sqe));
  Sqe.opcode = IORING_OP_RECV;
  Sqe.fd = (int)Fd;
  Sqe.addr = (unsigned long)(uint8_t *)(Buf + 1); /* data starts after length */
  Sqe.len = (unsigned)MaximumBytes;

  uint64_t Id = YonaRuntimeIoUringSubmit(&Sqe);
  if (Id == 0) {
    YonaRuntimeIoContextCleanupCancelled(Ctx);
    return 0;
  }
  YonaRuntimeIoContextPut(Id, Ctx);
  return (int64_t)Id;
}

int64_t YonaStdNetClose(int64_t Fd) {
  close((int)Fd);
  return 0;
}

/* ===== HTTP GET via io_uring ===== */

extern const char *YonaStdHttpBuildRequest(const char *Method, const char *Host,
                                           const char *Path, const char *Body);
extern int64_t *YonaStdHttpParseUrl(const char *Url);
#define YONA_RC_TYPE_STRING 6

int64_t YonaStdHttpHttpGet(const char *Url) {
  int64_t *Parsed = YonaStdHttpParseUrl(Url);
  const char *Host = (const char *)(intptr_t)YonaRuntimeAdtGetField(Parsed, 0);
  int64_t Port = YonaRuntimeAdtGetField(Parsed, 1);
  const char *Path = (const char *)(intptr_t)YonaRuntimeAdtGetField(Parsed, 2);

  struct addrinfo Hints, *Ai;
  memset(&Hints, 0, sizeof(Hints));
  Hints.ai_family = AF_INET;
  Hints.ai_socktype = SOCK_STREAM;
  char PortString[8];
  snprintf(PortString, sizeof(PortString), "%" PRId64, Port);
  if (getaddrinfo(Host, PortString, &Hints, &Ai) != 0) {
    YonaRuntimeRelease(Parsed);
    return 0;
  }
  int Fd = socket(Ai->ai_family, Ai->ai_socktype, Ai->ai_protocol);
  if (Fd < 0) {
    freeaddrinfo(Ai);
    YonaRuntimeRelease(Parsed);
    return 0;
  }

  struct sockaddr_storage AddressBuffer;
  memcpy(&AddressBuffer, Ai->ai_addr, Ai->ai_addrlen);
  socklen_t AddressLength = Ai->ai_addrlen;
  freeaddrinfo(Ai);
  {
    struct io_uring_sqe Sqe;
    memset(&Sqe, 0, sizeof(Sqe));
    Sqe.opcode = IORING_OP_CONNECT;
    Sqe.fd = Fd;
    Sqe.addr = (unsigned long)&AddressBuffer;
    Sqe.off = (uint64_t)AddressLength;
    uint64_t Id = YonaRuntimeIoUringSubmit(&Sqe);
    if (YonaRuntimeIoUringAwait(Id) < 0) {
      close(Fd);
      YonaRuntimeRelease(Parsed);
      return 0;
    }
  }

  const char *Req = YonaStdHttpBuildRequest("GET", Host, Path, NULL);
  YonaRuntimeRelease(Parsed);
  {
    struct io_uring_sqe Sqe;
    memset(&Sqe, 0, sizeof(Sqe));
    Sqe.opcode = IORING_OP_SEND;
    Sqe.fd = Fd;
    Sqe.addr = (unsigned long)Req;
    Sqe.len = (unsigned)strlen(Req);
    uint64_t Id = YonaRuntimeIoUringSubmit(&Sqe);
    YonaRuntimeIoUringAwait(Id);
  }
  YonaRuntimeRelease((void *)Req);

  size_t BufferSize = 16384;
  size_t Total = 0;
  char *Buf = (char *)YonaRuntimeAllocate(YONA_RC_TYPE_STRING, BufferSize);
  for (;;) {
    struct io_uring_sqe Sqe;
    memset(&Sqe, 0, sizeof(Sqe));
    Sqe.opcode = IORING_OP_RECV;
    Sqe.fd = Fd;
    Sqe.addr = (unsigned long)(Buf + Total);
    Sqe.len = (unsigned)(BufferSize - Total - 1);
    uint64_t Id = YonaRuntimeIoUringSubmit(&Sqe);
    int32_t N = YonaRuntimeIoUringAwait(Id);
    if (N <= 0)
      break;
    Total += (size_t)N;
    if (Total >= BufferSize - 256) {
      size_t NewSize = BufferSize * 2;
      char *NewBuffer =
          (char *)YonaRuntimeAllocate(YONA_RC_TYPE_STRING, NewSize);
      memcpy(NewBuffer, Buf, Total);
      YonaRuntimeRelease(Buf);
      Buf = NewBuffer;
      BufferSize = NewSize;
    }
  }
  Buf[Total] = '\0';
  close(Fd);

  YonaIoContext *Ctx = (YonaIoContext *)malloc(sizeof(YonaIoContext));
  Ctx->Kind = YonaIoOperationReadFile;
  Ctx->FileDescriptor = -1;
  Ctx->Buffer = Buf;
  Ctx->BufferSize = Total;
  Ctx->CloseFileDescriptor = 0;
  struct io_uring_sqe Sqe;
  memset(&Sqe, 0, sizeof(Sqe));
  Sqe.opcode = IORING_OP_NOP;
  uint64_t NoOperationId = YonaRuntimeIoUringSubmit(&Sqe);
  if (NoOperationId == 0) {
    YonaRuntimeIoContextCleanupCancelled(Ctx);
    return 0;
  }
  YonaRuntimeIoContextPut(NoOperationId, Ctx);
  return (int64_t)NoOperationId;
}

/* ===== UDP (sync — datagram ops are fast) ===== */

int64_t YonaStdNetUdpBind(const char *Host, int64_t Port) {
  struct sockaddr_in Addr;
  memset(&Addr, 0, sizeof(Addr));
  Addr.sin_family = AF_INET;
  Addr.sin_port = htons((uint16_t)Port);
  if (Host && Host[0] != '\0')
    inet_pton(AF_INET, Host, &Addr.sin_addr);
  else
    Addr.sin_addr.s_addr = INADDR_ANY;
  int Fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (Fd < 0)
    return -1;
  if (bind(Fd, (struct sockaddr *)&Addr, sizeof(Addr)) < 0) {
    close(Fd);
    return -1;
  }
  return (int64_t)Fd;
}

int64_t YonaStdNetUdpSendTo(int64_t Fd, const char *Host, int64_t Port,
                            const char *Data) {
  struct sockaddr_in Addr;
  memset(&Addr, 0, sizeof(Addr));
  Addr.sin_family = AF_INET;
  Addr.sin_port = htons((uint16_t)Port);
  inet_pton(AF_INET, Host, &Addr.sin_addr);
  return (int64_t)sendto((int)Fd, Data, strlen(Data), 0,
                         (struct sockaddr *)&Addr, sizeof(Addr));
}

int64_t YonaStdNetUdpRecv(int64_t Fd, int64_t MaximumBytes) {
  if (MaximumBytes <= 0)
    MaximumBytes = 4096;
  char *Buf = (char *)YonaRuntimeAllocateString((size_t)MaximumBytes + 1);
  struct sockaddr_in From;
  socklen_t SourceAddressLength = sizeof(From);
  ssize_t N = recvfrom((int)Fd, Buf, (size_t)MaximumBytes, 0,
                       (struct sockaddr *)&From, &SourceAddressLength);
  if (N <= 0) {
    Buf[0] = '\0';
    return (int64_t)(intptr_t)Buf;
  }
  Buf[N] = '\0';
  return (int64_t)(intptr_t)Buf;
}

const char *YonaStdNetPeerAddress(int64_t Fd) {
  struct sockaddr_in Addr;
  socklen_t AddressLength = sizeof(Addr);
  if (getpeername((int)Fd, (struct sockaddr *)&Addr, &AddressLength) < 0) {
    char *R = (char *)YonaRuntimeAllocateString(8);
    memcpy(R, "unknown", 8);
    return R;
  }
  char Ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &Addr.sin_addr, Ip, sizeof(Ip));
  char *R = (char *)YonaRuntimeAllocateString(64);
  snprintf(R, 64, "%s:%d", Ip, ntohs(Addr.sin_port));
  return R;
}
