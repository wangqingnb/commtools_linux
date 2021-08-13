#include "des.h"
#include "mac.h"
#include <string.h>
#include "commroute.h"

void CalcMACCBC(UCHAR* mac, UCHAR* data, DWORD len, UCHAR* mac_key,
	TDESKeyMode KeyMode, UCHAR* init_v)
{
	char T_buf[8], T_mac[8], RData[8];
	memset(T_buf, 0, 8);
	if (init_v != nullptr)
		memcpy(T_buf, init_v, 8);

	for (DWORD i=0; i< len / 8; i++)
	{
		for (int j=0; j < 8; j++)
			T_buf[j] = (char) (T_buf[j] ^ data[i*8+j]);
		DesEncry((UCHAR*)T_buf, (UCHAR*)mac_key, (UCHAR*)T_mac, 8, KeyMode);
		memcpy(T_buf, T_mac, 8);
	}

	DWORD L = len % 8;
	if( L != 0)
	{
		memcpy(RData, &data[(len / 8)*8], L);
		for (DWORD i = 1; i <= 8-L; i++)
			RData[L+i-1] = 0;
		for (DWORD i = 0; i<8; i++)
			T_buf[i] =  T_buf[i] ^ RData[i];
		DesEncry((UCHAR*)T_buf, (UCHAR*)mac_key, (UCHAR*)T_mac, 8, KeyMode);
	}
	memcpy(mac, T_mac, 8);
}

void CalcMACCBC_XOR(UCHAR* mac, UCHAR* data, DWORD len, UCHAR* mac_key,
  TDESKeyMode KeyMode)
{
	char T_buf[8], T_mac[8], RData[8];
	memset(T_buf, 0, 8);
	for (DWORD i=0; i< len / 8; i++)
	 	for (DWORD j=0; j <= 7; j++)
			T_buf[j] = (char)(T_buf[j] ^ data[i*8+j]);

	DWORD L = len % 8;
	if( L != 0) {
		memcpy(RData, &data[(len / 8)*8], L);
		for (DWORD i = 1; i <= 8-L; i++)
			RData[L+i-1] = 0;
		for (DWORD i = 0; i<8; i++)
			T_buf[i] =  T_buf[i] ^ RData[i];
	}
	char HexData[16];
	BinToHex(T_buf, HexData, sizeof(T_buf));
	DesEncry((UCHAR*)HexData, mac_key, (UCHAR*)T_mac, 8, KeyMode);
	for (DWORD j = 0; j <= 7; j++)
    	HexData[j] =  T_mac[j] ^HexData[j+8];
	DesEncry((UCHAR*)HexData, mac_key, (UCHAR*)T_mac, 8, KeyMode);
	BinToHex(T_mac, HexData, sizeof(T_mac)); //转换为ASCII
	memcpy(mac, HexData, 8);  //前8个字节赋值给MAC
}

