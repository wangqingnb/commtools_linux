/*

	  Written by RockyWang
*/
#include "RK_Exception.h"
#include "TCPClient.h"
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <pthread.h>
#include "BaseLog.h"
#include "commroute.h"

TCPClient::TCPClient(void)
{
	LogObj = NULL;
	m_ServerPort = 1018;
	m_Terminated = false;
	m_OnRecvCompleted = NULL;
	m_OnSendCompleted = NULL;
	m_OnConnected = NULL;
	m_OnDisconnected = NULL;
	m_OnConnectFail = NULL;
	
	m_ConnectStatus = TS_NOTCONNECT;
	m_Terminated = false;
	
	m_TotalRecvBytes = 0;
	m_TotalSentBytes = 0;
	//CopyMemory(m_LocalIP, string("0.0.0.0").data(), 7);
	strcpy(m_LocalIP, "0.0.0.0");
	SocketInfo.Socket = -1;
	SocketInfo.bEvent = false;
	SocketInfo.bReading = false;
	SocketInfo.bSending = false;
	SocketInfo.BytesRECV = 0;
	SocketInfo.BytesSEND = 0;
	SocketInfo.ID = 0;
	

	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&m_mutex, &attr);
	pthread_mutexattr_destroy(&attr);

	//生成用于处理数据传输描述符
	m_epfd = epoll_create1(0);

	int ret = pthread_create(&m_ThreadId, NULL, WorkerThread, (void*)this);
	if (ret != 0)
	{
		char Msg[MAX_MSG_SIZE];
		snprintf(Msg, MAX_MSG_SIZE, "Create WorkerThread failed with error! Code:%d, %s\n", errno, strerror(errno));
		throw CRK_Exception(Msg);
	}

}


TCPClient::~TCPClient(void)
{
	m_Terminated = true;
	m_ConnectStatus = TS_NOTCONNECT;
	//等待线程结束


   //等待线程结束
	if (pthread_join(m_ThreadId, NULL) != 0) {
		LOG("WorkerThread join failed!");
	}
	else {
		LOG("WorkerThread was finished!");
	}

	close(m_epfd);
	pthread_mutex_destroy(&m_mutex);
}

bool TCPClient::SetNonblocking(SOCKET sock)
{
	int opts = fcntl(sock, F_GETFL);
	if (opts < 0)
	{
		LOG_F("fcntl(sock,GETFL),%s", strerror(errno));
		return false;
	}
	opts = opts | O_NONBLOCK;
	if (fcntl(sock, F_SETFL, opts) < 0)
	{
		LOG_F("fcntl(sock,SETFL,opts),%s", strerror(errno));
		return false;
	}
	return true;
}

bool TCPClient::Connect()
{

	if (m_ConnectStatus != TS_NOTCONNECT || m_ConnectStatus == TS_CONNECTING)
		return false;

	m_ConnectStatus = TS_CONNECTING;

	//char Msg[MAX_MSG_SIZE];
	//建立Socket
	memset(&SocketInfo, 0, sizeof(SOCKET_INFORMATION));
	SocketInfo.Socket = socket(AF_INET, SOCK_STREAM, 0);
	if (SocketInfo.Socket < 0)
	{
		LOG_F("Connecting, create socket error! Error Code: %d\n", errno);
		m_ConnectStatus = TS_NOTCONNECT;
		return false;
		//snprintf(Msg, MAX_MSG_SIZE, "Failed to get a socket Code: %d, %s\n", errno, strerror(errno));
		//throw CRK_Exception(Msg);
	}

	bzero(&ClientAddr, sizeof(ClientAddr));
	ClientAddr.sin_family = AF_INET;
	ClientAddr.sin_port = htons(0);
	ClientAddr.sin_addr.s_addr = inet_addr(m_LocalIP);


	
	if (bind(SocketInfo.Socket, (struct sockaddr*)&ClientAddr, sizeof(ClientAddr)) == -1)
	{
		LOG_F("Connecting, bind socket error! Error Code: %d, at %s %d\n", errno, __FILE__, __LINE__);
		m_ConnectStatus = TS_NOTCONNECT;
		return false;
		//snprintf(Msg, MAX_MSG_SIZE, "bind() failed with error! Code:%d\n", errno, strerror(errno));
		//throw CRK_Exception(Msg);
	}

	SetNonblocking(SocketInfo.Socket);

	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_addr.s_addr = inet_addr(m_ServerIP);
	//inet_pton(AF_INET, m_LocalIP, &LocalAddr.sin_addr);
	ServerAddr.sin_port = htons(m_ServerPort);
	
	if (connect(SocketInfo.Socket, (struct sockaddr*)&ServerAddr, sizeof(ServerAddr)) == -1) {
		if (errno != EINPROGRESS) {
			perror("connect error!");
			LOG_F("Connecting, connect socket error! Error Code: %d, at %s %d\n", errno, __FILE__, __LINE__);
			m_ConnectStatus = TS_NOTCONNECT;
			return false;
			//snprintf(Msg, MAX_MSG_SIZE, "connect failed with error! Code:%d\n", errno, strerror(errno));
			//throw CRK_Exception(Msg);
		}
	}

	ReTriggerEvent(&SocketInfo);
	return true;
}

