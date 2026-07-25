#pragma once
//#include "des.h"

//mac -输出的mac结果， data-输入的数据  len-输入数据长度  mac_key-mackey明文, KeyMode-Mac密钥类型,  init_v-初始向量
void CalcMACCBC(UCHAR* mac, UCHAR* data, DWORD len, UCHAR* mac_key,
  TDESKeyMode KeyMode = Single, UCHAR* init_v = nullptr);

void CalcMACCBC_XOR(UCHAR* mac, UCHAR* data, DWORD len, UCHAR* mac_key,
  TDESKeyMode KeyMode = Single);
