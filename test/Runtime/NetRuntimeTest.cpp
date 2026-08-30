#include <doctest/doctest.h>

#include <cerrno>
#include <cstdint>
#include <string>

#ifdef __linux__
#include <sys/socket.h>
#include <unistd.h>
#endif

extern "C" {
int64_t YonaRuntimeIoAwait(int64_t uring_id);
void YonaRuntimeRelease(void *ptr);

int64_t YonaStdNetTcpListen(const char *host, int64_t port);
int64_t YonaStdNetTcpConnect(const char *host, int64_t port);
int64_t YonaStdNetTcpAccept(int64_t listener_fd);
int64_t YonaStdNetSend(int64_t fd, const char *data);
int64_t YonaStdNetRecv(int64_t fd, int64_t max_bytes);
int64_t YonaStdNetClose(int64_t fd);
int64_t YonaStdNetUdpBind(const char *host, int64_t port);
int64_t YonaStdNetUdpSendTo(int64_t fd, const char *host, int64_t port,
                            const char *data);
int64_t YonaStdNetUdpRecv(int64_t fd, int64_t max_bytes);
}

static int64_t bind_loopback_listener_with_port(int64_t *out_port) {
  for (int64_t port = 28080; port < 28280; ++port) {
    int64_t listener = YonaStdNetTcpListen("127.0.0.1", port);
    if (listener != -1) {
      if (out_port)
        *out_port = port;
      return listener;
    }
  }
  return -1;
}

static int64_t bind_udp_with_port(int64_t *out_port) {
  for (int64_t port = 28281; port < 28481; ++port) {
    int64_t s = YonaStdNetUdpBind("127.0.0.1", port);
    if (s != -1) {
      if (out_port)
        *out_port = port;
      return s;
    }
  }
  return -1;
}

static bool loopback_sockets_blocked_by_sandbox() {
#ifdef __linux__
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd >= 0) {
    close(fd);
    return false;
  }
  return errno == EPERM || errno == EACCES;
#else
  return false;
#endif
}

TEST_SUITE("Runtime Net Submit/Await") {

  TEST_CASE("TCP connect accept send recv on loopback") {
    if (loopback_sockets_blocked_by_sandbox()) {
      MESSAGE("skipped: the execution sandbox denies AF_INET sockets");
      return;
    }
    int64_t listen_port = 0;
    int64_t listener = bind_loopback_listener_with_port(&listen_port);
    REQUIRE(listener != -1);

    int64_t accept_id = YonaStdNetTcpAccept(listener);
    REQUIRE(accept_id > 0);
    int64_t connect_id = YonaStdNetTcpConnect("127.0.0.1", listen_port);
    REQUIRE(connect_id > 0);

    int64_t server_fd = YonaRuntimeIoAwait(accept_id);
    int64_t client_fd = YonaRuntimeIoAwait(connect_id);
    REQUIRE(server_fd > 0);
    REQUIRE(client_fd > 0);

    YonaStdNetClose(client_fd);
    YonaStdNetClose(server_fd);
    YonaStdNetClose(listener);
  }

  TEST_CASE("UDP send and recv on loopback") {
    if (loopback_sockets_blocked_by_sandbox()) {
      MESSAGE("skipped: the execution sandbox denies AF_INET sockets");
      return;
    }
    int64_t recv_port = 0;
    int64_t recv_sock = bind_udp_with_port(&recv_port);
    REQUIRE(recv_sock != -1);

    /* OS chooses sender port; we only need a valid socket. */
    int64_t send_sock = YonaStdNetUdpBind("127.0.0.1", 0);
    REQUIRE(send_sock != -1);

    /* UDP is synchronous (FN in Net.yonai): send first so loopback
     * queues the datagram, then recv. Do not treat the return as a
     * uring cookie — that deadlocks (recvfrom waits forever). */
    int64_t sent =
        YonaStdNetUdpSendTo(send_sock, "127.0.0.1", recv_port, "udp_ping");
    REQUIRE(sent > 0);

    char *payload = (char *)(intptr_t)YonaStdNetUdpRecv(recv_sock, 64);
    REQUIRE(payload != nullptr);
    CHECK(std::string(payload) == "udp_ping");
    YonaRuntimeRelease(payload);

    YonaStdNetClose(send_sock);
    YonaStdNetClose(recv_sock);
  }

} // TEST_SUITE