void TCPClient::CloseConnect()
{
	close(SocketInfo.Socket);
	SocketInfo.Socket = -1;
}

void TCPClient::Disconnect()
{
	if (m_ConnectStatus == TS_CONNECTED)
	{
		LOG("Connect Closed！\n");
		CloseConnect();
		if (m_OnDisconnected != NULL)
			m_OnDisconnected(RegOnDisconnectedClass);
		m_ConnectStatus = TS_NOTCONNECT;
	}
}


//重新触发epoll事件
void TCPClient::ReTriggerEvent(PSOCKET_INFORMATION SI)
{
	struct epoll_event ev;  //ev用于注册事件
	//ev.data.ptr = SI;

	int op = SI->bEvent ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
	SI->bEvent = true;

	//设置用操作事件
	ev.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLHUP | EPOLLRDHUP;
	if (epoll_ctl(m_epfd, op, SI->Socket, &ev) < 0)
	{
		perror("epoll_ctl_mod");
		LOG_F("epoll_ctl_mod error!,%s\n", strerror(errno));
	}
}


bool TCPClient::PostRecv(char* buf, DWORD len)
{
	if (m_ConnectStatus != TS_CONNECTED) return false;
	if (SocketInfo.bReading) {
		errno = EAGAIN;
		return false;
	}


	//当socket信息无效或前面连接的接收操作没有完成是不允许再次投递接收操作
	if (SocketInfo.Socket == -1 || SocketInfo.bReading) return false;
	
	pthread_mutex_lock(&m_mutex);
	if (buf == NULL)  //使用内部缓冲区
	{
		if (len > MAX_PKG_LEN)
		{
			pthread_mutex_unlock(&m_mutex);
			errno = EOVERFLOW;
			return false;
		}
		SocketInfo.RecvDataBuf.buf = SocketInfo.RecvBuffer;
	}
	else
		SocketInfo.RecvDataBuf.buf = buf;
	SocketInfo.RecvDataBuf.len = 0;  
	SocketInfo.BytesRECV = len; //期待接收的字节长度
	SocketInfo.bReading = true;
	ReTriggerEvent(&SocketInfo);
	pthread_mutex_unlock(&m_mutex);
	return true;
}


bool TCPClient::PostSend(char* buf, DWORD len)
{
	if (m_ConnectStatus != TS_CONNECTED) {
		errno = ECONNABORTED;
		return false;
	}

	//当socket信息无效或前面连接的发送操作没有完成是不允许再次投递发送操作
	if (SocketInfo.Socket == -1)  
	{
		errno = ECONNABORTED;
		return false;
	}
	
	if (SocketInfo.bSending) {
		errno = EAGAIN;
		return false;
	}
	
	//发送的数据大于内置缓冲大小
	if (len > DATA_BUFSIZE) {
		errno = EINVAL;
		return false;
	}

	pthread_mutex_lock(&m_mutex);
	memcpy(SocketInfo.SendBuffer, buf, len);
	SocketInfo.SendDataBuf.buf = SocketInfo.SendBuffer;
	SocketInfo.SendDataBuf.len = 0;
	SocketInfo.BytesSEND = len; //期待发送的字节长度
	SocketInfo.bSending = true;
	ReTriggerEvent(&SocketInfo);
	pthread_mutex_unlock(&m_mutex);
	return true;
}


