#pragma once
#include "CommTools.h"


class CRK_Exception
{
private:
	char Msg[MAX_MSG_SIZE];
public:
    //CRK_Exception(LPCTSTR pszFormat, ...);
    CRK_Exception(const char* pszFormat, ...);
    virtual ~CRK_Exception(void);
    const char* getMsg(void);
};
