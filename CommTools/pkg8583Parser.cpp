#include "commroute.h"
#include "RK_Exception.h"
#include "pkg8583.h"
#include "pkg8583Parser.h"
#include <string.h>


bool ParseFieldFormat(TFieldFormat& FieldFormat,  string s)
{
	FieldFormat.Defined = false;
	s = Str_TrimA(s);

	//LenType
	int p = (int)s.find(",");
	if (p < 0) return false;
	FieldFormat.LenType = (BYTE)atol(s.substr(0,p).data());
	s.erase(0, p+1);

	//LenFormat
	p = (int)s.find(",");
	if (p < 0) return false;
	FieldFormat.LenFormat = (BYTE)atol(s.substr(0,p).data());
	s.erase(0, p+1);

	//Field Desc - skip it
	p = (int)s.find(",");
	if (p < 0) return false;
	s.erase(0, p+1);

	//aligne
	p = (int)s.find(",");
	if (p < 0) return false;
	FieldFormat.aligne = (BYTE)atol(s.substr(0,p).data());
	s.erase(0, p+1);

	//Filler
	p = (int)s.find(",");
	if (p < 0) return false;
	string sTmp = Str_TrimA(s.substr(0,p).data());
	s.erase(0, p+1);

	char Filler = 0x20;
	if (sTmp.size() == 3 && sTmp[1] == '$')
	{
		size_t l = HexToBin(&sTmp[1], &Filler, 1);
		Filler = (l!=0) ? Filler : 0x20;
	} else
	{
		if (sTmp != "")   
			Filler = sTmp[0];
	}
	FieldFormat.Filler = Filler;

	//MinLen
	p = (int)s.find(",");
	if (p < 0) return false;
	FieldFormat.MinLen = (WORD)atol(s.substr(0,p).data());
	s.erase(0, p+1);

	//MaxLen
	p = (int)s.find(",");
	if (p < 0) return false;
	FieldFormat.MaxLen = (WORD)atol(s.substr(0,p).data());
	s.erase(0, p+1);

	//ContentType
	FieldFormat.ContentType = (BYTE)atol(Str_TrimA(s).data());
	FieldFormat.Defined = true;
	return true;
}


T8583Parser::T8583Parser()
{
	memset(FieldFormat, 0, sizeof(FieldFormat));
	LoadFieldFormatDef();
}

TISO8583* T8583Parser::CreateISO8583FromBin(char* pData, DWORD iPkgSize, TMsgType MsgTypeMode)
{
	// 无独立实现：缺少包长度无法做边界检查，统一走 Ex 版
	return CreateISO8583FromBinEx(pData, iPkgSize, MsgTypeMode);
}

