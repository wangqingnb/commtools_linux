#include "commroute.h"
#include "RK_Exception.h"
#include "pkg8583.h"
#include <string.h>


void T8583Field::SetData(const char* pData, const DWORD &Len)
{
	m_DataLen = Len;
	memcpy(m_Data, pData, Len);
}

void T8583Field::GetBinData(char* pData, DWORD& Len, DWORD StartPos)
{
	Len = m_DataLen - StartPos;
	memcpy(pData, &m_Data[StartPos], Len);
}

string T8583Field::AsString()
{
	return string(m_Data, m_DataLen);
}

string T8583Field::AsHex()
{
	char HexData[FIELD_DATA_MAX_SIZE];
	memset(HexData, 0, sizeof(HexData));
	BinToHex(m_Data, HexData, m_DataLen);
	return string(HexData, m_DataLen*2);
}

T8583Field* T8583Field::Clone()
{
	T8583Field* pField = new T8583Field();
	pField->SetData(m_Data, m_DataLen);
	return pField;
}

TISO8583::TISO8583(const string &MsgType, TISO8583Type ISO8583Type)
{
	if (MsgType.length() != 4) {
		char Buf[MAX_MSG_SIZE];
		snprintf(Buf, sizeof(Buf), "TISO8583-> MsgType must have 4 bytes");
		throw CRK_Exception(Buf);
	}
	m_MsgType = MsgType;

	m_Type = ISO8583Type;
	if (m_Type == ISO8583_64) m_FieldMaxNum = 64;
		else m_FieldMaxNum = 128;

	m_DataState = EMPTY;
	m_FieldCount = 1;     //已包含bitmap域

	//初始化bitmap位 为 false
	memset(m_BitMap, 0, sizeof(m_BitMap));

	//初始化数据
	memset(m_Fields, 0, sizeof(m_Fields));

	m_BinDataLength = 0;

}



TISO8583::~TISO8583(void)
{
	for (DWORD i = 0; i <= m_FieldMaxNum; i++)
		if (m_Fields[i] != NULL)
		{
			delete m_Fields[i];
			m_Fields[i] = NULL;
		}
}

bool TISO8583::AddField(DWORD FieldIdx, T8583BaseField* pField)
{
	if ((FieldIdx < 2) || (FieldIdx > m_FieldMaxNum) || (m_BitMap[FieldIdx]))
		return false;
	m_FieldCount++;
	m_BitMap[FieldIdx] = true;
	m_DataState = MODIFIED;
	m_Fields[FieldIdx] = pField;
	return true;
}

bool TISO8583::AddFieldRAW(DWORD FieldIdx,  const char* pData, DWORD DataLen)
{
	if ((FieldIdx < 2) || (FieldIdx > m_FieldMaxNum) || (m_BitMap[FieldIdx]))
		return false;


	if (DataLen > 0 && DataLen <=FIELD_DATA_MAX_SIZE)
	{
		m_FieldCount++;
		m_BitMap[FieldIdx] = true;
		m_DataState = MODIFIED;

		T8583Field* pFieldObj = new T8583Field();
		pFieldObj->SetData(pData, DataLen);
		m_Fields[FieldIdx] = pFieldObj;
		return true;
	} else
		return false;
}

bool TISO8583::RemoveField(DWORD FieldIdx)
{
	if ((FieldIdx < 2) || (FieldIdx > m_FieldMaxNum))
		return false;

	if (m_BitMap[FieldIdx])
	{
		m_BitMap[FieldIdx] = false;
		if (m_Fields[FieldIdx] != NULL)
			delete m_Fields[FieldIdx];
		m_Fields[FieldIdx] = NULL;
		m_FieldCount--;
		if (m_FieldCount == 1) 
			m_DataState = EMPTY;
		else m_DataState = MODIFIED;
		return true;
	} else
		return false;
}

T8583BaseField* TISO8583::GetFieldObj(DWORD FieldIdx)
{
	if ((FieldIdx < 2) || (FieldIdx > m_FieldMaxNum))
		return NULL;

	if (m_BitMap[FieldIdx])
		return  m_Fields[FieldIdx];
	else
		return NULL;
}

