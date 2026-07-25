#include "BaseLog.h"
#include "RK_Exception.h"
#include "TCPClient.h"
#include "TCPServer.h"
#include "cupscomm.h"
#include <unistd.h>
#include <fcntl.h>
#include "BaseLog.h"
#include "commroute.h"
#include <string.h>


TCupsCOMM::TCupsCOMM(PCOMMParm pCommParm)
{
	LogObj = NULL;
	m_CommParm = *pCommParm;
	m_Started = false;
	m_Terminated = false;

	pServer = new TCPServer();
	pClient = new TCPClient();


	int ret = pthread_create(&m_ThreadId, NULL, WorkerThread, (void*)this);
	if (ret != 0)
	{
		char Msg[MAX_MSG_SIZE];
		snprintf(Msg, MAX_MSG_SIZE, "Create WorkerThread failed with error! Code:%d, %s\n", errno, strerror(errno));
		throw CRK_Exception(Msg);
	}

	pClient->RegCallback_OnConnected(this, OnClientConnected);
	pClient->RegCallback_OnConnectFail(this, OnClientConnectFail);
	pClient->RegCallback_OnDisconnected(this, OnClientDisconnected);
	pClient->RegCallback_OnSendCompleted(this, OnClientSendCompleted);
	pServer->RegCallback_OnConnected(this, OnServerConnected);
	pServer->RegCallback_OnDisconnected(this, OnServerDisconnected);
	pServer->RegCallback_OnRecvCompleted(this, OnServerRecvCompleted);
}

TCupsCOMM::~TCupsCOMM()
{
	m_Terminated = true;

	//等待线程结束
	if (pthread_join(m_ThreadId, NULL) != 0) {
		LOG("WorkerThread join failed!");
	}
	else {
		LOG("WorkerThread was finished!");
	}
	delete pServer;
	delete pClient;
}


void TCupsCOMM::StartComm()
{
	m_Started = true;
	m_ServerConnected = false;
	m_ClientConnected = false;
	ServerConnectID = 0;
	pClient->SetServerPort(m_CommParm.RemotePort);
	pClient->SetServerIP(m_CommParm.RemoteIP);
	pClient->SetLocalIP(m_CommParm.ClientIP);
	pServer->SetMaxConnectNum(1);
	pServer->SetNetPort(m_CommParm.LocalPort);
	pServer->SetMaxTimeNoActive(300);
	pServer->SetLocalServerIP(m_CommParm.LocalIP);

	pClient->setLogObj(LogObj);
	pServer->setLogObj(LogObj);
	pServer->StartServer();
	//	LOG(TEXT("starting Client...\r\n"));
	//	Sleep(500);	
	pClient->Connect();
}

void TCupsCOMM::StopComm()
{
	pServer->StopServer();
	pClient->Disconnect();
	m_Started = false;
}

void* TCupsCOMM::WorkerThread(void* lpParameter)
{
	TCupsCOMM* inst = (TCupsCOMM*)lpParameter;

	//DWORD result = 0;
	while (!inst->m_Terminated)
	{
		if (!inst->m_Started)
		{
			usleep(500 * 1000);
			continue;
		}

		if (!inst->m_ClientConnected) {
			inst->pClient->Connect();
			usleep(500 * 1000);
			continue;
		}

		TCommObj* pSendCommObj = (TCommObj*)inst->m_CommParm.pSendQueue->Pop();
		if (pSendCommObj != NULL)
		{
			bool rslt = false;
			while (!rslt) {
				rslt = inst->pClient->PostSend(pSendCommObj->DataBuffer, pSendCommObj->DataLen);
				if (!rslt) {
					if (errno == EAGAIN) {
						LOG_S("PostSend failed would blocked !\n");
						sleep(1);
					}
					else {
						LOG_S("PostSend failed !\n");
						break;
					}
				}
			}
				
			delete pSendCommObj;
		}

		TCPLinkStatics ClientStatics;
		if (inst->pClient->GetStatistics(&ClientStatics) &&
			((GetTickCount64() - ClientStatics.LastActiveTick) / 1000 > inst->m_CommParm.RemoteIdleSec))
		{
			TCommObj* pSendCommObj = new TCommObj();
			pSendCommObj->DataLen = 4;
			memcpy(pSendCommObj->DataBuffer, "0000", 4);
			inst->m_CommParm.pSendQueue->Push(pSendCommObj);
			usleep(1000 * 1000);
		}

		TCPLinkStatics ServerStatics;
		if (inst->pServer->GetStatistics(inst->ServerConnectID, &ServerStatics) &&
			((GetTickCount64() - ServerStatics.LastActiveTick) / 1000 > inst->m_CommParm.LocalIdleSec))
		{
			LOG_SF("No link maintenance packet received in %d seconds, disconnected!\n", inst->m_CommParm.LocalIdleSec);
			inst->pServer->CloseConnect(inst->ServerConnectID);
			
		}
		usleep(10 * 1000);
	}
	return 0;
}

