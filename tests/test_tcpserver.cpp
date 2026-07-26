#include "test_harness.h"
#include "TCPServer.h"
#include "RK_Exception.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <vector>

namespace {

volatile int g_connected = 0;
volatile long g_last_id = 0;
volatile int g_send_done = 0;
volatile long g_send_len = 0;
volatile long g_send_exp = 0;

void on_connected(long id, void* /*pClass*/)
{
	g_last_id = id;
	__sync_add_and_fetch(&g_connected, 1);
}

void on_send_completed(long /*id*/, char* /*buf*/, long len, long expLen, void* /*pClass*/)
{
	g_send_len = len;
	g_send_exp = expLen;
	__sync_add_and_fetch(&g_send_done, 1);
}

WORD test_port_base()
{
	return (WORD)(29000 + (getpid() % 500));
}

bool ipv6_loopback_available()
{
	int s = socket(AF_INET6, SOCK_STREAM, 0);
	if (s < 0)
		return false;
	struct sockaddr_in6 addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin6_family = AF_INET6;
	addr.sin6_addr = in6addr_loopback;
	addr.sin6_port = 0;
	bool ok = (bind(s, (struct sockaddr*)&addr, sizeof(addr)) == 0);
	close(s);
	return ok;
}

int connect_ipv4_fd(const char* ip, WORD port, int timeout_ms)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;

	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
		close(fd);
		return -1;
	}

	int rc = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
	if (rc != 0 && errno != EINPROGRESS) {
		close(fd);
		return -1;
	}
	if (rc != 0) {
		fd_set wfds;
		FD_ZERO(&wfds);
		FD_SET(fd, &wfds);
		struct timeval tv;
		tv.tv_sec = timeout_ms / 1000;
		tv.tv_usec = (timeout_ms % 1000) * 1000;
		rc = select(fd + 1, NULL, &wfds, NULL, &tv);
		if (rc <= 0) {
			close(fd);
			return -1;
		}
		int soerr = 0;
		socklen_t len = sizeof(soerr);
		getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len);
		if (soerr != 0) {
			close(fd);
			return -1;
		}
	}
	fcntl(fd, F_SETFL, flags);
	return fd;
}

bool connect_ipv4(const char* ip, WORD port, int timeout_ms)
{
	int fd = connect_ipv4_fd(ip, port, timeout_ms);
	if (fd < 0)
		return false;
	close(fd);
	return true;
}

bool connect_ipv6(const char* ip, WORD port, int timeout_ms)
{
	int fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (fd < 0)
		return false;

	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	struct sockaddr_in6 addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(port);
	if (inet_pton(AF_INET6, ip, &addr.sin6_addr) != 1) {
		close(fd);
		return false;
	}

	int rc = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
	if (rc == 0) {
		close(fd);
		return true;
	}
	if (errno != EINPROGRESS) {
		close(fd);
		return false;
	}

	fd_set wfds;
	FD_ZERO(&wfds);
	FD_SET(fd, &wfds);
	struct timeval tv;
	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;
	rc = select(fd + 1, NULL, &wfds, NULL, &tv);
	if (rc <= 0) {
		close(fd);
		return false;
	}
	int soerr = 0;
	socklen_t len = sizeof(soerr);
	getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len);
	close(fd);
	return soerr == 0;
}

bool wait_connected(int expect, int timeout_ms)
{
	int waited = 0;
	while (waited < timeout_ms) {
		if (__sync_fetch_and_add(&g_connected, 0) >= expect)
			return true;
		usleep(20 * 1000);
		waited += 20;
	}
	return __sync_fetch_and_add(&g_connected, 0) >= expect;
}

bool wait_send_done(int timeout_ms)
{
	int waited = 0;
	while (waited < timeout_ms) {
		if (__sync_fetch_and_add(&g_send_done, 0) >= 1)
			return true;
		usleep(20 * 1000);
		waited += 20;
	}
	return __sync_fetch_and_add(&g_send_done, 0) >= 1;
}

bool recv_all(int fd, char* buf, int need, int timeout_ms)
{
	int got = 0;
	int waited = 0;
	while (got < need && waited < timeout_ms) {
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 50 * 1000;
		int rc = select(fd + 1, &rfds, NULL, NULL, &tv);
		if (rc < 0)
			return false;
		if (rc == 0) {
			waited += 50;
			continue;
		}
		int n = (int)recv(fd, buf + got, need - got, 0);
		if (n == 0)
			return false;
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		got += n;
	}
	return got == need;
}

void test_ipv6_default_any_bind()
{
	test_section("TCPServer IPv6 default :: bind");

	if (!ipv6_loopback_available()) {
		std::printf("  [SKIP] IPv6 loopback not available\n");
		return;
	}

	WORD port = (WORD)(test_port_base() + 2);
	g_connected = 0;

	TCPServer server;
	server.SetAddressFamily(AF_INET6);
	server.SetNetPort(port);
	server.SetMaxConnectNum(4);
	server.RegCallback_OnConnected(NULL, on_connected);

	bool started = false;
	try {
		server.StartServer();
		started = true;
	} catch (CRK_Exception& ex) {
		std::printf("  [INFO] IPv6 default bind StartServer failed: %s\n", ex.getMsg());
		CHECK(false);
		return;
	}

	usleep(50 * 1000);
	CHECK(connect_ipv6("::1", port, 1000));
	CHECK(wait_connected(1, 2000));

	if (started)
		server.StopServer();
}

