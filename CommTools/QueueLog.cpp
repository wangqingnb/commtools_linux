
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
	string s =  buffer;
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
	//这里限制输出的字符不太大(s.size() < 1024， 但是值得注意的是：实际输出可能会大于1024)
	while(pStrQueue->size() && (s.size() < 1024)){
		s += pStrQueue->front();
		pStrQueue->pop(); 
	}
	unlock();
	return  s;
}