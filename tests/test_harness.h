#pragma once
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct TestStats {
	int passed;
	int failed;
	std::vector<std::string> failures;
	TestStats() : passed(0), failed(0) {}
};

inline TestStats& g_stats() {
	static TestStats s;
	return s;
}

inline void test_check(bool cond, const char* expr, const char* file, int line) {
	if (cond) {
		g_stats().passed++;
		std::printf("  [PASS] %s\n", expr);
	} else {
		g_stats().failed++;
		char buf[512];
		std::snprintf(buf, sizeof(buf), "%s:%d  CHECK(%s)", file, line, expr);
		g_stats().failures.push_back(buf);
		std::printf("  [FAIL] %s\n", buf);
	}
}

#define CHECK(expr) test_check(!!(expr), #expr, __FILE__, __LINE__)

inline void test_section(const char* name) {
	std::printf("\n== %s ==\n", name);
}

void run_commroute_tests();
void run_md5_tests();
void run_des_tests();
void run_ptrqueue_tests();
void run_iso8583_tests();
void run_iniparser_tests();
void run_memlog_tests();
void run_tcpserver_tests();