void* TCPClient::WorkerThread(void* lpParameter)
{
	TCPClient* inst = (TCPClient*)lpParameter;

	int nfds;  //返回的等待事件数量
	struct epoll_event Events[MAX_EPWEVENT_NUM]; //声明epoll_event结构体的变量数组用于回传要处理的事件
	PSOCKET_INFORMATION SI = &inst->SocketInfo;
	while (!inst->m_Terminated)
	{
		if (inst->m_ConnectStatus == TS_NOTCONNECT || SI->Socket == -1 )
		{
			usleep(50 * 1000);
			continue;
		}

		nfds = epoll_wait(inst->m_epfd, Events, MAX_EPWEVENT_NUM, WAIT_TIME_OUT);
		if (nfds == 0)
		{
			usleep(50 * 1000);
			continue;
		}
		for (int i = 0; i < nfds; ++i)
		{
			if (inst->m_ConnectStatus == TS_CONNECTING)
			{
				int so_error = 0;
				socklen_t so_len = sizeof(so_error);
				bool writable = (Events[i].events & EPOLLOUT) != 0;
				bool failed = (Events[i].events & (EPOLLERR | EPOLLHUP)) != 0;
				if (writable || failed)
				{
					if (getsockopt(SI->Socket, SOL_SOCKET, SO_ERROR, &so_error, &so_len) < 0)
						so_error = errno;
				}
				if (writable && !failed && so_error == 0)
				{
					SI->FirstActiveTick = GetTickCount64();
					LOG_S("connected!\n");
					inst->m_ConnectStatus = TS_CONNECTED;
					if (inst->m_OnConnected != NULL)
						inst->m_OnConnected(inst->RegOnConnectedClass);
					break;
				}
				else if (failed || so_error != 0 || (Events[i].events & EPOLLIN)) {
					LOG_S("connect failed!\n");
					if (inst->m_OnConnectFail != NULL)
						inst->m_OnConnectFail(inst->RegOnConnectFailClass);
					inst->CloseConnect();
					inst->m_ConnectStatus = TS_NOTCONNECT;
					break;
				}
			} else if (Events[i].events & EPOLLHUP || Events[i].events & EPOLLRDHUP) 
			{
				inst->Disconnect();
				break; //已经断开连接就不用继续检查其他事件
			}
			if (Events[i].events & EPOLLIN && SI->bReading) //收到数据就绪事件，那么进行读入。
			{

				//数据缓冲区的接收数据的位置
				char* pBaseAddr = SI->RecvDataBuf.buf;

				//期望获取的数据字节数
				long ExpRecvNum = SI->BytesRECV;

				//每次调用recv返回的返回收到的数据字节数
				long recvNum = 0;
				//已读取的字节总数
				long recvNumTotal = 0;
				long iOffset = 0;  //缓存区的偏移量

				//本次读数据是否完成，有两种情况，1是完成读取期待的数据量，2是当前没有更多的数据可读
				bool bReadCompleted = false;
				//开始读取数据
				while (1)
				{
					recvNum = recv(SI->Socket, pBaseAddr + iOffset, ExpRecvNum, 0);
					//读取失败按返回码进行相应处理
					if (recvNum < 0)
					{
						if (errno == EAGAIN)
						{
							// 当errno为EAGAIN时,表示当前缓冲区已无数据可读,就完成本次读
							bReadCompleted = true;
							SI->RecvDataBuf.len = recvNumTotal;  //设置本次已读的字节数
							break;
						}
						else if (errno == ECONNRESET)
						{
							// 对方发送了RST
							inst->Disconnect();
							break;
						}
						else if (errno == EINTR)
						{
							// 被信号中断重新再读取
							continue;
						}
						else
						{
							//其他不可弥补的错误
							inst->Disconnect();
							break;
						}
					}
					else if (recvNum == 0)
					{
						// 这里表示对端的socket已正常关闭.发送过FIN了。
						inst->Disconnect();
						break;
					}

					//下面是读取到数据后的处理

					recvNumTotal += recvNum; //累计已读的数据量
					ExpRecvNum -= recvNum;
					//已经完成期望读取的数据就不继续读取
					if (ExpRecvNum == 0)
					{
						SI->RecvDataBuf.len = recvNumTotal; //设置本次已读的字节数
						bReadCompleted = true;
						break;
					}
					else //没有完成读取期待长度的数据就继续读
					{
						iOffset += recvNum;
						continue;   // 需要再次读取
					}
				}
				__sync_add_and_fetch(&inst->m_TotalRecvBytes, recvNumTotal);
				SI->LastActiveTick = GetTickCount64();
				SI->RecvTotalBytes += recvNumTotal;
				if (bReadCompleted && inst->m_OnRecvCompleted)
				{
					inst->m_OnRecvCompleted(SI->RecvDataBuf.buf, SI->RecvDataBuf.len,
						SI->BytesRECV, inst->RegOnRecvCompletedClass);
				}
				SI->bReading = false;
			}
			if (Events[i].events & EPOLLOUT && SI->bSending)//收到可以发送数据的事件，那么进行发送。
			{
				//数据缓冲区的发送数据的位置
				char* pBaseAddr = SI->SendDataBuf.buf;

				//期望获取的数据字节数
				long ExpSendNum = SI->BytesSEND;

				//每次调用send发生成功的数据字节数
				long sendNum = 0;
				
				//已发送的字节总数
				long sendNumTotal = 0;
				long iOffset = 0;  //缓存区的偏移量

				//本次发送数据是否完成，有两种情况，1是完成发送期待的数据量，2是当前发送缓冲已满不能发送出去
				bool bSendCompleted = false;
				
				//开始发送数据
				while (1) {
					sendNum = send(SI->Socket, pBaseAddr + iOffset, ExpSendNum, 0);
					//发送失败按返回码进行相应处理
					if (sendNum < 0)
					{
						if (errno == EAGAIN)
						{
							// 当errno为EAGAIN时,表示当前缓冲区满,就完成本次发送
							bSendCompleted = true;
							SI->SendDataBuf.len = sendNumTotal;  //设置本次已读的字节数
							break;
						}
						else if (errno == ECONNRESET)
						{
							// 对方发送了RST
							inst->Disconnect();
							break;
						}
						else if (errno == EINTR)
						{
							// 被信号中断重新再发送
							continue;
						}
						else
						{
							//其他不可弥补的错误
							inst->Disconnect();
							break;
						}
					}
					//下面是发送数据成功后的处理
					sendNumTotal += sendNum; //累计已发送的数据量
					ExpSendNum -= sendNum;
					//已经完成期望发送的数据就不继续读取
					if (ExpSendNum == 0)
					{
						SI->SendDataBuf.len = sendNumTotal; //设置本次已读的字节数
						bSendCompleted = true;
						break;
					}
					else //发送没有达到期待长度的数据就继续发
					{
						iOffset += sendNum;
						continue;   // 需要再次发送
					}
				}
				__sync_add_and_fetch(&inst->m_TotalSentBytes, sendNumTotal);
				SI->LastActiveTick = GetTickCount64();
				SI->SendTotalBytes += sendNumTotal;
				if (bSendCompleted && inst->m_OnSendCompleted)
				{
					inst->m_OnSendCompleted(SI->SendDataBuf.buf, SI->SendDataBuf.len,
						SI->BytesSEND, inst->RegOnSendCompletedClass);
					
				}
				SI->bSending = false;
			}
		}

	}
	return 0;
}

