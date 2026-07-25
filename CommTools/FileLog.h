#pragma once
#include <stdio.h>
#include "BaseLog.h"

class CFileLog :
    public CBaseLog
    
{
private:    
    FILE* fp = NULL;
    bool m_bAddDateTime;
public:
    CFileLog(const char* FileName, bool bAddDateTime=false);
    virtual ~CFileLog(void);
    virtual void WriteLog(const char* s);
    virtual void WriteLogWithFormat(const char* pszFormat, ...);
};
