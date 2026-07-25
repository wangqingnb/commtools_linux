#pragma once
#include <string>
#include "CommTools.h"

#define  FIELD_COUNT_MAX  128
#define  FIELD_DATA_MAX_SIZE  1024
#define  BIN_DATA_MAX_SIZE  8192

using namespace std;
//  EISO8583Error = class(Exception);

enum TDataState  {EMPTY, MODIFIED, STABLED};
enum TISO8583Type {ISO8583_64, ISO8583_128};

class T8583BaseField
{
public:
	virtual ~T8583BaseField(void) {};
	virtual void GetBinData(char* pData, DWORD& Len, DWORD StartPos = 0) = 0;
	virtual T8583BaseField* Clone() = 0;
};

class T8583Field : public T8583BaseField
{
protected:
	char m_Data[FIELD_DATA_MAX_SIZE];
	DWORD m_DataLen;
public:
	virtual void SetData(const char* pData, const DWORD &Len);
	virtual void GetBinData(char* pData, DWORD& Len, DWORD StartPos = 0);
	virtual string AsString();
	virtual string AsHex();
	virtual T8583Field* Clone();
};

class TISO8583
{
private:
	string m_MsgType;
	TISO8583Type m_Type;  //ISO8583 类型  F64 or F128
	DWORD m_FieldMaxNum; //最多包含域数
	DWORD m_FieldCount;  //已含域数
	DWORD m_BinDataLength; //数据总长度
	TDataState m_DataState;
	char m_BinData[BIN_DATA_MAX_SIZE];
	bool m_BitMap[FIELD_COUNT_MAX + 1];  //为了便于理解，数组0下标未使用

	T8583BaseField* m_Fields[FIELD_COUNT_MAX + 1]; //（多建一个，其中第0个索引不用，便于索引号的匹配）

	void MakeBitmap(const DWORD& MemOffset = 4);
	void Gen8583BinData(const bool& MsgTypeBCD);
public:
	TISO8583(const string& MsgType, TISO8583Type ISO8583Type = ISO8583_128);
	virtual ~TISO8583(void);
	bool AddField(DWORD FieldIdx, T8583BaseField* pField);
	bool AddFieldRAW(DWORD FieldIdx, const char* pData, DWORD DataLen);
	bool RemoveField(DWORD FieldIdx);
	T8583BaseField* GetFieldObj(DWORD FieldIdx);
	bool UpdateField(DWORD FieldIdx, T8583BaseField* pField);
	void GetBinData(char* pData, DWORD& Len, const bool MsgTypeBCD = false);
	bool TestBitmap(DWORD FieldIdx);
	virtual TISO8583* Clone();
	string GetMsgType();
	void SetMsgType(const string& MT);
};

