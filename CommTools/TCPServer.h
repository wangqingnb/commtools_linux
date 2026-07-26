#pragma once
#include "CommTools.h"
#include "BaseLog.h"
#include <time.h>
#include <pthread.h>
#include <atomic>
#include <deque>
#include <set>
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
	int m_af;  // 地址族：AF_INET（纯 IPv4）或 AF_INET6（纯 IPv6，启动时 IPV6_V6ONLY=1）
	DWORD volatile m_IDCount;  //连接ID计数
	WORD m_MaxConnectNum;  //允许的最大连接数
	WORD m_Port; //监听端口
	int m_epfd_tf;    //数据传输事件描述符
	int m_WakeupFd;   //唤醒 TransferThread 的 eventfd

	enum SERVER_COMMAND_TYPE
	{
		SERVER_CMD_UPDATE_EPOLL,
		SERVER_CMD_CLOSE,
		SERVER_CMD_STOP
	};

	struct SERVER_COMMAND
	{
		SERVER_COMMAND_TYPE Type;
		long ConnectID;
		SERVER_COMMAND(SERVER_COMMAND_TYPE type, long id)
			: Type(type), ConnectID(id) {}
	};

	std::deque<SERVER_COMMAND> m_CommandQueue;
	std::set<long> m_ClosingConnections;
	pthread_cond_t m_CloseCond;
	pthread_cond_t m_CallbackCond;
	pthread_cond_t m_AcceptReadyCond;
	unsigned int m_ActiveCallbacks;
	bool m_AcceptReady;
	int m_AcceptStartError;


	//char AcceptBuffer[(sizeof(SOCKADDR_STORAGE) + 16) * 2];

	SOCKET ListenSocket;
	pthread_mutex_t m_mutex;
	char m_LocalServerIP[128];  //本端地址
	std::atomic<bool> m_ServerStarted;
	std::atomic<bool> m_AcceptStopping;
	std::atomic<bool> m_TransferStopping;
	std::atomic<bool> m_Stopping;
	std::atomic<bool> m_TransferOwnsStopCleanup;

	CBaseLog* LogObj;  //日志接口对象
	_U64  m_LastTick;  //最后活动时间
	DWORD volatile m_ConnTotal;  //当前连接数

	//存放socket信息的数组，第0项目保留，从第1项开始
	PSOCKET_INFORMATION SocketArray[MAX_CONNECT_NUM+1] = { 0 };

	//关闭连接后延迟释放，避免 TransferThread 仍持有 epoll data.ptr 时 UAF
	PSOCKET_INFORMATION m_PendingFree[MAX_CONNECT_NUM + 1];
	WORD m_PendingFreeCount;
	void DrainPendingFree();
	long FindSocketInformationIDXbyIDLocked(long ConnectID);
	void EnqueueCommandLocked(SERVER_COMMAND_TYPE type, long ConnectID);
	void WakeTransferThread();
	void ProcessCommands();
	bool UpdateEpoll(long ConnectID);
	void CloseConnectInternal(long ConnectID);
	void CloseAllConnections();
	void CheckIdleConnections();
	void CleanupStartFailure();
	void FinishCallback();
	void WaitForCallbacksLocked();

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
	std::atomic<bool> m_AcceptThreadCreated;
	static void* AcceptThread(void* lpParameter);

	//处理收发传输线程
	pthread_t  m_TransferThreadId;
	std::atomic<bool> m_TransferThreadCreated;
	static void* TransferThread(void* lpParameter);

	void ReTriggerEvent(PSOCKET_INFORMATION SI);
public:
	TCPServer(void);
	void setLogObj(CBaseLog* LogObj);
	// 对象必须由回调线程之外的所有者销毁；回调内允许 StopServer，但不可 delete TCPServer。
	virtual ~TCPServer(void);
	bool SetNonblocking(SOCKET sock);
	void SetNetPort(WORD Port);
	void SetLocalServerIP(const char* IP); // IPv4 如 0.0.0.0；IPv6 如 :: 或具体地址
	void SetAddressFamily(int af); // 仅 AF_INET / AF_INET6；切到 IPv6 且绑定仍为 0.0.0.0 时自动改为 ::
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
