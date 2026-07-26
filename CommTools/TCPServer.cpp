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
#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <vector>

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
	m_AcceptStopping = false;
	m_TransferStopping = false;
	m_Stopping = false;
	m_TransferOwnsStopCleanup = false;
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
	m_WakeupFd = -1;
	ListenSocket = -1;
	m_AcceptThreadCreated = false;
	m_TransferThreadCreated = false;
	m_ActiveCallbacks = 0;
	m_AcceptReady = false;
	m_AcceptStartError = 0;
	m_PendingFreeCount = 0;
	memset(m_PendingFree, 0, sizeof(m_PendingFree));

	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&m_mutex, &attr);
	pthread_mutexattr_destroy(&attr);
	pthread_cond_init(&m_CloseCond, NULL);
	pthread_cond_init(&m_CallbackCond, NULL);
	pthread_cond_init(&m_AcceptReadyCond, NULL);
	m_MaxConnectNum = MAX_CONNECT_NUM;
}


TCPServer::~TCPServer(void)
{
	if (m_ServerStarted || m_AcceptThreadCreated || m_TransferThreadCreated)
		StopServer();
	DrainPendingFree();
	pthread_cond_destroy(&m_AcceptReadyCond);
	pthread_cond_destroy(&m_CallbackCond);
	pthread_cond_destroy(&m_CloseCond);
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
	if (m_Stopping)
	{
		char buf[MAX_MSG_SIZE];
		snprintf(buf, sizeof(buf), "Server is still stopping\n");
		throw CRK_Exception(buf);
	}
	if ((m_AcceptThreadCreated && pthread_equal(pthread_self(), m_AcceptThreadId)) ||
		(m_TransferThreadCreated && pthread_equal(pthread_self(), m_TransferThreadId)))
	{
		char buf[MAX_MSG_SIZE];
		snprintf(buf, sizeof(buf), "Cannot restart server from its callback thread\n");
		throw CRK_Exception(buf);
	}

	// 回收在服务器回调中异步停止后已经退出的线程。
	if (m_AcceptThreadCreated)
	{
		pthread_join(m_AcceptThreadId, NULL);
		m_AcceptThreadCreated = false;
	}
	if (m_TransferThreadCreated)
	{
		pthread_join(m_TransferThreadId, NULL);
		m_TransferThreadCreated = false;
	}

	pthread_mutex_lock(&m_mutex);
	m_CommandQueue.clear();
	m_ClosingConnections.clear();
	m_AcceptReady = false;
	m_AcceptStartError = 0;
	m_ConnTotal = 0;
	memset(SocketArray, 0, sizeof(SocketArray));
	pthread_mutex_unlock(&m_mutex);

	m_ConnTotal = 0;
	m_LastTick = GetTickCount64();
	m_AcceptStopping = false;
	m_TransferStopping = false;
	m_Stopping = false;
	m_TransferOwnsStopCleanup = false;
	m_TotalRecvBytes = 0;
	m_TotalSentBytes = 0;

	//生成用于处理数据传输描述符
	m_epfd_tf = epoll_create1(EPOLL_CLOEXEC);
	if (m_epfd_tf < 0)
	{
		char buf[MAX_MSG_SIZE];
		snprintf(buf, sizeof(buf), "epoll_create1() failed: %s\n", strerror(errno));
		throw CRK_Exception(buf);
	}

	m_WakeupFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (m_WakeupFd < 0)
	{
		char buf[MAX_MSG_SIZE];
		snprintf(buf, sizeof(buf), "eventfd() failed: %s\n", strerror(errno));
		CleanupStartFailure();
		throw CRK_Exception(buf);
	}

	struct epoll_event wakeEvent;
	memset(&wakeEvent, 0, sizeof(wakeEvent));
	wakeEvent.data.ptr = this;
	wakeEvent.events = EPOLLIN;
	if (epoll_ctl(m_epfd_tf, EPOLL_CTL_ADD, m_WakeupFd, &wakeEvent) < 0)
	{
		char buf[MAX_MSG_SIZE];
		snprintf(buf, sizeof(buf), "epoll_ctl(eventfd) failed: %s\n", strerror(errno));
		CleanupStartFailure();
		throw CRK_Exception(buf);
	}

	//建立监听Socket
	ListenSocket = socket(m_af, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (ListenSocket < 0)
	{
		LOG_F("Server Socket error! Error Code: %d at %s %d\n", errno,  __FILE__, __LINE__);
		char buf[MAX_MSG_SIZE];
		snprintf(buf, MAX_MSG_SIZE, "Failed to get a socket Code: %d, %s\n",  errno, strerror(errno));
		CleanupStartFailure();
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
		LOG_F("getaddrinfo() failed: %s at %s %d\n", gai_strerror(rc), __FILE__, __LINE__);
		snprintf(Msg, MAX_MSG_SIZE, "getaddrinfo() failed: %s at %s %d\n", gai_strerror(rc), __FILE__, __LINE__);
		CleanupStartFailure();
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
				LOG_F("bind() failed with error! Code:%d  at %s %d\n", errno, __FILE__, __LINE__);
				snprintf(Msg, MAX_MSG_SIZE, "bind() failed with error! Code:%d, %s\n", errno, strerror(errno));
				CleanupStartFailure();
				throw CRK_Exception(Msg);
			}
		}
		else
			break;
		iRetry++;
	} while (1);

	freeaddrinfo(pAddInfo);

	if (listen(ListenSocket, SOMAXCONN) < 0) {
		LOG_F("listen() failed with error! Code:%d  at %s %d\n", errno, __FILE__, __LINE__);
		snprintf(Msg, MAX_MSG_SIZE, "listen() failed with error! Code:%d, %s\n", errno, strerror(errno));
		CleanupStartFailure();
		throw CRK_Exception(Msg);
	}
	if (!SetNonblocking(ListenSocket))
	{
		snprintf(Msg, MAX_MSG_SIZE, "Failed to set listener nonblocking: %s\n", strerror(errno));
		CleanupStartFailure();
		throw CRK_Exception(Msg);
	}

	//先创建传输线程，确保 Accept 回调可以立即投递收发任务
	int ret = pthread_create(&m_TransferThreadId, NULL, TransferThread, (void*)this);
	if (ret != 0)
	{
		LOG_F("Create TransferThread failed! Code:%d at %s %d\n", ret, __FILE__, __LINE__);
		snprintf(Msg, MAX_MSG_SIZE, "Create TransferThread failed! Code:%d, %s\n", ret, strerror(ret));
		CleanupStartFailure();
		throw CRK_Exception(Msg);
	}
	m_TransferThreadCreated = true;
	m_ServerStarted = true;

	//创建Accept线程用来处理接受连接接入
	ret = pthread_create(&m_AcceptThreadId, NULL, AcceptThread, (void*)this);
	if (ret != 0)
	{
		LOG_F("Create AcceptThread failed! Code:%d at %s %d\n", ret, __FILE__, __LINE__);
		snprintf(Msg, MAX_MSG_SIZE, "Create AcceptThread failed! Code:%d, %s\n", ret, strerror(ret));
		CleanupStartFailure();
		throw CRK_Exception(Msg);
	}
	m_AcceptThreadCreated = true;

	pthread_mutex_lock(&m_mutex);
	while (!m_AcceptReady)
		pthread_cond_wait(&m_AcceptReadyCond, &m_mutex);
	int acceptStartError = m_AcceptStartError;
	pthread_mutex_unlock(&m_mutex);
	if (acceptStartError != 0)
	{
		snprintf(Msg, MAX_MSG_SIZE, "AcceptThread initialization failed: %s\n",
			strerror(acceptStartError));
		CleanupStartFailure();
		throw CRK_Exception(Msg);
	}
}

