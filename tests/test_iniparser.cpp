#include "test_harness.h"
#include "iniparser.h"
#include <cstdio>

void run_iniparser_tests()
{
	test_section("iniparser");

	const char* path = "/tmp/commtools_test.ini";
	FILE* fp = fopen(path, "w");
	CHECK(fp != NULL);
	if (!fp) return;
	fputs("[server]\n", fp);
	fputs("host = 127.0.0.1\n", fp);
	fputs("port = 8080\n", fp);
	fputs("enabled = 1\n", fp);
	fclose(fp);

	dictionary* d = iniparser_load(path);
	CHECK(d != NULL);
	if (!d) return;

	CHECK(strcmp(iniparser_getstring(d, "server:host", "x"), "127.0.0.1") == 0);
	CHECK(iniparser_getint(d, "server:port", -1) == 8080);
	CHECK(iniparser_getboolean(d, "server:enabled", 0) == 1);
	CHECK(strcmp(iniparser_getstring(d, "server:missing", "default"), "default") == 0);

	iniparser_freedict(d);
	remove(path);
}
