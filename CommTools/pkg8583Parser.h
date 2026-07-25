#pragma once
#pragma pack(1)
#ifndef IN
#define IN
#endif

#ifndef OUT
#define OUT
#endif

typedef unsigned char   BYTE;
typedef unsigned short   WORD;

typedef struct _TFieldFormat
{
    bool Defined;  //true - 已定义  false-未定义
    BYTE LenType;     //域长度类型0-定长，1-LL变长，2-LLL变长
    BYTE LenFormat;   //长度位格式0-无关,1-数字字符, 2-BCD,3-二进制数值
    BYTE aligne;      //对齐方式 0-无关 1-左对齐 2-右对齐
    BYTE Filler;      //填充字符
    WORD MinLen;      //最小长度(对于定长类型无意义
    WORD MaxLen;      //最长长度(对于定长表示长度；对于BCD类型是实际字符个数而非占用的字节数，其他类型都是占用的字节数)
    BYTE ContentType; //0-文本（字符+数字混合） 1-二进制 2-数字 3-BCD
} TFieldFormat, *PFieldFormat;
#pragma pack()

enum TMsgType {MsgType_BCD, MsgType_CHAR};


bool ParseFieldFormat(TFieldFormat& FieldFormat, string s);

class T8583Parser
{
protected:
    TFieldFormat FieldFormat[FIELD_COUNT_MAX];
    virtual T8583BaseField* GenFieldObj(char* pData, DWORD Len);

	//idx-域号， pDataPos-当前位置(输出)
	virtual T8583BaseField* UnpackField(DWORD idx,  OUT char** pDataPos);

	//idx-域号， pDataPos-当前位置(输出), iDataRemain-剩余字节数(输入、输出)
    virtual T8583BaseField* UnpackFieldEx(DWORD idx, OUT char** pDataPos, IN OUT DWORD &iDataRemain);

public:
    T8583Parser();
    TISO8583* CreateISO8583FromBin(char* pData, DWORD iPkgSize, TMsgType MsgTypeMode = MsgType_CHAR);
	TISO8583* CreateISO8583FromBinEx(char* pData,  DWORD iPkgSize, TMsgType MsgTypeMode = MsgType_CHAR);
    virtual void LoadFieldFormatDef();
};