void TCPClient::SetServerPort(WORD Port)
{
	m_ServerPort = Port;
}

void TCPClient::SetServerIP(char* IP)
{
	memset(m_ServerIP, 0, sizeof(m_ServerIP));
	memcpy(m_ServerIP, IP, strlen(IP));
}

void TCPClient::SetLocalIP(char* IP)
{
	memset(m_LocalIP, 0, sizeof(m_LocalIP));
	memcpy(m_LocalIP, IP, strlen(IP));
}

bool TCPClient::GetStatistics(PTCPLinkStatics pServerStatics)
{
	if (m_ConnectStatus != TS_CONNECTED)
		return false;
	pServerStatics->RecvTotalBytes = SocketInfo.RecvTotalBytes;
	pServerStatics->SendTotalBytes = SocketInfo.SendTotalBytes;
	pServerStatics->ConnectedDT = SocketInfo.ConnectedDT;
	pServerStatics->FirstActiveTick = SocketInfo.FirstActiveTick;
	pServerStatics->LastActiveTick = SocketInfo.LastActiveTick;
	return true;
}



void TCPClient::RegCallback_OnRecvCompleted(void* pClass, PRecvSendEvent pCallBackFunc)
{
	m_OnRecvCompleted = pCallBackFunc;
	RegOnRecvCompletedClass = pClass;
}

void TCPClient::UnRegCallback_OnRecvCompleted()
{
	m_OnRecvCompleted = NULL;
	RegOnRecvCompletedClass = NULL;
}

void TCPClient::RegCallback_OnSendCompleted(void* pClass, PRecvSendEvent pCallBackFunc)
{
	m_OnSendCompleted = pCallBackFunc;
	RegOnSendCompletedClass = pClass;
}

void TCPClient::UnRegCallback_OnSendCompleted()
{
	m_OnSendCompleted = NULL;
	RegOnSendCompletedClass = NULL;
}

void TCPClient::RegCallback_OnConnected(void* pClass, PConnectStatusEvent pCallBackFunc)
{
	m_OnConnected = pCallBackFunc;
	RegOnConnectedClass = pClass;
}

void TCPClient::UnRegCallback_OnConnected()
{
	m_OnConnected = NULL;
	RegOnConnectedClass = NULL;
}

void TCPClient::RegCallback_OnDisconnected(void* pClass, PConnectStatusEvent pCallBackFunc)
{
	m_OnDisconnected = pCallBackFunc;
	RegOnDisconnectedClass = pClass;
}

void TCPClient::UnRegCallback_OnDisconnected()
{
	m_OnDisconnected = NULL;
	RegOnDisconnectedClass = NULL;
}

void TCPClient::RegCallback_OnConnectFail(void* pClass, PConnectStatusEvent pCallBackFunc)
{
	m_OnConnectFail = pCallBackFunc;
	RegOnConnectFailClass = pClass;
}

void TCPClient::UnRegCallback_OnConnectFail()
{
	m_OnConnectFail = NULL;
	RegOnConnectFailClass = NULL;
}


void TCPClient::setLogObj(CBaseLog* LogObj)
{
	this->LogObj = LogObj;
}
