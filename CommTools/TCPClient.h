#pragma once
#include <pthread.h>
#include "SockPubDefine.h"
#include "BaseLog.h"
#include <netinet/in.h>
#include <sys/socket.h>
enum TConnectStatus { TS_CONNECTING, TS_CONNECTED, TS_NOTCONNECT };

class TCPClient;

typedef void(*PRecvSendEvent)(char* Buf, long Len, long ExpLen, void* pClass);
typedef void(*PConnectStatusEvent)(void* pClass);

#define MAX_EPWEVENT_NUM 20  //一次等待EPoll事件的最大数
#define WAIT_TIME_OUT  300   //等待EPoll事件超时值（毫秒）

class TCPClient
{
private:
	char m_ServerIP[128];  //服务器地址
	char m_LocalIP[128];  //本端地址
	WORD m_ServerPort; //远端服务器监听端口

	//全局统计信息
	_U64 volatile m_TotalRecvBytes;  //接收字节合计
	_U64 volatile m_TotalSentBytes;  //发送字节合计

	pthread_mutex_t m_mutex;
	int m_epfd;    //epoll事件描述符

	struct sockaddr_in  ClientAddr;
	struct sockaddr_in  ServerAddr;

	SOCKET ClientSocket;
	bool m_Terminated;
	CBaseLog* LogObj;  //日志接口对象

	//回调函数指针
	PRecvSendEvent m_OnRecvCompleted;
	void* RegOnRecvCompletedClass;

	PRecvSendEvent m_OnSendCompleted;
	void* RegOnSendCompletedClass;

	PConnectStatusEvent m_OnConnected;
	void* RegOnConnectedClass;

	PConnectStatusEvent m_OnDisconnected;
	void* RegOnDisconnectedClass;

	PConnectStatusEvent m_OnConnectFail;
	void* RegOnConnectFailClass;

	TConnectStatus m_ConnectStatus;

	pthread_t  m_ThreadId;
	static void* WorkerThread(void* lpParameter);

	SOCKET_INFORMATION SocketInfo;
	void CloseConnect();
	bool SetNonblocking(SOCKET sock);
	void ReTriggerEvent(PSOCKET_INFORMATION SI);

public:
	TCPClient(void);
	void setLogObj(CBaseLog* LogObj);
	virtual ~TCPClient(void);


	void SetServerPort(WORD Port);
	void SetServerIP(char* IP);
	void SetLocalIP(char* IP);
	bool Connect();
	void Disconnect();
	bool PostSend(char* buf, DWORD len);
	bool PostRecv(char* buf, DWORD len);
	bool GetStatistics(PTCPLinkStatics pServerStatics);

	void RegCallback_OnRecvCompleted(void* pClass, PRecvSendEvent pCallBackFunc);
	void UnRegCallback_OnRecvCompleted();
	void RegCallback_OnSendCompleted(void* pClass, PRecvSendEvent pCallBackFunc);
	void UnRegCallback_OnSendCompleted();
	void RegCallback_OnConnected(void* pClass, PConnectStatusEvent pCallBackFunc);
	void UnRegCallback_OnConnected();
	void RegCallback_OnDisconnected(void* pClass, PConnectStatusEvent pCallBackFunc);
	void UnRegCallback_OnDisconnected();
	void RegCallback_OnConnectFail(void* pClass, PConnectStatusEvent pCallBackFunc);
	void UnRegCallback_OnConnectFail();

};

