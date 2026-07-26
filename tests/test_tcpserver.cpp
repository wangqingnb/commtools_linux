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

namespace {

volatile int g_connected = 0;
volatile long g_last_id = 0;

void on_connected(long id, void* /*pClass*/)
{
	g_last_id = id;
	__sync_add_and_fetch(&g_connected, 1);
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

bool connect_ipv4(const char* ip, WORD port, int timeout_ms)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return false;

	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
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
	// 仅切地址族：默认 0.0.0.0 应同步为 ::，从而能接受 ::1 连接
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

	// 纯 IPv6（绑 ::1 + V6ONLY）：同端口 IPv4 环回不应连上
	bool ok4 = connect_ipv4("127.0.0.1", port, 500);
	CHECK(!ok4);

	if (started)
		server.StopServer();
}

} // namespace

void run_tcpserver_tests()
{
	test_ipv4_listen_accept();
	test_ipv6_listen_accept();
	test_ipv6_default_any_bind();
}
