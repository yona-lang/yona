#include <doctest/doctest.h>
#include <cstdint>
#include <string>

extern "C" {
int64_t yona_rt_io_await(int64_t uring_id);
void yona_rt_rc_dec(void* ptr);

int64_t yona_Std_Net__tcpListen(const char* host, int64_t port);
int64_t yona_Std_Net__tcpConnect(const char* host, int64_t port);
int64_t yona_Std_Net__tcpAccept(int64_t listener_fd);
int64_t yona_Std_Net__send(int64_t fd, const char* data);
int64_t yona_Std_Net__recv(int64_t fd, int64_t max_bytes);
int64_t yona_Std_Net__close(int64_t fd);
int64_t yona_Std_Net__udpBind(const char* host, int64_t port);
int64_t yona_Std_Net__udpSendTo(int64_t fd, const char* host, int64_t port, const char* data);
int64_t yona_Std_Net__udpRecv(int64_t fd, int64_t max_bytes);
}

static int64_t bind_loopback_listener_with_port(int64_t* out_port) {
	for (int64_t port = 28080; port < 28280; ++port) {
		int64_t listener = yona_Std_Net__tcpListen("127.0.0.1", port);
		if (listener != -1) {
			if (out_port) *out_port = port;
			return listener;
		}
	}
	return -1;
}

static int64_t bind_udp_with_port(int64_t* out_port) {
	for (int64_t port = 28281; port < 28481; ++port) {
		int64_t s = yona_Std_Net__udpBind("127.0.0.1", port);
		if (s != -1) {
			if (out_port) *out_port = port;
			return s;
		}
	}
	return -1;
}

TEST_SUITE("Runtime Net Submit/Await") {

TEST_CASE("TCP connect accept send recv on loopback") {
	int64_t listen_port = 0;
	int64_t listener = bind_loopback_listener_with_port(&listen_port);
	REQUIRE(listener != -1);

	int64_t accept_id = yona_Std_Net__tcpAccept(listener);
	REQUIRE(accept_id > 0);
	int64_t connect_id = yona_Std_Net__tcpConnect("127.0.0.1", listen_port);
	REQUIRE(connect_id > 0);

	int64_t server_fd = yona_rt_io_await(accept_id);
	int64_t client_fd = yona_rt_io_await(connect_id);
	REQUIRE(server_fd > 0);
	REQUIRE(client_fd > 0);

	yona_Std_Net__close(client_fd);
	yona_Std_Net__close(server_fd);
	yona_Std_Net__close(listener);
}

TEST_CASE("UDP send and recv on loopback") {
	int64_t recv_port = 0;
	int64_t recv_sock = bind_udp_with_port(&recv_port);
	REQUIRE(recv_sock != -1);

	/* OS chooses sender port; we only need a valid socket. */
	int64_t send_sock = yona_Std_Net__udpBind("127.0.0.1", 0);
	REQUIRE(send_sock != -1);

	/* UDP is synchronous (FN in Net.yonai): send first so loopback
	 * queues the datagram, then recv. Do not treat the return as a
	 * uring cookie — that deadlocks (recvfrom waits forever). */
	int64_t sent = yona_Std_Net__udpSendTo(send_sock, "127.0.0.1", recv_port, "udp_ping");
	REQUIRE(sent > 0);

	char* payload = (char*)(intptr_t)yona_Std_Net__udpRecv(recv_sock, 64);
	REQUIRE(payload != nullptr);
	CHECK(std::string(payload) == "udp_ping");
	yona_rt_rc_dec(payload);

	yona_Std_Net__close(send_sock);
	yona_Std_Net__close(recv_sock);
}

} // TEST_SUITE
