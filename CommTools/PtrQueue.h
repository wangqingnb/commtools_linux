#pragma once
#include <stddef.h>
#include <pthread.h>

class PtrQueue
{
private:
    size_t volatile m_Capacity;  //已分配保存指针数量（个数）
    size_t* volatile m_pData;      //保存指针的空间首地址
    size_t* volatile m_pDataEnd;   //保存指针的空间尾地址
    size_t* volatile m_pPop;     //当前pop指针
    size_t* volatile m_pPush;    //当前push指针
    size_t volatile m_Count;     //当前实际保存的数量
    bool m_bBlock;  //阻塞模式
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
    void allocMemory(const size_t nCapacity);
    void setCapacity(const size_t nCapacity);
    void init(size_t InitCapacity);
public:
    PtrQueue(size_t InitCapacity = 8);
    PtrQueue(bool bBlock, size_t InitCapacity = 8);
    ~PtrQueue();
    void Lock();
    void UnLock();
    void Push(void* ptr);
    void* Pop(long wait = 0);
    size_t getCount();
    size_t getCapacity();
};
 

