#include "test_harness.h"
#include "pkg8583.h"
#include "unpkg.h"
#include "RK_Exception.h"
#include <cstring>

static void fill_formats(TFieldFormat fmts[FIELD_COUNT_MAX])
{
	memset(fmts, 0, sizeof(TFieldFormat) * FIELD_COUNT_MAX);

	// Field 2: LLVAR ASCII numeric, max 19
	fmts[1].Defined = true;
	fmts[1].LenType = 1;
	fmts[1].LenFormat = 1;
	fmts[1].MaxLen = 19;
	fmts[1].MinLen = 1;
	fmts[1].ContentType = 2;

	// Field 3: fixed 6
	fmts[2].Defined = true;
	fmts[2].LenType = 0;
	fmts[2].MaxLen = 6;
	fmts[2].ContentType = 2;

	// Field 4: fixed 12
	fmts[3].Defined = true;
	fmts[3].LenType = 0;
	fmts[3].MaxLen = 12;
	fmts[3].ContentType = 2;

	// Field 11: fixed 6
	fmts[10].Defined = true;
	fmts[10].LenType = 0;
	fmts[10].MaxLen = 6;
	fmts[10].ContentType = 2;
}

void run_iso8583_tests()
{
	test_section("iso8583");

	// ParseFieldFormat
	TFieldFormat ff;
	CHECK(ParseFieldFormat(ff, "0,0,ProcCode,0, ,0,6,2") == true);
	CHECK(ff.Defined == true);
	CHECK(ff.LenType == 0);
	CHECK(ff.MaxLen == 6);
	CHECK(ff.ContentType == 2);

	CHECK(ParseFieldFormat(ff, "1,1,PAN,0, ,1,19,2") == true);
	CHECK(ff.LenType == 1);
	CHECK(ff.LenFormat == 1);
	CHECK(ff.MaxLen == 19);

	// SetData bounds
	{
		T8583Field field;
		bool threw = false;
		try {
			char big[FIELD_DATA_MAX_SIZE + 8];
			memset(big, 'A', sizeof(big));
			field.SetData(big, FIELD_DATA_MAX_SIZE + 1);
		} catch (CRK_Exception&) {
			threw = true;
		}
		CHECK(threw == true);

		field.SetData("OK", 2);
		DWORD len = 0;
		char out[8] = {0};
		field.GetBinData(out, len, 0);
		CHECK(len == 2);
		CHECK(memcmp(out, "OK", 2) == 0);
	}

	// Short package rejected
	{
		CUnpkg parser;
		TFieldFormat fmts[FIELD_COUNT_MAX];
		fill_formats(fmts);
		parser.SetFormatDef(fmts);

		bool threw = false;
		try {
			char shortPkg[] = "0200";
			parser.CreateISO8583FromBinEx(shortPkg, 4, MsgType_CHAR);
		} catch (CRK_Exception&) {
			threw = true;
		}
		CHECK(threw == true);
	}

	// Pack -> Unpack roundtrip (64 fields bitmap)
	{
		TISO8583 pkg("0200", ISO8583_64);
		// Field2 LLVAR: "16" + 16-digit PAN
		const char* f2 = "166222021234567890";
		CHECK(pkg.AddFieldRAW(2, f2, (DWORD)strlen(f2)) == true);
		CHECK(pkg.AddFieldRAW(3, "000000", 6) == true);
		CHECK(pkg.AddFieldRAW(4, "000000001000", 12) == true);
		CHECK(pkg.AddFieldRAW(11, "123456", 6) == true);

		char bin[BIN_DATA_MAX_SIZE];
		DWORD binLen = 0;
		pkg.GetBinData(bin, binLen, false);
		CHECK(binLen > 0);
		CHECK(memcmp(bin, "0200", 4) == 0);

		CUnpkg parser;
		TFieldFormat fmts[FIELD_COUNT_MAX];
		fill_formats(fmts);
		parser.SetFormatDef(fmts);

		TISO8583* parsed = parser.CreateISO8583FromBinEx(bin, binLen, MsgType_CHAR);
		CHECK(parsed != NULL);
		CHECK(parsed->GetMsgType() == "0200");
		CHECK(parsed->TestBitmap(2) == true);
		CHECK(parsed->TestBitmap(3) == true);
		CHECK(parsed->TestBitmap(4) == true);
		CHECK(parsed->TestBitmap(11) == true);

		T8583Field* p2 = dynamic_cast<T8583Field*>(parsed->GetFieldObj(2));
		T8583Field* p3 = dynamic_cast<T8583Field*>(parsed->GetFieldObj(3));
		CHECK(p2 != NULL);
		CHECK(p3 != NULL);
		CHECK(p3->AsString() == "000000");
		CHECK(p2->AsString() == f2);

		delete parsed;
	}

	// Null pData rejected
	{
		CUnpkg parser;
		TFieldFormat fmts[FIELD_COUNT_MAX];
		fill_formats(fmts);
		parser.SetFormatDef(fmts);
		bool threw = false;
		try {
			parser.CreateISO8583FromBinEx(NULL, 0, MsgType_CHAR);
		} catch (CRK_Exception&) {
			threw = true;
		}
		CHECK(threw == true);
	}
}
