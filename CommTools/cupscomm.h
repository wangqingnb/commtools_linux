#pragma once
#include "PtrQueue.h"
#include "SockPubDefine.h"
#include "TCPClient.h"
#include "TCPServer.h"
#include <pthread.h>


//报文数据对象
class TCommObj
{
public:
    WORD DataLen;
    char DataBuffer[MAX_PKG_LEN];
};

typedef struct _TCOMMParm {
    WORD RemotePort;
    char RemoteIP[64];  //远程服务地址
    WORD LocalPort;  //本机服务端口
    char LocalIP[64];  //本机服务地址（本机有多个地址时可指定某个地址作为本地服务地址）
    char ClientIP[64];  //本机客户端IP（本机有多个地址时可指定某个地址作为客户端访问主机的地址）
    WORD RemoteIdleSec;
    WORD LocalIdleSec;
    PtrQueue* pSendQueue;
    PtrQueue* pRecvQueue;
    DWORD QueryE3toSucc;  //1-应答码E3转00， 0-不转
} TCOMMParm, * PCOMMParm;


class TCupsCOMM
{
private:
    CBaseLog* LogObj;  //日志接口对象
    bool m_Started;
    pthread_t  m_ThreadId;
    bool m_Terminated;
    TCOMMParm m_CommParm;
    TCPServer* pServer;
    TCPClient* pClient;
    //    WorkerThreadId: DWORD;

    bool m_ClientConnected;
    bool m_ServerConnected;
    long ServerConnectID;

    static void OnClientConnected(void* cls_inst);
    static void OnClientDisconnected(void* cls_inst);
    static void OnClientConnectFail(void* cls_inst);
    static void OnClientSendCompleted(char* buf, long len, long ExpLen, void* cls_inst);
    static void OnServerConnected(long ID, void* cls_inst);
    static void OnServerDisconnected(long ID, void* cls_inst);
    static void OnServerRecvCompleted(long ID, char* buf, long len, long ExpLen, void* cls_inst);
    static void* WorkerThread(void* lpParameter);

public:
    //static TCupsCOMM *inst;

    TCupsCOMM(PCOMMParm pCommParm);
    ~TCupsCOMM();
    void StartComm();
    void StopComm();
    bool IsStarted();
    void SetLog(CBaseLog* LogObj);
};