TISO8583* T8583Parser::CreateISO8583FromBinEx(char* pData,  DWORD iPkgSize, TMsgType MsgTypeMode)
{

	BYTE BmpBin[FIELD_COUNT_MAX / 8];
	bool BitMap[FIELD_COUNT_MAX];
	//PosIdx, i, j: integer;
	WORD MsgTypeLen;
	DWORD FieldNum; //最多包含域数
	DWORD BmpBytes; //位图占用的字节数
	DWORD iDataRemain = iPkgSize;

	if (pData == NULL)
		throw CRK_Exception("CreateISO8583FromBinEx: pData is NULL");

	string MsgType;
	if (MsgTypeMode == MsgType_CHAR)
		MsgTypeLen = 4;
	else
		MsgTypeLen = 2;

	if (iPkgSize < (DWORD)(MsgTypeLen + 1))
		throw CRK_Exception("CreateISO8583FromBinEx: package too short for MsgType/Bitmap");

	if (MsgTypeMode == MsgType_CHAR)
		MsgType.assign(pData, 4);
	else
	{
		char hex[4] = {0};
		BinToHex(pData, hex, MsgTypeLen);
		MsgType.assign(hex, 4);
	}

	TISO8583Type PkgType;
	if (((unsigned char)pData[MsgTypeLen] >> 7) == 1)
	{
		FieldNum = 128;
		PkgType = ISO8583_128;
	}
	else
	{
		FieldNum = 64;
		PkgType = ISO8583_64;
	}

	BmpBytes = FieldNum / 8;
	if (iPkgSize < MsgTypeLen + BmpBytes)
		throw CRK_Exception("CreateISO8583FromBinEx: package too short for Bitmap");

	iDataRemain = iPkgSize - MsgTypeLen - BmpBytes;
	memcpy(BmpBin, pData + MsgTypeLen, BmpBytes);

	for (int i = 0; i < FIELD_COUNT_MAX; i++)
		BitMap[i] = false;

	size_t PosIdx = 0;
	for (DWORD i = 0; i< BmpBytes; i++)
		for (int j=7; j >=0; j--)
		{
			if ((BmpBin[i] >> j & 0x1) == 0x1)
				BitMap[PosIdx] = true;
			PosIdx++;
		}


	TISO8583* pISO8583 = new TISO8583(MsgType, PkgType);
	char* pDataPos = pData + MsgTypeLen + BmpBytes;  //消息类型长度+位图长度

	for (DWORD i = 1; i< FieldNum; i++) //从第2域开始
	{
		if (BitMap[i])
		try {
			T8583BaseField* pField = UnpackFieldEx(i+1, (char**) &(pDataPos), iDataRemain);
			if (!pISO8583->AddField(i+1, pField))
			{
				delete pField;
				throw CRK_Exception("AddField: %d Error!", i+1);
			}
		}
		catch (CRK_Exception&)
		{
			delete pISO8583;
			throw;
		}
		catch (...) {
			delete pISO8583;
			throw CRK_Exception("Unpack Field: %d  Error!",  i);
		}
	}
	if (iDataRemain != 0) {
		delete pISO8583;
		throw CRK_Exception("Check Package Length Error!");
	}
	return pISO8583;
}

T8583BaseField* T8583Parser::GenFieldObj(char* pData, DWORD Len)
{
  T8583Field* pField = new T8583Field();
  pField->SetData(pData, Len);
  return pField;
}

T8583BaseField* T8583Parser::UnpackField(DWORD idx,  char** pDataPos)
{
	WORD  L = 0;
	WORD DataLen = 0; //域长度 单位字节 （包含域中的长度位和数据）
	T8583BaseField* result;
	PFieldFormat pFieldFmt = &FieldFormat[idx-1];
	if (!pFieldFmt->Defined)
		throw CRK_Exception("Field: %d  not defined!\r\n", idx);

	if (pFieldFmt->LenType == 0) //定长
	{
		if (pFieldFmt->ContentType == 3) //BCD then
			DataLen = (WORD)(pFieldFmt->MaxLen / 2 + pFieldFmt->MaxLen % 2);
		else
			DataLen = pFieldFmt->MaxLen;
	} else if (pFieldFmt->LenType == 1) //1-LL变长
	{
		if (pFieldFmt->LenFormat == 2)    //长度位格式 2-BCD
		{
			L = (WORD)BCDToDec((char*)*pDataPos, 1);
			DataLen = 1;  //长度位占用字节
		} else if (pFieldFmt->LenFormat == 1)     //长度位格式 1-数字字符
		{
			char tmpData[4];
			memcpy(tmpData, *pDataPos, 2);
			tmpData[2] = 0;
			L = (WORD)strtoul(tmpData, NULL,10);
			DataLen = 2;  //长度位占用字节
		} else
			throw CRK_Exception("Field: %d  Define Information Error!",  idx);

		if (pFieldFmt->ContentType == 3) //BCD then
			DataLen = (WORD)(DataLen + (L / 2 + L % 2));
		else
			DataLen = (WORD)(DataLen + L);

	} else if (pFieldFmt->LenType == 2) //2-LLL变长
	{
		if (pFieldFmt->LenFormat == 2)    //长度位格式 2-BCD
		{
			L = (WORD)BCDToDec((char*)*pDataPos, 2);
			DataLen = 2;  //长度位占用字节
		} else if (pFieldFmt->LenFormat == 1)    //长度位格式 1-数字字符
		{
			char tmpData[4];
			memcpy(tmpData, *pDataPos, 3);
			tmpData[3] = 0;
			L = (WORD)strtoul(tmpData, NULL,10);
			DataLen = 3;  //长度位占用字节
		} else
			throw CRK_Exception("Field: %d  Define Information Error!",  idx);

		if (pFieldFmt->ContentType == 3) //BCD then
			DataLen = (WORD)(DataLen + (L / 2 + L % 2));
		else
			DataLen = (WORD)(DataLen + L);
	}

	result = GenFieldObj((char*)*pDataPos, DataLen);
	*pDataPos += DataLen;

	return result;
}

