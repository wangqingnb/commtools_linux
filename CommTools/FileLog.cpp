#include "commroute.h"
#include "FileLog.h"
#include "RK_Exception.h"
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
//#include <stdlib.h>



CFileLog::CFileLog(const char* FileName, bool bAddDateTime)
{
	char buf[MAX_MSG_SIZE] = { 0 };
   m_bAddDateTime = bAddDateTime;
   if (FileExists(FileName))
	   fp = fopen(FileName, "a");
   else
	   fp = fopen(FileName, "w+");

   if (fp == NULL) 
   { 
		snprintf(buf,  MAX_MSG_SIZE, "create /open log file \"%s\" error! errno: %d, %s",  FileName, errno, strerror(errno));
		throw CRK_Exception(buf);
   }
}

CFileLog::~CFileLog(void)
{
    if (fp)
		fclose(fp);
}
void CFileLog::WriteLog(string& s)
{
	fputs(s.data(), fp);
	fflush(fp);
}

void CFileLog::WriteLogWithFormat(char* pszFormat, ...) 
{

	va_list argList;
	va_start(argList, pszFormat);

	char buffer[8192] = { 0 };
	string strDt = "";
	if (m_bAddDateTime) {
		strDt = GetSysDateTimeStr();
	}
	vsnprintf(buffer, 8192, pszFormat, argList);
	string s = buffer;
	s = strDt + s;
	fputs(s.data(), fp);
	fflush(fp);
	va_end(argList);
 }

