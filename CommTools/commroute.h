#include "CommTools.h"
#include <string>
using namespace std;

void BinToHex(char* Buffer, char* Text, int BufSize);
int HexToBin(char* Text, char* Buffer, int BufSize);
unsigned int BCDToDec(const char *bcd, int length);
string Str_TrimA(const string& str);
string GetSysDateTimeStr(void);
string GetDateStr();
bool FileExists(const char* FileName);
unsigned long long GetTickCount64();
char* GetExePath(char* buf, int ibufSize);
bool IsInstanceExists(const char* procname);