T8583BaseField*  T8583Parser::UnpackFieldEx(DWORD idx, OUT char** pDataPos, IN OUT DWORD &iDataRemain)
{
	WORD  L = 0;
	WORD DataLen = 0; //域长度 单位字节 （包含域中的长度位和数据）
	T8583BaseField* result;
	PFieldFormat pFieldFmt = &FieldFormat[idx-1];
	if (!pFieldFmt->Defined)
		throw CRK_Exception("Field: %d  not defined!\r\n", idx);

	if (pFieldFmt->LenType == 0) //定长
	{
		if (pFieldFmt->ContentType == 3) //BCD then
			DataLen = (WORD)(pFieldFmt->MaxLen / 2 + pFieldFmt->MaxLen % 2);
		else
			DataLen = pFieldFmt->MaxLen;
	} else if (pFieldFmt->LenType == 1) //1-LL变长
	{
		if (pFieldFmt->LenFormat == 2)    //长度位格式 2-BCD
		{
			if (iDataRemain < 1)
				throw CRK_Exception("Field: %d  Parser Error!",  idx);
			L = (WORD)BCDToDec((char*)*pDataPos, 1);
			DataLen = 1;  //长度位占用字节
		} else if (pFieldFmt->LenFormat == 1)     //长度位格式 1-数字字符
		{
			char tmpData[4];
			if (iDataRemain < 2)
				throw CRK_Exception("Field: %d  Parser Error!",  idx);
			memcpy(tmpData, *pDataPos, 2);
			tmpData[2] = 0;
			L = (WORD)strtoul(tmpData, NULL,10);
			DataLen = 2;  //长度位占用字节
		} else
			throw CRK_Exception("Field: %d  Define Information Error!",  idx);

		if (pFieldFmt->ContentType == 3) //BCD then
			DataLen = (WORD)(DataLen + (L / 2 + L % 2));
		else
			DataLen = (WORD)(DataLen + L);

	} else if (pFieldFmt->LenType == 2) //2-LLL变长
	{
		if (pFieldFmt->LenFormat == 2)    //长度位格式 2-BCD
		{
			if (iDataRemain < 2)
				throw CRK_Exception("Field: %d  Parser Error!",  idx);
			L = (WORD)BCDToDec((char*)*pDataPos, 2);
			DataLen = 2;  //长度位占用字节
		} else if (pFieldFmt->LenFormat == 1)    //长度位格式 1-数字字符
		{
			char tmpData[4];
			if (iDataRemain < 3)
				throw CRK_Exception("Field: %d  Parser Error!",  idx);
			memcpy(tmpData, *pDataPos, 3);
			tmpData[3] = 0;
			L = (WORD)strtoul(tmpData, NULL,10);
			DataLen = 3;  //长度位占用字节
		} else
			throw CRK_Exception("Field: %d  Define Information Error!",  idx);

		if (pFieldFmt->ContentType == 3) //BCD then
			DataLen = (WORD)(DataLen + (L / 2 + L % 2));
		else
			DataLen = (WORD)(DataLen + L);
	}
	if (DataLen > iDataRemain) {
		throw CRK_Exception("Field: %d  Parser Error!",  idx);
	}

	result = GenFieldObj((char*)*pDataPos, DataLen);
	*pDataPos += DataLen;
	iDataRemain -= DataLen;
	return result;
}

void T8583Parser::LoadFieldFormatDef()
{
}


