#pragma once
#include <time.h>
#include "CommTools.h"

#define DATA_BUFSIZE				4096   // default buffer size
#define C_WAIT_TIMEOUT				500    //wait event timeout secs
#define MAX_PKG_LEN					4096

#define SOCK_OPER_NONE  0
#define SOCK_OPER_RECV  1
#define SOCK_OPER_SEND  2
#define SOCK_OPER_CONN  3

#define SOCKET int

typedef struct _TCPLinkStatics
{
	_U64 RecvTotalBytes;  //接收字节合计
	_U64 SendTotalBytes;  //发送字节合计
	struct tm ConnectedDT;  //连接建立时间
	_U64  FirstActiveTick;  //连接建立时间戳
	_U64  LastActiveTick; //最后活动时间戳
} TCPLinkStatics, * PTCPLinkStatics;

typedef struct _SBUF {
	_U32 len;     /* the length of the buffer */
	 char* buf;   /* the pointer to the buffer */
} SBUF;


typedef struct _SOCKET_INFORMATION
{
	long			ID;
	SBUF			SendDataBuf;   //发送数据缓存区（含长度+缓冲区地址）
	SBUF			RecvDataBuf;   //接收数据缓存区（含长度+缓冲区地址）
	DWORD			BytesSEND;   //期望发送操作发送的字节
	DWORD			BytesRECV;  //期望接收操作的字节
	bool			bReading;   //true-正接收送数据，在没有完成上次读取时候，不可再次读取（post_recv）
	bool			bSending;   //true-正在发送数据，在没有完成上次写的时候，不可再次发送（post_send）
	char			RecvBuffer[DATA_BUFSIZE]; //内部接收缓冲区
	char			SendBuffer[DATA_BUFSIZE]; //内部发送缓冲区
	SOCKET			Socket;
	bool			bEvent;    //当前连接是否已经注册epoll事件
	_U64			RecvTotalBytes;  //接收字节合计
	_U64			SendTotalBytes;  //发送字节合计
	struct tm		ConnectedDT;  //连接建立时间
	_U64			FirstActiveTick;  //连接建立时间戳
	_U64			LastActiveTick; //最后活动时间戳
} SOCKET_INFORMATION, * PSOCKET_INFORMATION;


#define LOG(...) \
	if (LogObj != NULL) LogObj->WriteLog(__VA_ARGS__)

#define LOG_F(...) \
	if (LogObj != NULL) LogObj->WriteLogWithFormat(__VA_ARGS__)

#define LOG_S(...) \
	if (inst->LogObj != NULL) inst->LogObj->WriteLog(__VA_ARGS__)

#define LOG_SF(...) \
	if (inst->LogObj != NULL) inst->LogObj->WriteLogWithFormat(__VA_ARGS__)
