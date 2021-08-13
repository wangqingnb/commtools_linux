#pragma once
//#include<string>
//using namespace std;

class CBaseLog
{
public:
    CBaseLog(void);
    virtual ~CBaseLog(void);
    void virtual WriteLogWithFormat(const char* pszFormat, ...);
	void virtual WriteLog(const char* s);
};