void test_ipv4_listen_accept()
{
	test_section("TCPServer IPv4");

	WORD port = test_port_base();
	g_connected = 0;
	g_last_id = 0;

	TCPServer server;
	server.SetAddressFamily(AF_INET);
	server.SetLocalServerIP("127.0.0.1");
	server.SetNetPort(port);
	server.SetMaxConnectNum(4);
	server.RegCallback_OnConnected(NULL, on_connected);

	bool started = false;
	try {
		server.StartServer();
		started = true;
	} catch (CRK_Exception& ex) {
		std::printf("  [INFO] IPv4 StartServer failed: %s\n", ex.getMsg());
		CHECK(false);
		return;
	}

	usleep(50 * 1000);
	bool ok = connect_ipv4("127.0.0.1", port, 1000);
	CHECK(ok);
	CHECK(wait_connected(1, 2000));
	CHECK(g_last_id > 0);

	if (started)
		server.StopServer();
}

void test_ipv6_listen_accept()
{
	test_section("TCPServer IPv6");

	if (!ipv6_loopback_available()) {
		std::printf("  [SKIP] IPv6 loopback not available\n");
		return;
	}

	WORD port = (WORD)(test_port_base() + 1);
	g_connected = 0;
	g_last_id = 0;

	TCPServer server;
	server.SetAddressFamily(AF_INET6);
	server.SetLocalServerIP("::1");
	server.SetNetPort(port);
	server.SetMaxConnectNum(4);
	server.RegCallback_OnConnected(NULL, on_connected);

	bool started = false;
	try {
		server.StartServer();
		started = true;
	} catch (CRK_Exception& ex) {
		std::printf("  [INFO] IPv6 StartServer failed: %s\n", ex.getMsg());
		CHECK(false);
		return;
	}

	usleep(50 * 1000);
	bool ok6 = connect_ipv6("::1", port, 1000);
	CHECK(ok6);
	CHECK(wait_connected(1, 2000));
	CHECK(g_last_id > 0);

	bool ok4 = connect_ipv4("127.0.0.1", port, 500);
	CHECK(!ok4);

	if (started)
		server.StopServer();
}

void test_burst_accept()
{
	test_section("TCPServer burst accept");

	WORD port = (WORD)(test_port_base() + 3);
	const int N = 8;
	g_connected = 0;

	TCPServer server;
	server.SetAddressFamily(AF_INET);
	server.SetLocalServerIP("127.0.0.1");
	server.SetNetPort(port);
	server.SetMaxConnectNum((WORD)N);
	server.RegCallback_OnConnected(NULL, on_connected);

	bool started = false;
	try {
		server.StartServer();
		started = true;
	} catch (CRK_Exception& ex) {
		std::printf("  [INFO] burst StartServer failed: %s\n", ex.getMsg());
		CHECK(false);
		return;
	}

	usleep(50 * 1000);
	std::vector<int> fds;
	for (int i = 0; i < N; i++) {
		int fd = connect_ipv4_fd("127.0.0.1", port, 1000);
		CHECK(fd >= 0);
		if (fd >= 0)
			fds.push_back(fd);
	}
	CHECK(wait_connected(N, 3000));
	CHECK(__sync_fetch_and_add(&g_connected, 0) == N);

	for (size_t i = 0; i < fds.size(); i++)
		close(fds[i]);
	if (started)
		server.StopServer();
}

void test_post_send()
{
	test_section("TCPServer PostSend");

	WORD port = (WORD)(test_port_base() + 4);
	g_connected = 0;
	g_last_id = 0;
	g_send_done = 0;
	g_send_len = 0;
	g_send_exp = 0;

	TCPServer server;
	server.SetAddressFamily(AF_INET);
	server.SetLocalServerIP("127.0.0.1");
	server.SetNetPort(port);
	server.SetMaxConnectNum(4);
	server.RegCallback_OnConnected(NULL, on_connected);
	server.RegCallback_OnSendCompleted(NULL, on_send_completed);

	bool started = false;
	try {
		server.StartServer();
		started = true;
	} catch (CRK_Exception& ex) {
		std::printf("  [INFO] PostSend StartServer failed: %s\n", ex.getMsg());
		CHECK(false);
		return;
	}

	usleep(50 * 1000);
	int cfd = connect_ipv4_fd("127.0.0.1", port, 1000);
	CHECK(cfd >= 0);
	CHECK(wait_connected(1, 2000));

	const int payload_len = 4096;
	char payload[4096];
	for (int i = 0; i < payload_len; i++)
		payload[i] = (char)(i & 0xFF);

	CHECK(server.PostSend(g_last_id, payload, (DWORD)payload_len));

	// 先读对端，避免发送窗口堵死导致续传无法完成
	char got[4096];
	bool got_all = recv_all(cfd, got, payload_len, 5000);
	CHECK(wait_send_done(5000));
	CHECK(got_all);
	CHECK(g_send_exp == payload_len);
	CHECK(g_send_len == payload_len);
	CHECK(memcmp(got, payload, payload_len) == 0);

	close(cfd);
	if (started)
		server.StopServer();
}

} // namespace

void run_tcpserver_tests()
{
	test_ipv4_listen_accept();
	test_ipv6_listen_accept();
	test_ipv6_default_any_bind();
	test_burst_accept();
	test_post_send();
}
