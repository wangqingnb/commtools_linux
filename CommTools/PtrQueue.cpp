#include "commroute.h"
#include "PtrQueue.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdio.h>
#include "RK_Exception.h"

#define PAGE_SIZE 4096

void PtrQueue::init(size_t InitCapacity)
{
	//嵌套锁
	//printf("ptrQueu init start\n ");
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&m_mutex,&attr);

    if (m_bBlock) {
		int r =	pthread_cond_init(&m_cond, NULL);
		if (r < 0)
		{
			char buf[MAX_MSG_SIZE];
			snprintf(buf, MAX_MSG_SIZE, "pthread_cond_init Failed!  Code: %d, %s\n", errno, strerror(errno));
			throw CRK_Exception(buf);
		}
		//printf("cond_init result = %d \n", r);
	}

	m_Capacity = 0;
	m_pData = 0;
	m_pDataEnd = 0;
	m_Count = 0;
	allocMemory(InitCapacity);
}

PtrQueue::PtrQueue(size_t InitCapacity)
{
	init(InitCapacity);
}


PtrQueue::PtrQueue(bool bBlock, size_t InitCapacity)
{
	m_bBlock = bBlock;
	init(InitCapacity);
}


PtrQueue::~PtrQueue()
{
	Lock();
	free(m_pData);
	UnLock();
	if (m_bBlock)
		pthread_cond_destroy(&m_cond);
	pthread_mutex_destroy(&m_mutex);
}

void PtrQueue::allocMemory(const size_t nCapacity)
{
	if (nCapacity <= m_Capacity)
		return;

	//根据所需要存放的个数，并按页面对齐方式计算出的实际页数
	size_t nSizeInByte = nCapacity * sizeof(void *);
	size_t nPagesCount = nSizeInByte / PAGE_SIZE;
	if ((nSizeInByte % PAGE_SIZE) > 0) 
		nPagesCount++;

	//分配空间（按上面的页面数计算出的数据）
	size_t nAllocSize = nPagesCount * PAGE_SIZE;  //字节数
	m_Capacity = nAllocSize / sizeof(void*); //个数		
	size_t* pTmpData = (size_t*)malloc(nAllocSize);

	if (m_pData != NULL)  //如果是需要扩容则重新分配空间
	{
		if (m_pPush < m_pPop)
		{   size_t iLen = (size_t)m_pDataEnd - (size_t)m_pPop + sizeof(void*);
			memcpy(pTmpData, m_pPop, iLen);
			memcpy(pTmpData + iLen / sizeof(void*), m_pData, (size_t)m_pPush - (size_t)m_pData + sizeof(void*));
			m_pPop = pTmpData;
			m_pPush = m_pPop + m_Count;
		} else
		{
			memcpy(pTmpData, m_pPop, (size_t)m_pPush - (size_t)m_pPop + sizeof(void*));
			m_pPop = pTmpData;
			m_pPush = m_pPop +  m_Count;
		}
		free(m_pData);  //释放原空间
	} else {  //初始情况
		m_pPop = pTmpData;
		m_pPush = pTmpData;
	}

	m_pData = pTmpData;
	pTmpData = NULL;
	m_pDataEnd = m_pData + (m_Capacity - 1);  //m_pDataEnd定位到最后

}

void PtrQueue::Lock()
{
	pthread_mutex_lock(&m_mutex);
}

void PtrQueue::UnLock()
{
	pthread_mutex_unlock(&m_mutex);
}


void PtrQueue::setCapacity(const size_t nCapacity)
{
	Lock();
	allocMemory(nCapacity);
	UnLock();
}
 
size_t PtrQueue::getCapacity()
{
    return m_Capacity;
}

void PtrQueue::Push(void* ptr)
{
	Lock();
	if (m_Count >= m_Capacity - 1)  //需要重新分配内存
	{
		allocMemory(m_Capacity + PAGE_SIZE / sizeof(void*));  //新加一个页面大小的内存
	}
	*m_pPush = (size_t)ptr; //保存指针值
    if (m_pPush == m_pDataEnd) //到尾部后重新指向到头
		m_pPush = m_pData;
	else
		m_pPush++;

	//队列里有数据了就通知
	if (m_bBlock && m_Count == 0) {
		pthread_cond_signal(&m_cond);
	}
	m_Count++;
	UnLock();
}
 

void* PtrQueue::Pop(long wait)
{
	int iResult = -1;
	Lock();
	//判断队列是否为空
	if (m_pPush == m_pPop) {
		if (m_bBlock && wait > 0) {  //如果是阻塞模式
			//UnLock(); 
			//注意：不需要解锁，因为pthread_cond_timedwait会自动解锁， 
			//这个函数内部过程为：//解锁->等待->加锁
			while (1) { //循环等待
				struct timespec abstime;
				struct timeval now;
				gettimeofday(&now, NULL);
				long nsec = now.tv_usec * 1000 + (wait % 1000) * 1000000;
				abstime.tv_sec=now.tv_sec + nsec / 1000000000 + wait / 1000;
				abstime.tv_nsec=nsec % 1000000000;
				iResult = pthread_cond_timedwait(&m_cond, &m_mutex, &abstime);
				if (iResult == 0) {  //收到事件通知了
					if (m_pPush != m_pPop) //有数据了
						break;
				}
				else {
					UnLock();
					//printf("time out or error!\n");
					return NULL;
				}
			}
		}
		else {
			UnLock();
			return NULL;
		}
	}

	void* ptr = (void*)*m_pPop;
	if (m_pPop == m_pDataEnd) //到尾部后重新指向到头
		m_pPop = m_pData;
	else
		m_pPop++;
	m_Count--;
	UnLock();
	return ptr;
}

size_t PtrQueue::getCount()
{
	return m_Count;
}