//数据收发处理线程
void* TCPServer::TransferThread(void* lpParameter)
{
	TCPServer* inst = (TCPServer*)lpParameter;
	struct epoll_event Events[MAX_EPWEVENT_NUM];

	while (!inst->m_TransferStopping)
	{
		inst->DrainPendingFree();
		int nfds = epoll_wait(inst->m_epfd_tf, Events, MAX_EPWEVENT_NUM, WAIT_TIME_OUT);
		if (nfds < 0)
		{
			if (errno == EINTR)
				continue;
			if (inst->LogObj != NULL)
				inst->LogObj->WriteLogWithFormat("epoll_wait transfer failed: %s\n", strerror(errno));
			inst->m_Stopping = true;
			inst->m_ServerStarted = false;
			inst->m_AcceptStopping = true;
			inst->m_TransferOwnsStopCleanup = true;
			if (inst->ListenSocket >= 0)
				shutdown(inst->ListenSocket, SHUT_RDWR);
			inst->m_TransferStopping = true;
			break;
		}

		// eventfd 命令优先于本批 socket 事件，避免处理已请求关闭的连接。
		for (int i = 0; i < nfds; ++i)
		{
			if (Events[i].data.ptr == inst)
			{
				uint64_t value;
				while (read(inst->m_WakeupFd, &value, sizeof(value)) < 0 && errno == EINTR) {}
				inst->ProcessCommands();
			}
		}

		for (int i = 0; i < nfds; ++i)
		{
			if (Events[i].data.ptr == inst)
				continue;

			PSOCKET_INFORMATION SI = (PSOCKET_INFORMATION)Events[i].data.ptr;
			if (SI == NULL || SI->Socket < 0)
				continue;

			long connectID = SI->ID;
			if (Events[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))
			{
				inst->CloseConnectInternal(connectID);
				continue;
			}

			if (Events[i].events & EPOLLIN)
			{
				pthread_mutex_lock(&inst->m_mutex);
				bool canRead = SI->Socket >= 0 && SI->bReading &&
					inst->m_ClosingConnections.count(connectID) == 0;
				int socketFd = SI->Socket;
				char* pBaseAddr = SI->RecvDataBuf.buf;
				long expectedRecv = SI->BytesRECV;
				pthread_mutex_unlock(&inst->m_mutex);

				if (canRead)
				{
					long remaining = expectedRecv;
					long recvNumTotal = 0;
					long offset = 0;
					bool readCompleted = false;
					bool closed = false;

					while (remaining > 0)
					{
						long recvNum = recv(socketFd, pBaseAddr + offset, remaining, 0);
						if (recvNum < 0)
						{
							if (errno == EAGAIN || errno == EWOULDBLOCK)
							{
								readCompleted = true;
								break;
							}
							if (errno == EINTR)
								continue;
							inst->CloseConnectInternal(connectID);
							closed = true;
							break;
						}
						if (recvNum == 0)
						{
							inst->CloseConnectInternal(connectID);
							closed = true;
							break;
						}
						recvNumTotal += recvNum;
						offset += recvNum;
						remaining -= recvNum;
						if (remaining == 0)
							readCompleted = true;
					}

					if (!closed)
					{
						PServerRecvSendEvent callback = NULL;
						void* callbackClass = NULL;
						char* callbackBuf = NULL;
						long callbackLen = 0;
						long callbackExpected = 0;
						bool invokeCallback = false;

						pthread_mutex_lock(&inst->m_mutex);
						if (SI->Socket >= 0)
						{
							SI->RecvDataBuf.len = (DWORD)recvNumTotal;
							SI->RecvTotalBytes += recvNumTotal;
							if (recvNumTotal > 0)
								SI->LastActiveTick = GetTickCount64();
							SI->bReading = false;
							__sync_add_and_fetch(&inst->m_TotalRecvBytes, recvNumTotal);
							if (readCompleted && inst->m_ClosingConnections.count(connectID) == 0)
							{
								callback = inst->m_OnRecvCompleted;
								callbackClass = inst->RegOnRecvCompletedClass;
								callbackBuf = SI->RecvDataBuf.buf;
								callbackLen = SI->RecvDataBuf.len;
								callbackExpected = SI->BytesRECV;
								invokeCallback = callback != NULL;
								if (invokeCallback)
									inst->m_ActiveCallbacks++;
							}
						}
						pthread_mutex_unlock(&inst->m_mutex);

						inst->UpdateEpoll(connectID);
						if (invokeCallback) {
							callback(connectID, callbackBuf, callbackLen, callbackExpected, callbackClass);
							inst->FinishCallback();
						}
					}
				}
			}

			if (SI->Socket < 0)
				continue;

			if (Events[i].events & EPOLLOUT)
			{
				pthread_mutex_lock(&inst->m_mutex);
				bool canSend = SI->Socket >= 0 && SI->bSending &&
					inst->m_ClosingConnections.count(connectID) == 0;
				int socketFd = SI->Socket;
				char* pBaseAddr = SI->SendDataBuf.buf;
				long offset = (long)SI->BytesSentSoFar;
				long remaining = (long)SI->BytesSEND - offset;
				pthread_mutex_unlock(&inst->m_mutex);

				if (!canSend)
					continue;

				long sentThisRound = 0;
				bool sendCompleted = remaining <= 0;
				bool closed = false;
				while (remaining > 0)
				{
					long sendNum = send(socketFd, pBaseAddr + offset, remaining, MSG_NOSIGNAL);
					if (sendNum < 0)
					{
						if (errno == EAGAIN || errno == EWOULDBLOCK)
							break;
						if (errno == EINTR)
							continue;
						inst->CloseConnectInternal(connectID);
						closed = true;
						break;
					}
					sentThisRound += sendNum;
					offset += sendNum;
					remaining -= sendNum;
					if (remaining == 0)
						sendCompleted = true;
				}

				if (!closed)
				{
					PServerRecvSendEvent callback = NULL;
					void* callbackClass = NULL;
					char* callbackBuf = NULL;
					long callbackLen = 0;
					long callbackExpected = 0;
					bool invokeCallback = false;

					pthread_mutex_lock(&inst->m_mutex);
					if (SI->Socket >= 0)
					{
						SI->BytesSentSoFar = (DWORD)offset;
						SI->SendDataBuf.len = SI->BytesSentSoFar;
						SI->SendTotalBytes += sentThisRound;
						if (sentThisRound > 0)
							SI->LastActiveTick = GetTickCount64();
						__sync_add_and_fetch(&inst->m_TotalSentBytes, sentThisRound);
						if (sendCompleted)
						{
							SI->bSending = false;
							if (inst->m_ClosingConnections.count(connectID) == 0)
							{
								callback = inst->m_OnSendCompleted;
								callbackClass = inst->RegOnSendCompletedClass;
								callbackBuf = SI->SendDataBuf.buf;
								callbackLen = SI->SendDataBuf.len;
								callbackExpected = SI->BytesSEND;
								invokeCallback = callback != NULL;
								if (invokeCallback)
									inst->m_ActiveCallbacks++;
							}
						}
					}
					pthread_mutex_unlock(&inst->m_mutex);

					inst->UpdateEpoll(connectID);
					if (invokeCallback) {
						callback(connectID, callbackBuf, callbackLen, callbackExpected, callbackClass);
						inst->FinishCallback();
					}
				}
			}
		}

		inst->ProcessCommands();
		inst->CheckIdleConnections();
	}

	inst->CloseAllConnections();
	inst->DrainPendingFree();
	if (inst->m_Stopping)
	{
		if (inst->ListenSocket >= 0)
		{
			close(inst->ListenSocket);
			inst->ListenSocket = -1;
		}
		if (inst->m_WakeupFd >= 0)
		{
			close(inst->m_WakeupFd);
			inst->m_WakeupFd = -1;
		}
		if (inst->m_epfd_tf >= 0)
		{
			close(inst->m_epfd_tf);
			inst->m_epfd_tf = -1;
		}
		pthread_mutex_lock(&inst->m_mutex);
		inst->m_CommandQueue.clear();
		inst->m_ClosingConnections.clear();
		pthread_cond_broadcast(&inst->m_CloseCond);
		pthread_mutex_unlock(&inst->m_mutex);
		inst->m_ServerStarted = false;
		inst->m_TransferStopping = false;
		if (inst->m_TransferOwnsStopCleanup)
		{
			inst->m_TransferOwnsStopCleanup = false;
			inst->m_Stopping = false;
		}
	}
	return NULL;
}

