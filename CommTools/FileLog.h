#pragma once
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
    void virtual WriteLog(string& pStr);
	void virtual WriteLogWithFormat(char* pszFormat, ...);
};
