#include "CommTools.h"
typedef unsigned char U_CHAR;

enum TDesMode {dmEncry, dmDecry};
enum TDESKeyMode {Single, Double, Triple};

bool DesEncry(U_CHAR* pData, U_CHAR* pKey, U_CHAR* pOutData, DWORD DataLen, TDESKeyMode KeyMode = Single);
bool DesDecry(U_CHAR* pData, U_CHAR* pKey, U_CHAR* pOutData, DWORD DataLen, TDESKeyMode KeyMode = Single);


