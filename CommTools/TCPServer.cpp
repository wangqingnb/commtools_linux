/*

	  Written by RockyWang
*/
#include "RK_Exception.h"
#include "BaseLog.h"
#include "TCPServer.h"
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include "SockPubDefine.h"
#include <pthread.h>
#include "commroute.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>

namespace {

// Format peer address for logging (IPv4 / IPv6).
bool FormatSockAddr(const struct sockaddr* addr, socklen_t addrlen, char* buf, size_t buflen)
{
	if (buf == NULL || buflen == 0)
		return false;
	buf[0] = '\0';
	if (addr == NULL || addrlen < (socklen_t)sizeof(sa_family_t))
		return false;

	if (addr->sa_family == AF_INET) {
		const struct sockaddr_in* a4 = (const struct sockaddr_in*)addr;
		return inet_ntop(AF_INET, &a4->sin_addr, buf, buflen) != NULL;
	}
	if (addr->sa_family == AF_INET6) {
		const struct sockaddr_in6* a6 = (const struct sockaddr_in6*)addr;
		return inet_ntop(AF_INET6, &a6->sin6_addr, buf, buflen) != NULL;
	}
	return false;
}

} // namespace

TCPServer::TCPServer(void)
{
	LogObj = NULL;
	m_af = AF_INET;  //默认协议为IPV4
	m_Port = 1018;
	m_MaxTimeNoActive = 600;
	m_ServerStarted = false;
	m_Terminated = false;
	m_OnRecvCompleted = NULL;
	m_OnSendCompleted = NULL;
	m_OnConnected = NULL;
	m_OnDisconnected = NULL;

	RegOnRecvCompletedClass = NULL;
	RegOnSendCompletedClass = NULL;
	RegOnConnectedClass = NULL;
	RegOnDisconnectedClass = NULL;
	strcpy(m_LocalServerIP, "0.0.0.0");

	m_IDCount = 0;
	m_epfd_tf = -1;
	m_PendingFreeCount = 0;
	memset(m_PendingFree, 0, sizeof(m_PendingFree));

	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&m_mutex, &attr);
	pthread_mutexattr_destroy(&attr);
	m_MaxConnectNum = MAX_CONNECT_NUM;
}


TCPServer::~TCPServer(void)
{
	if (m_ServerStarted)
		StopServer();
	DrainPendingFree();
	pthread_mutex_destroy(&m_mutex);
}

bool TCPServer::SetNonblocking(SOCKET sock)
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

