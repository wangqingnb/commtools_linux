#include "test_harness.h"
#include <cstdio>

int main()
{
	std::printf("CommTools test suite\n");

	run_commroute_tests();
	run_md5_tests();
	run_des_tests();
	run_ptrqueue_tests();
	run_memlog_tests();
	run_iso8583_tests();
	run_iniparser_tests();

	std::printf("\n==== SUMMARY ====\n");
	std::printf("passed: %d\n", g_stats().passed);
	std::printf("failed: %d\n", g_stats().failed);
	if (!g_stats().failures.empty()) {
		std::printf("failures:\n");
		for (size_t i = 0; i < g_stats().failures.size(); i++)
			std::printf("  - %s\n", g_stats().failures[i].c_str());
	}

	return g_stats().failed == 0 ? 0 : 1;
}