void* TCPServer::AcceptThread(void* lpParameter)
{
	TCPServer* inst = (TCPServer*)lpParameter;
	struct epoll_event Events[MAX_EPWEVENT_NUM]; //声明epoll_event结构体的变量数组用于回传要处理的事件
	struct epoll_event ev;  //ev用于注册事件

	//生成用于处理accept的epoll专用的文件描述符
	int epfd = epoll_create1(EPOLL_CLOEXEC);
	if (epfd < 0)
	{
		int savedErrno = errno;
		LOG_SF("Accept epoll_create1 failed: %s\n", strerror(errno));
		pthread_mutex_lock(&inst->m_mutex);
		inst->m_AcceptStartError = savedErrno;
		inst->m_AcceptReady = true;
		pthread_cond_broadcast(&inst->m_AcceptReadyCond);
		pthread_mutex_unlock(&inst->m_mutex);
		return NULL;
	}

	//设置与要处理的事件相关的文件描述符
	memset(&ev, 0, sizeof(ev));
	ev.data.fd = inst->ListenSocket;
	//水平触发：可读时会持续通知，配合下面 accept 排空
	ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;

	//注册epoll事件
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, inst->ListenSocket, &ev) < 0)
	{
		int savedErrno = errno;
		LOG_SF("Accept epoll_ctl failed: %s\n", strerror(errno));
		close(epfd);
		pthread_mutex_lock(&inst->m_mutex);
		inst->m_AcceptStartError = savedErrno;
		inst->m_AcceptReady = true;
		pthread_cond_broadcast(&inst->m_AcceptReadyCond);
		pthread_mutex_unlock(&inst->m_mutex);
		return NULL;
	}

	pthread_mutex_lock(&inst->m_mutex);
	inst->m_AcceptReady = true;
	pthread_cond_broadcast(&inst->m_AcceptReadyCond);
	pthread_mutex_unlock(&inst->m_mutex);

	while (!inst->m_AcceptStopping)
	{
		int nfds = epoll_wait(epfd, Events, MAX_EPWEVENT_NUM, WAIT_TIME_OUT);
		if (nfds < 0)
		{
			if (errno == EINTR)
				continue;
			LOG_SF("Accept epoll_wait failed: %s\n", strerror(errno));
			break;
		}
		for (int i = 0; i < nfds; ++i)
		{
			if (inst->m_AcceptStopping)
				break;
			//如果新监测到一个SOCKET用户连接到了绑定的SOCKET端口，建立新的连接。
			if (Events[i].data.fd == inst->ListenSocket)
			{
				while (!inst->m_AcceptStopping)
				{
					struct sockaddr_storage clientaddr;
					socklen_t clilen = sizeof(clientaddr);
					SOCKET connfd = accept(inst->ListenSocket, (sockaddr*)&clientaddr, &clilen);
					if (connfd < 0) {
						if (errno == EAGAIN || errno == EWOULDBLOCK)
							break;
						if (errno == EINTR)
							continue;
						if (inst->m_AcceptStopping)
							break;
						LOG_SF("Accept socket error! %s, at %s %d\n", strerror(errno), __FILE__, __LINE__);
						break;
					}
					char ipstr[INET6_ADDRSTRLEN] = { 0 };
					if (!FormatSockAddr((sockaddr*)&clientaddr, clilen, ipstr, sizeof(ipstr)))
						strncpy(ipstr, "?", sizeof(ipstr) - 1);
					LOG_SF("new client connected!,ip:%s\n", ipstr);

					pthread_mutex_lock(&inst->m_mutex);
					if (inst->m_Stopping || inst->m_ConnTotal >= inst->m_MaxConnectNum) {
						DWORD current = inst->m_ConnTotal;
						pthread_mutex_unlock(&inst->m_mutex);
						LOG_SF("too many connection or server stopping, current connects:%d, MaxConnect:%d\n",
							(int)current, (int)inst->m_MaxConnectNum);
						close(connfd);
						continue;
					}

					//保存Socket相关信息
					inst->m_ConnTotal++;
					inst->SocketArray[inst->m_ConnTotal] =
						PSOCKET_INFORMATION(calloc(1, sizeof(SOCKET_INFORMATION)));
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
					time_t connectedTime = time(NULL);
					localtime_r(&connectedTime, &SI->ConnectedDT);

					if (!inst->SetNonblocking(connfd))
					{
						inst->SocketArray[inst->m_ConnTotal] = NULL;
						inst->m_ConnTotal--;
						pthread_mutex_unlock(&inst->m_mutex);
						free(SI);
						close(connfd);
						continue;
					}

					PServerConnectStatusEvent callback = inst->m_OnConnected;
					void* callbackClass = inst->RegOnConnectedClass;
					if (callback != NULL)
						inst->m_ActiveCallbacks++;
					long connectionID = SI->ID;
					pthread_mutex_unlock(&inst->m_mutex);

					if (callback != NULL) {
						callback(connectionID, callbackClass);
						inst->FinishCallback();
					}

					pthread_mutex_lock(&inst->m_mutex);
					bool connectionAlive =
						inst->FindSocketInformationIDXbyIDLocked(connectionID) >= 0 &&
						inst->m_ClosingConnections.count(connectionID) == 0;
					if (connectionAlive)
						inst->EnqueueCommandLocked(SERVER_CMD_UPDATE_EPOLL, connectionID);
					pthread_mutex_unlock(&inst->m_mutex);
					if (connectionAlive)
						inst->WakeTransferThread();
				}
			}
		}
	}
	close(epfd);  //关闭epoll描述符
	return NULL;
}

