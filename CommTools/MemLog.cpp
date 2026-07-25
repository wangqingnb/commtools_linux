
#include "MemLog.h"
#include "commroute.h"
#include <stdlib.h>
#include <stdarg.h>

CSysLog::CSysLog(void)
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&m_mutex, &attr);
	pthread_mutexattr_destroy(&attr);
	pthread_cond_init(&m_cond, NULL);
	pStrQueue = new queue<string>;
}

CSysLog::~CSysLog(void)
{
	delete pStrQueue;
	pthread_cond_destroy(&m_cond);
	pthread_mutex_destroy(&m_mutex);
}

void CSysLog::lock()
{
	pthread_mutex_lock(&m_mutex);
}

void CSysLog::unlock()
{
	pthread_mutex_unlock(&m_mutex);
}


void CSysLog::WriteLogWithFormat(const char* pszFormat, ...)
{
	lock();
	va_list argList;
	va_start(argList, pszFormat);

	char buffer[8192] = { 0 };
	string strDt = GetSysDateTimeStr();
	vsnprintf(buffer, 8192, pszFormat, argList);
	string s = buffer;
	pStrQueue->push(strDt + s);

	va_end(argList);
	unlock();
}

void CSysLog::WriteLog(string& s)
{
	lock();
	pStrQueue->push(s);
	unlock();
}


void CSysLog::WriteLog(const char* str)
{
	lock();
	string s = str;
	pStrQueue->push(s);
	unlock();
}

string CSysLog::GetLog()
{
	lock();
	string s = "";
	//杩欓噷闄愬埗杈撳嚭鐨勫瓧绗︿笉澶?澶?(s.size() < 1024锛? 浣嗘槸鍊煎緱娉ㄦ剰鐨勬槸锛氬疄闄呰緭鍑哄彲鑳戒細澶т簬1024)
	while (pStrQueue->size() && (s.size() < 1024)) {
		s += pStrQueue->front();
		pStrQueue->pop();
	}
	unlock();
	return  s;
}