bool TISO8583::UpdateField(DWORD FieldIdx, T8583BaseField* pField)
{
	if ((FieldIdx < 2) || (FieldIdx > m_FieldMaxNum))
		return false;

	if (m_BitMap[FieldIdx] && m_Fields[FieldIdx] != NULL)
	{
		delete m_Fields[FieldIdx];
		m_DataState = MODIFIED;
		m_Fields[FieldIdx] = pField;
		return true;
	} else 
		return false;
}


void TISO8583::GetBinData(char* pData,  DWORD &Len, const bool MsgTypeBCD)
{
	if (m_DataState == EMPTY)
	{
		Len = 0;
		return;
	}
	else if (m_DataState == MODIFIED) 
	{
		Gen8583BinData(MsgTypeBCD);
		m_DataState = STABLED;
	}
	memcpy(pData, m_BinData, m_BinDataLength);
	Len = m_BinDataLength;
}

bool TISO8583::TestBitmap(DWORD FieldIdx)
{
	if ((FieldIdx < 2) || (FieldIdx > m_FieldMaxNum))
		return false;
	return m_BitMap[FieldIdx];
}

TISO8583* TISO8583::Clone()
{
  TISO8583* pISO8583 = new TISO8583(m_MsgType, m_Type);
  for (DWORD i= 0; i<= m_FieldMaxNum; i++)
    if (m_Fields[i] != NULL)
       pISO8583->AddField(i, m_Fields[i]->Clone());
  return  pISO8583;
}

string TISO8583::GetMsgType()
{
	return m_MsgType;
}

void TISO8583::SetMsgType(const string& MT)
{
	m_MsgType = MT;
}

void TISO8583::Gen8583BinData(const bool &MsgTypeBCD)
{
  //已包含Message Type4个字节 和 bitmap域个字节（8/16个字节）
  DWORD MsgTypeLen;
  if (MsgTypeBCD) MsgTypeLen = 2;
	else MsgTypeLen = 4;

  DWORD BasePos = 0;
  if (m_Type == ISO8583_128)
     BasePos = MsgTypeLen + 16;
  else BasePos = MsgTypeLen + 8;

  m_BinDataLength = BasePos;
  DWORD Offset = 0;

  for (DWORD i = 2; i <=m_FieldMaxNum; i++)
  {
    if (m_BitMap[i])
	{
      T8583BaseField* pField = (T8583BaseField*) m_Fields[i];
      if (pField == NULL)
		  continue;
	  DWORD Len;
      pField->GetBinData(&m_BinData[BasePos + Offset], Len);
	  Offset += Len;
      m_BinDataLength += Len;
      if (m_BinDataLength > BIN_DATA_MAX_SIZE)
	  {
		char Buf[MAX_MSG_SIZE];
		snprintf(Buf, sizeof(Buf), "TISO8583::Gen8583BinData-> BinData too Large!\r\n");
		throw CRK_Exception(Buf);
	  }
	}
  }
  
  if (MsgTypeBCD)
  {
    HexToBin((char*)m_MsgType.data(), m_BinData, 2); //存放msgtype 压缩BCD
    MakeBitmap(2);
  } else
  {
    memcpy(m_BinData, m_MsgType.data(), 4); //存放msgtype
    MakeBitmap();    
  }

}
void TISO8583::MakeBitmap(const DWORD &MemOffset)
{
	unsigned char BmpBin[17];
	memset(BmpBin, 0, sizeof(BmpBin));
	if (m_Type == ISO8583_64)  BmpBin[1] = 0;
		else  BmpBin[1] = 128;  //bmp是128位的第一位置为1，64位的置为0
	for (DWORD i = 2; i <= m_FieldMaxNum; i++)
	{
		if (m_BitMap[i])
		{
			unsigned char Data = 1;
			DWORD BytePos;
			if (i % 8  != 0)
			{
				BytePos = i / 8 + 1;
				Data = (unsigned char)(Data << (8 - i % 8));
			} else
				BytePos = i / 8;
			BmpBin[BytePos] = BmpBin[BytePos] | Data;
		}
	}
	if (m_Type == ISO8583_128)
		memcpy(&m_BinData[MemOffset], &BmpBin[1], 16);
	else
		memcpy(&m_BinData[MemOffset], &BmpBin[1], 8);
}