long TCPServer::FindSocketInformationIDXbyIDLocked(long ConnectID)
{
	if (ConnectID <= 0)
		return -1;
	for (DWORD i = 1; i <= m_ConnTotal; ++i)
	{
		if (SocketArray[i] != NULL && SocketArray[i]->ID == ConnectID)
			return (long)i;
	}
	return -1;
}

void TCPServer::EnqueueCommandLocked(SERVER_COMMAND_TYPE type, long ConnectID)
{
	m_CommandQueue.push_back(SERVER_COMMAND(type, ConnectID));
}

void TCPServer::WakeTransferThread()
{
	if (m_WakeupFd < 0)
		return;
	uint64_t value = 1;
	while (write(m_WakeupFd, &value, sizeof(value)) < 0)
	{
		if (errno == EINTR)
			continue;
		// EAGAIN 表示 eventfd 已可读，无需重复写入。
		if (errno != EAGAIN)
			LOG_F("eventfd write failed: %s\n", strerror(errno));
		break;
	}
}

void TCPServer::FinishCallback()
{
	pthread_mutex_lock(&m_mutex);
	if (m_ActiveCallbacks > 0)
		m_ActiveCallbacks--;
	pthread_cond_broadcast(&m_CallbackCond);
	pthread_mutex_unlock(&m_mutex);
}