void TCPServer::StartServer()
{
	if (m_ServerStarted)
	{
		LOG("Server already Started, cannot start twice!\n");
		return;
	}
	m_ConnTotal = 0;
	m_LastTick = GetTickCount64();
	m_Terminated = false;
	m_TotalRecvBytes = 0;
	m_TotalSentBytes = 0;

	//生成用于处理数据传输描述符
	m_epfd_tf = epoll_create1(0);


	//建立监听Socket
	ListenSocket = socket(m_af, SOCK_STREAM, 0);
	if (ListenSocket < 0)
	{
		LOG_F("Server Socket error! Error Code: %d at %s %d\n", errno,  __FILE__, __LINE__);
		char buf[MAX_MSG_SIZE];
		snprintf(buf, MAX_MSG_SIZE, "Failed to get a socket Code: %d, %s\n",  errno, strerror(errno));
		throw CRK_Exception(buf);
	}

	int reuse = 1;
	if (setsockopt(ListenSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
	{
		LOG_F("setsockopt(SO_REUSEADDR) failed! %s\n", strerror(errno));
	}

	// 纯 IPv6：强制 V6ONLY，避免误收 IPv4-mapped（双栈后续再做）
	if (m_af == AF_INET6) {
		int v6only = 1;
		if (setsockopt(ListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) < 0)
		{
			LOG_F("setsockopt(IPV6_V6ONLY) failed! %s\n", strerror(errno));
		}
	}

	//获取服务地址信息
	struct addrinfo hints,*pAddInfo;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = m_af;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	char sPort[20] = { 0 };
	snprintf(sPort, sizeof(sPort), "%d", m_Port);
	char Msg[MAX_MSG_SIZE];
	DWORD rc = getaddrinfo(m_LocalServerIP, sPort, &hints, &pAddInfo);
	if (rc != 0) {
		close(ListenSocket);
		ListenSocket = -1;
		LOG_F("getaddrinfo() failed with error, %s at %s %d\n", errno, __FILE__, __LINE__);
		snprintf(Msg, MAX_MSG_SIZE, "getaddrinfo() failed with error %d, %s at %s %d\n", errno, gai_strerror(rc), __FILE__, __LINE__);
		throw CRK_Exception(Msg);
	}

	//绑定监听Socket 重试6次
	int iRetry = 1;
	int iMaxRetry = 6;
	do {
		if (bind(ListenSocket, pAddInfo->ai_addr, (int)pAddInfo->ai_addrlen) < 0)
		{
			if (iRetry <= iMaxRetry) {
				LOG_F("bind() failed with error! Code:%d， wait 10 secs! retry times=%d \n", errno, iRetry);
				usleep(10000 * 1000); //等待10秒
			} else {
				freeaddrinfo(pAddInfo);
				close(ListenSocket);
				ListenSocket = -1;
				LOG_F("bind() failed with error! Code:%d  at %s %d\n", errno, __FILE__, __LINE__);
				snprintf(Msg, MAX_MSG_SIZE, "bind() failed with error! Code:%d, %s\n", errno, strerror(errno));
				throw CRK_Exception(Msg);
			}
		}
		else
			break;
		iRetry++;
	} while (1);

	freeaddrinfo(pAddInfo);

	if (listen(ListenSocket, 5) < 0) {
		LOG_F("listen() failed with error! Code:%d  at %s %d\n", errno, __FILE__, __LINE__);
		snprintf(Msg, MAX_MSG_SIZE, "listen() failed with error! Code:%d, %s\n", errno, strerror(errno));
		throw CRK_Exception(Msg);
	}
	SetNonblocking(ListenSocket);

	//创建Accept线程用来处理接受连接接入
	int ret = pthread_create(&m_AcceptThreadId, NULL, AcceptThread, (void*)this);
	if (ret != 0)
	{
		LOG_F("Create AcceptThread failed with error! Code:%d  at %s %d\n", errno, __FILE__, __LINE__);
		snprintf(Msg, MAX_MSG_SIZE, "Create AcceptThread failed with error! Code:%d, %s\n", errno, strerror(errno));
		throw CRK_Exception(Msg);
	}
	
	//创建通讯传输线程用来处理数据接收和发送
	ret = pthread_create(&m_TransferThreadId, NULL, TransferThread, (void*)this);
	if (ret != 0)
	{
		LOG_F("Create TransferThread with error! Code:%d  at %s %d\n", errno, __FILE__, __LINE__);
		snprintf(Msg, MAX_MSG_SIZE, "Create TransferThread failed with error! Code:%d, %s\n", errno, strerror(errno));
		throw CRK_Exception(Msg);
	}

	m_ServerStarted = true;


}

//数据收发处理线程
void* TCPServer::TransferThread(void* lpParameter)
{
	TCPServer* inst = (TCPServer*)lpParameter;  //通过参数获取类实例
	int nfds;  //返回的等待事件数量
	struct epoll_event Events[MAX_EPWEVENT_NUM]; //声明epoll_event结构体的变量数组用于回传要处理的事件
	
	while (1)
	{
		inst->DrainPendingFree();
		if (inst->m_Terminated) break;
		nfds = epoll_wait(inst->m_epfd_tf, Events, MAX_EPWEVENT_NUM, WAIT_TIME_OUT);
		for (int i = 0; i < nfds; ++i)
		{
			//获取socket信息
			PSOCKET_INFORMATION SI = (PSOCKET_INFORMATION)Events[i].data.ptr;
			//无效或本批次已关闭的连接跳过
			if (SI == NULL || SI->Socket < 0)
				continue;

			//关闭/错误事件
			if (Events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)) {
				inst->CloseConnect(SI->ID);
				continue;
			}
			if (Events[i].events & EPOLLIN)//收到数据，那么进行读入。
			{
				if (!SI->bReading)  //当前不为读取状态就处理下一个事件
					continue;

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
						if (errno == EAGAIN || errno == EWOULDBLOCK)
						{
							// 当errno为EAGAIN时,表示当前缓冲区已无数据可读,就完成本次读
							bReadCompleted = true;
							SI->RecvDataBuf.len = recvNumTotal;  //设置本次已读的字节数
							break;
						}
						else if (errno == ECONNRESET)
						{
							// 对方发送了RST
							inst->CloseConnect(SI->ID);
							SI = NULL; //置为无效
							break;
						}
						else if (errno == EINTR)
						{
							// 被信号中断等待10毫秒重新再读取
							usleep(10 * 1000);
							continue;
						}
						else
						{
							//其他不可弥补的错误，如线路异常中断等
							inst->CloseConnect(SI->ID);
							SI = NULL; //置为无效
							break;
						}
					}
					else if (recvNum == 0)
					{
						// 这里表示对端的socket已正常关闭.发送过FIN了。
						inst->CloseConnect(SI->ID);
						SI = NULL; //置为无效
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
				__sync_add_and_fetch(&inst->m_TotalRecvBytes, recvNumTotal);  //更新当前连接总接收字节数信息
				if (SI) {
					SI->LastActiveTick = GetTickCount64();  //更新连接的最后活动时间戳
					SI->bReading = false;  //设置当前状态为未在读取
					inst->ReTriggerEvent(SI); // 去掉 EPOLLIN，避免 LT 下空转

					//处理已注册的接收成功回调函数
					if (bReadCompleted && inst->m_OnRecvCompleted)
					{
						inst->m_OnRecvCompleted(SI->ID, SI->RecvDataBuf.buf, SI->RecvDataBuf.len,
							SI->BytesRECV, inst->RegOnRecvCompletedClass);
					}
				}
				else
					continue;
			}
			if (SI == NULL || SI->Socket < 0)
				continue;
			if (Events[i].events & EPOLLOUT)//收到可以发送数据的事件，那么进行发送。
			{
				if (!SI->bSending)
					continue;
				//数据缓冲区的发送数据的位置
				char* pBaseAddr = SI->SendDataBuf.buf;

				//从已发送偏移继续，直到发完或 EAGAIN
				long iOffset = (long)SI->BytesSentSoFar;
				long ExpSendNum = (long)SI->BytesSEND - iOffset;
				if (ExpSendNum <= 0) {
					SI->bSending = false;
					inst->ReTriggerEvent(SI);
					continue;
				}

				long sendNum = 0;
				long sendNumThisRound = 0;
				bool bSendCompleted = false;
				bool bClosed = false;
				
				//开始发送数据
				while (ExpSendNum > 0) {
					sendNum = send(SI->Socket, pBaseAddr + iOffset, ExpSendNum, 0);
					//发送失败按返回码进行相应处理
					if (sendNum < 0)
					{
						if (errno == EAGAIN || errno == EWOULDBLOCK)
						{
							// 缓冲满：保留 bSending，等下次 EPOLLOUT 续传
							SI->BytesSentSoFar = (DWORD)iOffset;
							SI->SendDataBuf.len = SI->BytesSentSoFar;
							break;
						}
						else if (errno == ECONNRESET)
						{
							inst->CloseConnect(SI->ID);
							bClosed = true;
							break;
						}
						else if (errno == EINTR)
						{
							continue;
						}
						else
						{
							inst->CloseConnect(SI->ID);
							bClosed = true;
							break;
						}
					}
					sendNumThisRound += sendNum;
					iOffset += sendNum;
					ExpSendNum -= sendNum;
					if (ExpSendNum == 0)
					{
						SI->BytesSentSoFar = (DWORD)iOffset;
						SI->SendDataBuf.len = SI->BytesSentSoFar;
						bSendCompleted = true;
						break;
					}
				}
				__sync_add_and_fetch(&inst->m_TotalSentBytes, sendNumThisRound);
				if (bClosed)
					continue;
				if (SI->Socket < 0)
					continue;
				SI->LastActiveTick = GetTickCount64();
				if (bSendCompleted)
				{
					SI->bSending = false;
					inst->ReTriggerEvent(SI); // 去掉 EPOLLOUT
					if (inst->m_OnSendCompleted)
					{
						inst->m_OnSendCompleted(SI->ID, SI->SendDataBuf.buf, SI->SendDataBuf.len,
							SI->BytesSEND, inst->RegOnSendCompletedClass);
					}
				}
			}
		}

	}
	sleep(1);
	return 0;
}

void* TCPServer::AcceptThread(void* lpParameter)
{
	TCPServer* inst = (TCPServer*)lpParameter;
	int nfds;  //返回的等待事件数量
	
	struct epoll_event Events[MAX_EPWEVENT_NUM]; //声明epoll_event结构体的变量数组用于回传要处理的事件

	struct epoll_event ev;  //ev用于注册事件

	//生成用于处理accept的epoll专用的文件描述符
	int epfd = epoll_create1(0);

	//设置与要处理的事件相关的文件描述符
	ev.data.fd = inst->ListenSocket;
	//水平触发：可读时会持续通知，配合下面 accept 排空
	ev.events = EPOLLIN;

	//注册epoll事件
	epoll_ctl(epfd, EPOLL_CTL_ADD, inst->ListenSocket, &ev);

	while (1)
	{
		if (inst->m_Terminated) break;
		nfds = epoll_wait(epfd, Events, MAX_EPWEVENT_NUM, WAIT_TIME_OUT);
		for (int i = 0; i < nfds; ++i)
		{
			//如果新监测到一个SOCKET用户连接到了绑定的SOCKET端口，建立新的连接。
			if (Events[i].data.fd == inst->ListenSocket)
			{
				while (1)
				{
					struct sockaddr_storage clientaddr;
					socklen_t clilen = sizeof(clientaddr);
					SOCKET connfd = accept(inst->ListenSocket, (sockaddr*)&clientaddr, &clilen);
					if (connfd < 0) {
						if (errno == EAGAIN || errno == EWOULDBLOCK)
							break;
						LOG_SF("Accept socket error! %s, at %s %d\n", strerror(errno), __FILE__, __LINE__);
						break;
					}
					char ipstr[INET6_ADDRSTRLEN] = { 0 };
					if (!FormatSockAddr((sockaddr*)&clientaddr, clilen, ipstr, sizeof(ipstr)))
						strncpy(ipstr, "?", sizeof(ipstr) - 1);
					LOG_SF("new client connected!,ip:%s\n", ipstr);

					if (inst->m_ConnTotal >= inst->m_MaxConnectNum) {
						LOG_SF("too many connection!, current connects:%d, MaxConnect:%d\n", inst->m_ConnTotal, inst->m_MaxConnectNum);
						close(connfd);
						continue;
					}
					//保存Socket相关信息
					pthread_mutex_lock(&inst->m_mutex);
					inst->m_ConnTotal++;
					inst->SocketArray[inst->m_ConnTotal] = PSOCKET_INFORMATION(malloc(sizeof(SOCKET_INFORMATION)));
					if (inst->SocketArray[inst->m_ConnTotal] == NULL)
					{
						LOG_SF("SocketArray: malloc() failed with error! ,%s\n", strerror(errno));
						inst->m_ConnTotal--;
						pthread_mutex_unlock(&inst->m_mutex);
						close(connfd);
						break;
					}

					// Fill in the details of our accepted socket.
					PSOCKET_INFORMATION SI = inst->SocketArray[inst->m_ConnTotal];
					SI->Socket = connfd;
					SI->ID = inst->GenID();
					SI->bEvent = false;
					SI->BytesSEND = 0;
					SI->BytesRECV = 0;
					SI->BytesSentSoFar = 0;
					SI->bReading = false;
					SI->bSending = false;
					SI->FirstActiveTick = GetTickCount64();
					SI->LastActiveTick = SI->FirstActiveTick;

					pthread_mutex_unlock(&inst->m_mutex);

					inst->SetNonblocking(connfd);

					if (inst->m_OnConnected != NULL)
						inst->m_OnConnected(SI->ID, inst->RegOnConnectedClass);
				}
			}
		}
	}
	close(epfd);  //关闭epoll描述符
	sleep(1);
	return 0;
}

//重新触发epoll事件（水平触发；仅在投递读/写时注册对应兴趣）
void TCPServer::ReTriggerEvent(PSOCKET_INFORMATION SI)
{
	if (SI == NULL || SI->Socket < 0 || m_epfd_tf < 0)
		return;

	struct epoll_event ev;
	ev.data.ptr = SI;
	ev.events = EPOLLHUP | EPOLLRDHUP | EPOLLERR;
	if (SI->bReading)
		ev.events |= EPOLLIN;
	if (SI->bSending)
		ev.events |= EPOLLOUT;

	int op = SI->bEvent ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
	SI->bEvent = true;

	if (epoll_ctl(m_epfd_tf, op, SI->Socket, &ev) < 0)
	{
		perror("epoll_ctl_mod");
		LOG_F("epoll_ctl_mod error!,%s\n", strerror(errno));
	}
}

bool TCPServer::PostRecv(long ConnectID, char* buf, DWORD len)
{
	if (len <= 0) {
		errno = EINVAL;
		return false;
	}

	if (!m_ServerStarted)
	{
		errno = ENOTSOCK;
		return false;
	}

	pthread_mutex_lock(&m_mutex);
	long idx = GetSocketInformationIDXbyID(ConnectID);
	if (idx < 0) {
		errno = ENODATA;
		pthread_mutex_unlock(&m_mutex);
		return false;
	}

	PSOCKET_INFORMATION SI;
	SI = SocketArray[idx];
	//当socket信息无效
	if (SI == NULL) {
		errno = ENODATA;
		pthread_mutex_unlock(&m_mutex);
		return false;
	}

	//前面连接的读取操作没有完成是不允许再次投递读取操作
	if (SI->bReading) {
		errno = EAGAIN;
		pthread_mutex_unlock(&m_mutex);
		return false;
	}

	if (buf == NULL)  //使用内部缓冲区
	{
		if (len > MAX_PKG_LEN)
		{
			pthread_mutex_unlock(&m_mutex);
			errno = EOVERFLOW;
			return false;
		}
		SI->RecvDataBuf.buf = SI->RecvBuffer;
	} else
		SI->RecvDataBuf.buf = buf;

	SI->RecvDataBuf.len = 0;
    SI->BytesRECV = len;
	
	SI->bReading = true;   //设置当前状态为正在读取

	ReTriggerEvent(SI);
	pthread_mutex_unlock(&m_mutex);
	
	return true;
}


bool TCPServer::PostSend(long ConnectID, char* buf, DWORD len)
{
	if (!m_ServerStarted)
	{
		errno = ENOTSOCK;
		return false;
	}

	//发送的数据大于内置缓冲大小
	if (len > DATA_BUFSIZE) {
		errno = EINVAL;
		return false;
	}


	pthread_mutex_lock(&m_mutex);
	long idx = GetSocketInformationIDXbyID(ConnectID);
	if (idx < 0)
	{
		pthread_mutex_unlock(&m_mutex);
		errno = ENODATA;
		return false;
	}

	PSOCKET_INFORMATION SI;
	SI = SocketArray[idx];

	//socket信息无效
	if (SI == NULL) {
		pthread_mutex_unlock(&m_mutex);
		errno = ENODATA;
		return false;
	}
	
	//前面连接的发送操作没有完成是不允许再次投递发送操作
	if (SI->bSending) {
		pthread_mutex_unlock(&m_mutex);
		errno = EAGAIN;
		return false;
	}

	SI->SendDataBuf.len = 0;
	SI->BytesSEND = len;
	SI->BytesSentSoFar = 0;
	memcpy(SI->SendBuffer, buf, len);
	SI->SendDataBuf.buf = SI->SendBuffer;
	SI->bSending = true;   //设置当前状态为正在发送

	ReTriggerEvent(SI);
	pthread_mutex_unlock(&m_mutex);
	return true;
}

void TCPServer::DrainPendingFree()
{
	pthread_mutex_lock(&m_mutex);
	for (WORD i = 0; i < m_PendingFreeCount; i++)
	{
		if (m_PendingFree[i] != NULL)
		{
			free(m_PendingFree[i]);
			m_PendingFree[i] = NULL;
		}
	}
	m_PendingFreeCount = 0;
	pthread_mutex_unlock(&m_mutex);
}

void TCPServer::CloseConnect(long ConnectID)
{
	pthread_mutex_lock(&m_mutex);
	long Index = GetSocketInformationIDXbyID(ConnectID);
	if (Index < 0) {
		pthread_mutex_unlock(&m_mutex);
		return;
	}

	PSOCKET_INFORMATION SI = SocketArray[Index];

	long tmpID = SI->ID;

	LOG_F("Client: %d Closed\n", tmpID);
	if (m_OnDisconnected != NULL)
		m_OnDisconnected(tmpID, RegOnDisconnectedClass);

	if (SI->bEvent && SI->Socket >= 0 && m_epfd_tf >= 0)
	{
		epoll_ctl(m_epfd_tf, EPOLL_CTL_DEL, SI->Socket, NULL);
		SI->bEvent = false;
	}
	if (SI->Socket >= 0)
	{
		close(SI->Socket);
		SI->Socket = -1;
	}

	long i = Index;
	while (i <= m_ConnTotal)
	{
		if (i == m_ConnTotal)
		{
			SocketArray[i] = NULL;
		}
		else
		{
			SocketArray[i] = SocketArray[i + 1];
		}
		i++;
	}
	m_ConnTotal--;

	if (m_PendingFreeCount < MAX_CONNECT_NUM + 1)
	{
		m_PendingFree[m_PendingFreeCount++] = SI;
	}
	else
	{
		free(SI);
	}

	pthread_mutex_unlock(&m_mutex);

}

long TCPServer::GetSocketInformationIDXbyID(long ConnectID)
{
	long result = -1;
	if (ConnectID <= 0)
		return result;

	pthread_mutex_lock(&m_mutex);
	//bool bFound = false;
	for (DWORD i = 1; i <= m_ConnTotal; i++)
	{
		if (ConnectID == SocketArray[i]->ID) {
			result = i;
			break;
		}
	}

	pthread_mutex_unlock(&m_mutex);
	return result;
}

void TCPServer::StopServer()
{
	if (m_ServerStarted)
	{
		m_Terminated = true;

		//等待线程结束
		if (pthread_join(m_TransferThreadId, NULL) != 0) {
			LOG_F("TransferThread join failed!\n");
		}
		else {
			LOG_F("TransferThread was finished!\n");
		}

		//等待线程结束
		if (pthread_join(m_AcceptThreadId, NULL) != 0) {
			LOG_F("AcceptThread join failed!\n");
		}
		else {
			LOG_F("AcceptThread was finished!\n");
		}


		close(m_epfd_tf);
		m_epfd_tf = -1;
		DrainPendingFree();

		//关闭监听socket
		shutdown(ListenSocket, SHUT_RDWR);
		close(ListenSocket);
		LOG_F("listener socket is closed!\n");

		m_ServerStarted = false;
	}
}

void TCPServer::setLogObj(CBaseLog* LogObj)
{
	this->LogObj = LogObj;
}

long TCPServer::GenID()
{
	return __sync_add_and_fetch(&m_IDCount, 1);
}

void TCPServer::SetAddressFamily(int af)
{
	if (af != AF_INET && af != AF_INET6) {
		LOG_F("SetAddressFamily: unsupported af=%d, keep af=%d (use AF_INET or AF_INET6)\n", af, m_af);
		return;
	}
	m_af = af;
	// 切换地址族时，若仍是对端族的“任意地址”默认值，则同步为当前族默认
	if (af == AF_INET6 && strcmp(m_LocalServerIP, "0.0.0.0") == 0)
		strcpy(m_LocalServerIP, "::");
	else if (af == AF_INET && strcmp(m_LocalServerIP, "::") == 0)
		strcpy(m_LocalServerIP, "0.0.0.0");
}

void TCPServer::SetMaxConnectNum(WORD num)
{
	if (num >= MAX_CONNECT_NUM)
		m_MaxConnectNum = MAX_CONNECT_NUM;
	else if (num < 1)
		m_MaxConnectNum = 1;
	else m_MaxConnectNum = num;

}

void TCPServer::SetNetPort(WORD Port)
{
	m_Port = Port;
};

void TCPServer::SetMaxTimeNoActive(WORD secs)
{
	m_MaxTimeNoActive = secs;
}




void TCPServer::SetLocalServerIP(const char* IP)
{
	if (IP == NULL)
		return;
	memset(m_LocalServerIP, 0, sizeof(m_LocalServerIP));
	strncpy(m_LocalServerIP, IP, sizeof(m_LocalServerIP) - 1);
}

/*
void TCPServer::CloseClient(long ConnectID)
{
	if (!m_ServerStarted) return;
	long Index = GetSocketInformationIDXbyID(ConnectID);
	if (Index <= 0) return;
	PSOCKET_INFORMATION SI = SocketArray[Index];
	close(SI->Socket); 
	//  never call CloseConnect(I) cause race conditon
}
*/


bool TCPServer::GetStatistics(long ConnectID, PTCPLinkStatics pServerStatics)
{
	if (!m_ServerStarted)
		return false;
	long i = GetSocketInformationIDXbyID(ConnectID);
	if (i < 0)
		return false;
	PSOCKET_INFORMATION SocketInfo = SocketArray[i];
	pServerStatics->RecvTotalBytes = SocketInfo->RecvTotalBytes;
	pServerStatics->SendTotalBytes = SocketInfo->SendTotalBytes;
	pServerStatics->ConnectedDT = SocketInfo->ConnectedDT;
	pServerStatics->FirstActiveTick = SocketInfo->FirstActiveTick;
	pServerStatics->LastActiveTick = SocketInfo->LastActiveTick;
	return true;
}


void TCPServer::RegCallback_OnRecvCompleted(void* pClass, PServerRecvSendEvent pCallBackFunc)
{
	m_OnRecvCompleted = pCallBackFunc;
	RegOnRecvCompletedClass = pClass;
}

void TCPServer::UnRegCallback_OnRecvCompleted()
{
	m_OnRecvCompleted = NULL;
	RegOnRecvCompletedClass = NULL;
}

void TCPServer::RegCallback_OnSendCompleted(void* pClass, PServerRecvSendEvent pCallBackFunc)
{
	m_OnSendCompleted = pCallBackFunc;
	RegOnSendCompletedClass = pClass;
}

void TCPServer::UnRegCallback_OnSendCompleted()
{
	m_OnSendCompleted = NULL;
	RegOnSendCompletedClass = NULL;
}


void TCPServer::RegCallback_OnConnected(void* pClass, PServerConnectStatusEvent pCallBackFunc)
{
	m_OnConnected = pCallBackFunc;
	RegOnConnectedClass = pClass;
}

void TCPServer::UnRegCallback_OnConnected()
{
	m_OnConnected = NULL;
	RegOnConnectedClass = NULL;
}

void TCPServer::RegCallback_OnDisconnected(void* pClass, PServerConnectStatusEvent pCallBackFunc)
{
	m_OnDisconnected = pCallBackFunc;
	RegOnDisconnectedClass = pClass;
}

void TCPServer::UnRegCallback_OnDisonnected()
{
	m_OnDisconnected = NULL;
	RegOnDisconnectedClass = NULL;
}

