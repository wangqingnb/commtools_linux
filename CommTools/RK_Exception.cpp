#include "RK_Exception.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
CRK_Exception::CRK_Exception(const char* pszFormat, ...)
{
	memset(Msg, 0, sizeof(Msg));
	va_list argList;
	va_start(argList, pszFormat);
	vsnprintf(Msg, sizeof(Msg), pszFormat, argList);
	va_end(argList);
}

CRK_Exception::~CRK_Exception(void)
{
}

const char* CRK_Exception::getMsg(void)
{
    return Msg;
}

