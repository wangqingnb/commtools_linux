#include "test_harness.h"
#include "commroute.h"
#include <fstream>

void run_commroute_tests()
{
	test_section("commroute");

	char hex[32] = {0};
	char bin[16] = {0};
	unsigned char src[] = {0x01, 0x23, 0xAB, 0xCD};
	BinToHex((char*)src, hex, 4);
	CHECK(std::string(hex, 8) == "0123ABCD");

	int n = HexToBin(hex, bin, 4);
	CHECK(n == 4);
	CHECK(memcmp(bin, src, 4) == 0);

	unsigned char bcd[] = {0x12, 0x34};
	CHECK(BCDToDec((const char*)bcd, 2) == 1234u);

	CHECK(Str_TrimA("  hello  ") == "hello");
	CHECK(Str_TrimA("nospace") == "nospace");
	CHECK(Str_TrimA("   ") == "   "); // find_first_not_of fails => return original

	unsigned long long t1 = GetTickCount64();
	unsigned long long t2 = GetTickCount64();
	CHECK(t2 >= t1);

	std::string dt = GetSysDateTimeStr();
	CHECK(dt.size() > 10);
	CHECK(dt[0] == '[');

	std::string ds = GetDateStr();
	CHECK(ds.size() == 8);

	CHECK(FileExists("/etc/hosts") == true);
	CHECK(FileExists("/tmp/commtools_no_such_file_xyz") == false);

	char path[512] = {0};
	char* p = GetExePath(path, (int)sizeof(path));
	CHECK(p != NULL);
	CHECK(strlen(path) > 0);
}
