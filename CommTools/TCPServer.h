#pragma once
#include "CommTools.h"
#include "BaseLog.h"
#include <time.h>
#include <pthread.h>
#include "SockPubDefine.h"

#define MAX_CONNECT_NUM 64
#define MAX_EPWEVENT_NUM 20  //一次等待EPoll事件的最大数
#define WAIT_TIME_OUT  300   //等待EPoll事件超时值（毫秒）

//Type define for Callback functions

//Len- 实际接发送/收到字节  ExpLen -PostSend/PostRecv 期望发送/接收的字节
typedef void(*PServerRecvSendEvent)(long ID, char* Buf, long Len, long ExpLen, void* pClass); 

typedef void(*PServerConnectStatusEvent)(long ID, void* pClass);


class TCPServer
{
private:
	//全局统计信息
	_U64 volatile m_TotalRecvBytes;  //接收字节合计
	_U64 volatile m_TotalSentBytes;  //发送字节合计
	_U64  m_MaxTimeNoActive; //允许最长的不活动时间（秒）
	int m_af;  //协议类型 AF_INET， AF_INET6
	DWORD volatile m_IDCount;  //连接ID计数
	WORD m_MaxConnectNum;  //允许的最大连接数
	WORD m_Port; //监听端口
	int m_epfd_tf;    //数据传输事件描述符


	//char AcceptBuffer[(sizeof(SOCKADDR_STORAGE) + 16) * 2];

	SOCKET ListenSocket;
	pthread_mutex_t m_mutex;
	char m_LocalServerIP[128];  //本端地址
	bool m_ServerStarted;
	bool m_Terminated;

	CBaseLog* LogObj;  //日志接口对象
	_U64  m_LastTick;  //最后活动时间
	DWORD volatile m_ConnTotal;  //当前连接数

	//存放socket信息的数组，第0项目保留，从第1项开始
	PSOCKET_INFORMATION SocketArray[MAX_CONNECT_NUM+1] = { 0 };

	//关闭连接后延迟释放，避免 TransferThread 仍持有 epoll data.ptr 时 UAF
	PSOCKET_INFORMATION m_PendingFree[MAX_CONNECT_NUM + 1];
	WORD m_PendingFreeCount;
	void DrainPendingFree();

	//回调函数指针
	PServerRecvSendEvent m_OnRecvCompleted;
	void* RegOnRecvCompletedClass;

	PServerRecvSendEvent m_OnSendCompleted;
	void* RegOnSendCompletedClass;

	PServerConnectStatusEvent m_OnConnected;
	void* RegOnConnectedClass;

	PServerConnectStatusEvent m_OnDisconnected;
	void* RegOnDisconnectedClass;

	long GenID();
	long GetSocketInformationIDXbyID(long ConnectID);

	//处理连接线程
	pthread_t  m_AcceptThreadId;
	static void* AcceptThread(void* lpParameter);

	//处理收发传输线程
	pthread_t  m_TransferThreadId;
	static void* TransferThread(void* lpParameter);

	void ReTriggerEvent(PSOCKET_INFORMATION SI);
public:
	TCPServer(void);
	void setLogObj(CBaseLog* LogObj);
	virtual ~TCPServer(void);
	bool SetNonblocking(SOCKET sock);
	void SetNetPort(WORD Port);
	void SetLocalServerIP(const char* IP);
	void SetAddressFamily(int af);
	void SetMaxConnectNum(WORD num);
	void SetMaxTimeNoActive(WORD secs);

	void StartServer();
	void StopServer();
	//void WriteLog(const S: String);
	bool PostSend(long ConnectID, char* buf, DWORD len);
	bool PostRecv(long ConnectID, char* buf = NULL, DWORD len = 0);
	//void CloseClient(long ConnectID);
	void CloseConnect(long ConnectID);
	bool GetStatistics(long ConnectID, PTCPLinkStatics pServerStatics);

	void RegCallback_OnRecvCompleted(void* pClass, PServerRecvSendEvent pCallBackFunc);
	void UnRegCallback_OnRecvCompleted();
	void RegCallback_OnSendCompleted(void* pClass, PServerRecvSendEvent pCallBackFunc);
	void UnRegCallback_OnSendCompleted();
	void RegCallback_OnConnected(void* pClass, PServerConnectStatusEvent pCallBackFunc);
	void UnRegCallback_OnConnected();
	void RegCallback_OnDisconnected(void* pClass, PServerConnectStatusEvent pCallBackFunc);
	void UnRegCallback_OnDisonnected();

};
