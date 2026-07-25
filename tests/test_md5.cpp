#include "test_harness.h"
#include "md5.h"

void run_md5_tests()
{
	test_section("md5");

	// RFC 1321 / common vectors
	MD5 m1("abc");
	CHECK(m1.toString() == "900150983cd24fb0d6963f7d28e17f72");

	MD5 m2("");
	CHECK(m2.toString() == "d41d8cd98f00b204e9800998ecf8427e");

	MD5 m3("message digest");
	CHECK(m3.toString() == "f96b697d7cb7938d525a2f31aaf161d0");
}