void TCupsCOMM::OnClientConnected(void* cls_inst)
{
	TCupsCOMM* inst = (TCupsCOMM*)cls_inst;
	inst->m_ClientConnected = true;
	LOG_S("Successfully connected to host\n");
}

void TCupsCOMM::OnClientDisconnected(void* cls_inst)
{
	TCupsCOMM* inst = (TCupsCOMM*)cls_inst;
	inst->m_ClientConnected = false;
	LOG_S("Host disconnected\n");

}
void TCupsCOMM::OnClientConnectFail(void* cls_inst)
{
	TCupsCOMM* inst = (TCupsCOMM*)cls_inst;
	inst->m_ClientConnected = false;
	LOG_S("Failed to connect to host\n");
}

void TCupsCOMM::OnClientSendCompleted(char* buf, long len, long ExpLen, void* cls_inst)
{
	TCupsCOMM* inst = (TCupsCOMM*)cls_inst;
	LOG_S("Sending message to host successfully!\n");
}
void TCupsCOMM::OnServerConnected(long ID, void* cls_inst)
{
	TCupsCOMM* inst = (TCupsCOMM*)cls_inst;
	inst->m_ServerConnected = true;
	inst->ServerConnectID = ID;
	inst->pServer->PostRecv(ID, NULL, 4);
	LOG_S("Successful connection with client\n");
}

void TCupsCOMM::OnServerDisconnected(long ID, void* cls_inst)
{
	TCupsCOMM* inst = (TCupsCOMM*)cls_inst;
	inst->m_ServerConnected = false;
	inst->ServerConnectID = 0;
	LOG_S("Disconnect from client\n");
}

void TCupsCOMM::OnServerRecvCompleted(long ID, char* buf, long len, long ExpLen, void* cls_inst)
{
	TCupsCOMM* inst = (TCupsCOMM*)cls_inst;
	if (ExpLen == 4)
	{
		if (ExpLen != len)
		{
			LOG_S("OnServerRecvCompleted ExpLen <> len !\n");
			inst->pServer->PostRecv(ID, NULL, 4);
			return;
		}

		string S(buf, 4);
		long PkgLen = atol(S.c_str());
		if (PkgLen == 0)
		{
			LOG_S("Link maintenance packet received\n");
			inst->pServer->PostRecv(ID, NULL, 4);
		}
		else
			inst->pServer->PostRecv(ID, NULL, (DWORD)PkgLen);

	}
	else
	{
		LOG_S("Message received\n");

		if (ExpLen != len)
		{
			LOG_SF("OnServerRecvCompleted ExpLen <> len 2! ExpLen=%d; Len=%d\n", ExpLen, len);
			return;
		}

		TCommObj* pCommObj = new TCommObj();
		pCommObj->DataLen = (WORD)len;
		memcpy(pCommObj->DataBuffer, buf, len);
		inst->m_CommParm.pRecvQueue->Push(pCommObj);
		LOG_SF("RecvQueue Count=%d\n", inst->m_CommParm.pRecvQueue->getCount());
		inst->pServer->PostRecv(ID, NULL, 4);
	}
}

bool TCupsCOMM::IsStarted()
{
	return m_Started;
}

void TCupsCOMM::SetLog(CBaseLog* LogObj)
{
	this->LogObj = LogObj;
}