void TCPServer::WaitForCallbacksLocked()
{
	unsigned int callbacksOnThisThread = 0;
	if ((m_AcceptThreadCreated && pthread_equal(pthread_self(), m_AcceptThreadId)) ||
		(m_TransferThreadCreated && pthread_equal(pthread_self(), m_TransferThreadId)))
		callbacksOnThisThread = 1;
	while (m_ActiveCallbacks > callbacksOnThisThread)
		pthread_cond_wait(&m_CallbackCond, &m_mutex);
}

void TCPServer::ProcessCommands()
{
	std::deque<SERVER_COMMAND> commands;
	pthread_mutex_lock(&m_mutex);
	commands.swap(m_CommandQueue);
	pthread_mutex_unlock(&m_mutex);

	for (std::deque<SERVER_COMMAND>::iterator it = commands.begin();
		it != commands.end(); ++it)
	{
		if (it->Type == SERVER_CMD_UPDATE_EPOLL)
		{
			UpdateEpoll(it->ConnectID);
		}
		else if (it->Type == SERVER_CMD_CLOSE)
		{
			CloseConnectInternal(it->ConnectID);
		}
		else if (it->Type == SERVER_CMD_STOP)
		{
			CloseAllConnections();
			m_TransferStopping = true;
			break;
		}
	}
}

