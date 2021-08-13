#pragma once
#include <queue>
using std::queue;
#include <string>
using namespace std;

#include "BaseLog.h"
#include <pthread.h>
class CSysLog :
	public CBaseLog
{
private:
	pthread_mutex_t m_mutex;
	pthread_cond_t m_cond;
	queue<string>* pStrQueue;
public:
	CSysLog(void);
	~CSysLog(void);
    void lock();
    void unlock();
	void virtual WriteLogWithFormat(const char* pszFormat, ...);
	void virtual WriteLog(string& s);
	void virtual WriteLog(const char* str);
	string GetLog();
};

