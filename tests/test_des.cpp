#include "test_harness.h"
#include "des.h"
#include <cstring>

void run_des_tests()
{
	test_section("des");

	U_CHAR key[8] = {0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF};
	U_CHAR plain[16] = {
		0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
		0xFE,0xDC,0xBA,0x98,0x76,0x54,0x32,0x10
	};
	U_CHAR cipher[16] = {0};
	U_CHAR out[16] = {0};

	CHECK(DesEncry(plain, key, cipher, 16, Single) == true);
	CHECK(memcmp(plain, cipher, 16) != 0);

	CHECK(DesDecry(cipher, key, out, 16, Single) == true);
	CHECK(memcmp(plain, out, 16) == 0);
}