bool TCPServer::UpdateEpoll(long ConnectID)
{
	pthread_mutex_lock(&m_mutex);
	long idx = FindSocketInformationIDXbyIDLocked(ConnectID);
	if (idx < 0 || m_ClosingConnections.count(ConnectID) != 0)
	{
		pthread_mutex_unlock(&m_mutex);
		return false;
	}

	PSOCKET_INFORMATION SI = SocketArray[idx];
	if (SI == NULL || SI->Socket < 0 || m_epfd_tf < 0)
	{
		pthread_mutex_unlock(&m_mutex);
		return false;
	}

	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.data.ptr = SI;
	ev.events = EPOLLHUP | EPOLLRDHUP | EPOLLERR;
	if (SI->bReading)
		ev.events |= EPOLLIN;
	if (SI->bSending)
		ev.events |= EPOLLOUT;

	int op = SI->bEvent ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
	int rc = epoll_ctl(m_epfd_tf, op, SI->Socket, &ev);
	if (rc < 0 && op == EPOLL_CTL_ADD && errno == EEXIST)
		rc = epoll_ctl(m_epfd_tf, EPOLL_CTL_MOD, SI->Socket, &ev);
	else if (rc < 0 && op == EPOLL_CTL_MOD && errno == ENOENT)
		rc = epoll_ctl(m_epfd_tf, EPOLL_CTL_ADD, SI->Socket, &ev);

	if (rc == 0)
		SI->bEvent = true;
	int savedErrno = errno;
	pthread_mutex_unlock(&m_mutex);

	if (rc < 0)
	{
		LOG_F("epoll_ctl connection %ld failed: %s\n", ConnectID, strerror(savedErrno));
		CloseConnectInternal(ConnectID);
		return false;
	}
	return true;
}

//仅供 TransferThread 内部在收发状态改变后同步更新兴趣事件。
void TCPServer::ReTriggerEvent(PSOCKET_INFORMATION SI)
{
	if (SI != NULL)
		UpdateEpoll(SI->ID);
}

bool TCPServer::PostRecv(long ConnectID, char* buf, DWORD len)
{
	if (len <= 0) {
		errno = EINVAL;
		return false;
	}

	if (!m_ServerStarted || m_Stopping)
	{
		errno = ENOTSOCK;
		return false;
	}

	pthread_mutex_lock(&m_mutex);
	long idx = FindSocketInformationIDXbyIDLocked(ConnectID);
	if (idx < 0) {
		errno = ENODATA;
		pthread_mutex_unlock(&m_mutex);
		return false;
	}

	PSOCKET_INFORMATION SI;
	SI = SocketArray[idx];
	//当socket信息无效
	if (SI == NULL || m_ClosingConnections.count(ConnectID) != 0) {
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

	EnqueueCommandLocked(SERVER_CMD_UPDATE_EPOLL, ConnectID);
	pthread_mutex_unlock(&m_mutex);
	WakeTransferThread();
	return true;
}


bool TCPServer::PostSend(long ConnectID, char* buf, DWORD len)
{
	if (!m_ServerStarted || m_Stopping)
	{
		errno = ENOTSOCK;
		return false;
	}

	if (buf == NULL || len == 0) {
		errno = EINVAL;
		return false;
	}

	//发送的数据大于内置缓冲大小
	if (len > DATA_BUFSIZE) {
		errno = EINVAL;
		return false;
	}


	pthread_mutex_lock(&m_mutex);
	long idx = FindSocketInformationIDXbyIDLocked(ConnectID);
	if (idx < 0)
	{
		pthread_mutex_unlock(&m_mutex);
		errno = ENODATA;
		return false;
	}

	PSOCKET_INFORMATION SI;
	SI = SocketArray[idx];

	//socket信息无效
	if (SI == NULL || m_ClosingConnections.count(ConnectID) != 0) {
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

	EnqueueCommandLocked(SERVER_CMD_UPDATE_EPOLL, ConnectID);
	pthread_mutex_unlock(&m_mutex);
	WakeTransferThread();
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
	if (m_TransferThreadCreated && pthread_equal(pthread_self(), m_TransferThreadId))
	{
		CloseConnectInternal(ConnectID);
		return;
	}

	pthread_mutex_lock(&m_mutex);
	long index = FindSocketInformationIDXbyIDLocked(ConnectID);
	if (index < 0) {
		pthread_mutex_unlock(&m_mutex);
		return;
	}

	if (m_ClosingConnections.insert(ConnectID).second)
		EnqueueCommandLocked(SERVER_CMD_CLOSE, ConnectID);
	pthread_mutex_unlock(&m_mutex);
	WakeTransferThread();

	pthread_mutex_lock(&m_mutex);
	while (FindSocketInformationIDXbyIDLocked(ConnectID) >= 0 && m_TransferThreadCreated)
		pthread_cond_wait(&m_CloseCond, &m_mutex);
	pthread_mutex_unlock(&m_mutex);
}

void TCPServer::CloseConnectInternal(long ConnectID)
{
	PServerConnectStatusEvent callback = NULL;
	void* callbackClass = NULL;
	long closedID = 0;

	pthread_mutex_lock(&m_mutex);
	long index = FindSocketInformationIDXbyIDLocked(ConnectID);
	if (index < 0)
	{
		pthread_cond_broadcast(&m_CloseCond);
		pthread_mutex_unlock(&m_mutex);
		return;
	}

	PSOCKET_INFORMATION SI = SocketArray[index];
	closedID = SI->ID;
	callback = m_OnDisconnected;
	callbackClass = RegOnDisconnectedClass;
	if (callback != NULL)
		m_ActiveCallbacks++;

	if (SI->bEvent && SI->Socket >= 0 && m_epfd_tf >= 0)
		epoll_ctl(m_epfd_tf, EPOLL_CTL_DEL, SI->Socket, NULL);
	SI->bEvent = false;
	if (SI->Socket >= 0)
	{
		shutdown(SI->Socket, SHUT_RDWR);
		close(SI->Socket);
		SI->Socket = -1;
	}

	long i = index;
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

	m_ClosingConnections.erase(ConnectID);
	pthread_cond_broadcast(&m_CloseCond);
	pthread_mutex_unlock(&m_mutex);

	LOG_F("Client: %ld Closed\n", closedID);
	if (callback != NULL) {
		callback(closedID, callbackClass);
		FinishCallback();
	}
}

void TCPServer::CloseAllConnections()
{
	while (1)
	{
		pthread_mutex_lock(&m_mutex);
		long id = m_ConnTotal > 0 && SocketArray[1] != NULL ? SocketArray[1]->ID : -1;
		pthread_mutex_unlock(&m_mutex);
		if (id < 0)
			break;
		CloseConnectInternal(id);
	}
}

long TCPServer::GetSocketInformationIDXbyID(long ConnectID)
{
	pthread_mutex_lock(&m_mutex);
	long result = FindSocketInformationIDXbyIDLocked(ConnectID);
	pthread_mutex_unlock(&m_mutex);
	return result;
}

void TCPServer::StopServer()
{
	if (!m_ServerStarted && !m_AcceptThreadCreated && !m_TransferThreadCreated &&
		ListenSocket < 0 && m_epfd_tf < 0 && m_WakeupFd < 0)
		return;

	bool calledFromAcceptThread =
		m_AcceptThreadCreated && pthread_equal(pthread_self(), m_AcceptThreadId);
	m_Stopping = true;
	m_ServerStarted = false;
	m_AcceptStopping = true;

	if (ListenSocket >= 0)
		shutdown(ListenSocket, SHUT_RDWR);

	if (m_AcceptThreadCreated)
	{
		if (!pthread_equal(pthread_self(), m_AcceptThreadId))
		{
			if (pthread_join(m_AcceptThreadId, NULL) != 0) {
				LOG_F("AcceptThread join failed!\n");
			}
			else {
				LOG_F("AcceptThread was finished!\n");
			}
			m_AcceptThreadCreated = false;
		}
	}

	if (m_TransferThreadCreated)
	{
		if (pthread_equal(pthread_self(), m_TransferThreadId))
		{
			// 回调中停止时不能 join 自身；线程退出后可再次调用 StopServer 完成 fd 清理。
			m_TransferOwnsStopCleanup = true;
			CloseAllConnections();
			m_TransferStopping = true;
			return;
		}

		pthread_mutex_lock(&m_mutex);
		EnqueueCommandLocked(SERVER_CMD_STOP, 0);
		pthread_mutex_unlock(&m_mutex);
		WakeTransferThread();
		if (pthread_join(m_TransferThreadId, NULL) != 0) {
			LOG_F("TransferThread join failed!\n");
		}
		else {
			LOG_F("TransferThread was finished!\n");
		}
		m_TransferThreadCreated = false;
	}
	else
	{
		CloseAllConnections();
	}

	if (ListenSocket >= 0)
	{
		close(ListenSocket);
		ListenSocket = -1;
		LOG_F("listener socket is closed!\n");
	}
	if (m_WakeupFd >= 0)
	{
		close(m_WakeupFd);
		m_WakeupFd = -1;
	}
	if (m_epfd_tf >= 0)
	{
		close(m_epfd_tf);
		m_epfd_tf = -1;
	}

	DrainPendingFree();
	pthread_mutex_lock(&m_mutex);
	m_CommandQueue.clear();
	m_ClosingConnections.clear();
	pthread_cond_broadcast(&m_CloseCond);
	pthread_mutex_unlock(&m_mutex);
	m_TransferStopping = false;
	if (!calledFromAcceptThread)
		m_AcceptStopping = false;
	m_Stopping = false;
}

void TCPServer::CleanupStartFailure()
{
	StopServer();
}

void TCPServer::CheckIdleConnections()
{
	std::vector<long> expired;
	_U64 now = GetTickCount64();

	pthread_mutex_lock(&m_mutex);
	if (m_MaxTimeNoActive > 0 &&
		(now - m_LastTick >= 1000 || now < m_LastTick))
	{
		m_LastTick = now;
		for (DWORD i = 1; i <= m_ConnTotal; ++i)
		{
			PSOCKET_INFORMATION SI = SocketArray[i];
			if (SI != NULL && m_ClosingConnections.count(SI->ID) == 0 &&
				now >= SI->LastActiveTick &&
				now - SI->LastActiveTick >= m_MaxTimeNoActive * 1000)
			{
				m_ClosingConnections.insert(SI->ID);
				expired.push_back(SI->ID);
			}
		}
	}
	pthread_mutex_unlock(&m_mutex);

	for (std::vector<long>::iterator it = expired.begin(); it != expired.end(); ++it)
		CloseConnectInternal(*it);
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
	pthread_mutex_lock(&m_mutex);
	m_MaxTimeNoActive = secs;
	pthread_mutex_unlock(&m_mutex);
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
	if (!m_ServerStarted || pServerStatics == NULL)
		return false;

	pthread_mutex_lock(&m_mutex);
	long i = FindSocketInformationIDXbyIDLocked(ConnectID);
	if (i < 0)
	{
		pthread_mutex_unlock(&m_mutex);
		return false;
	}
	PSOCKET_INFORMATION SocketInfo = SocketArray[i];
	pServerStatics->RecvTotalBytes = SocketInfo->RecvTotalBytes;
	pServerStatics->SendTotalBytes = SocketInfo->SendTotalBytes;
	pServerStatics->ConnectedDT = SocketInfo->ConnectedDT;
	pServerStatics->FirstActiveTick = SocketInfo->FirstActiveTick;
	pServerStatics->LastActiveTick = SocketInfo->LastActiveTick;
	pthread_mutex_unlock(&m_mutex);
	return true;
}


void TCPServer::RegCallback_OnRecvCompleted(void* pClass, PServerRecvSendEvent pCallBackFunc)
{
	pthread_mutex_lock(&m_mutex);
	WaitForCallbacksLocked();
	m_OnRecvCompleted = pCallBackFunc;
	RegOnRecvCompletedClass = pClass;
	pthread_mutex_unlock(&m_mutex);
}

void TCPServer::UnRegCallback_OnRecvCompleted()
{
	pthread_mutex_lock(&m_mutex);
	m_OnRecvCompleted = NULL;
	RegOnRecvCompletedClass = NULL;
	WaitForCallbacksLocked();
	pthread_mutex_unlock(&m_mutex);
}

void TCPServer::RegCallback_OnSendCompleted(void* pClass, PServerRecvSendEvent pCallBackFunc)
{
	pthread_mutex_lock(&m_mutex);
	WaitForCallbacksLocked();
	m_OnSendCompleted = pCallBackFunc;
	RegOnSendCompletedClass = pClass;
	pthread_mutex_unlock(&m_mutex);
}

void TCPServer::UnRegCallback_OnSendCompleted()
{
	pthread_mutex_lock(&m_mutex);
	m_OnSendCompleted = NULL;
	RegOnSendCompletedClass = NULL;
	WaitForCallbacksLocked();
	pthread_mutex_unlock(&m_mutex);
}


void TCPServer::RegCallback_OnConnected(void* pClass, PServerConnectStatusEvent pCallBackFunc)
{
	pthread_mutex_lock(&m_mutex);
	WaitForCallbacksLocked();
	m_OnConnected = pCallBackFunc;
	RegOnConnectedClass = pClass;
	pthread_mutex_unlock(&m_mutex);
}

void TCPServer::UnRegCallback_OnConnected()
{
	pthread_mutex_lock(&m_mutex);
	m_OnConnected = NULL;
	RegOnConnectedClass = NULL;
	WaitForCallbacksLocked();
	pthread_mutex_unlock(&m_mutex);
}

void TCPServer::RegCallback_OnDisconnected(void* pClass, PServerConnectStatusEvent pCallBackFunc)
{
	pthread_mutex_lock(&m_mutex);
	WaitForCallbacksLocked();
	m_OnDisconnected = pCallBackFunc;
	RegOnDisconnectedClass = pClass;
	pthread_mutex_unlock(&m_mutex);
}

void TCPServer::UnRegCallback_OnDisonnected()
{
	pthread_mutex_lock(&m_mutex);
	m_OnDisconnected = NULL;
	RegOnDisconnectedClass = NULL;
	WaitForCallbacksLocked();
	pthread_mutex_unlock(&m_mutex);
}

