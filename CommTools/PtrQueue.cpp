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
	pthread_mutex_init(&m_mutex, &attr);
	pthread_mutexattr_destroy(&attr);

	if (m_bBlock) {
		int r = pthread_cond_init(&m_cond, NULL);
		if (r != 0)
		{
			char buf[MAX_MSG_SIZE];
			snprintf(buf, MAX_MSG_SIZE, "pthread_cond_init Failed!  Code: %d, %s\n", errno, strerror(errno));
			throw CRK_Exception(buf);
		}
	}

	m_Capacity = 0;
	m_pData = 0;
	m_pDataEnd = 0;
	m_Count = 0;
	allocMemory(InitCapacity);
}

PtrQueue::PtrQueue(size_t InitCapacity)
{
	m_bBlock = false;
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

	// 按页对齐计算实际分配字节数与可容纳指针个数
	size_t nSizeInByte = nCapacity * sizeof(void*);
	size_t nPagesCount = (nSizeInByte + PAGE_SIZE - 1) / PAGE_SIZE;
	if (nPagesCount == 0)
		nPagesCount = 1;
	size_t nAllocSize = nPagesCount * PAGE_SIZE;
	size_t newCapacity = nAllocSize / sizeof(void*);

	size_t* pTmpData = (size_t*)malloc(nAllocSize);
	if (pTmpData == NULL)
	{
		char buf[MAX_MSG_SIZE];
		snprintf(buf, MAX_MSG_SIZE, "PtrQueue::allocMemory malloc(%zu) failed\n", nAllocSize);
		throw CRK_Exception(buf);
	}

	if (m_pData != NULL)
	{
		if (m_Count > 0)
		{
			// 将环形缓冲中的有效元素拉直拷贝到新缓冲开头，长度严格按 m_Count
			if (m_pPush > m_pPop)
			{
				memcpy(pTmpData, m_pPop, m_Count * sizeof(void*));
			}
			else
			{
				// 已绕回: [m_pPop .. m_pDataEnd] + [m_pData .. m_pPush)
				size_t nFirst = (size_t)(m_pDataEnd - m_pPop + 1);
				size_t nSecond = m_Count - nFirst;
				memcpy(pTmpData, m_pPop, nFirst * sizeof(void*));
				if (nSecond > 0)
					memcpy(pTmpData + nFirst, m_pData, nSecond * sizeof(void*));
			}
			m_pPop = pTmpData;
			m_pPush = m_pPop + m_Count;
		}
		else
		{
			m_pPop = pTmpData;
			m_pPush = pTmpData;
		}
		free(m_pData);
	}
	else
	{
		m_pPop = pTmpData;
		m_pPush = pTmpData;
	}

	m_pData = pTmpData;
	m_Capacity = newCapacity;
	m_pDataEnd = m_pData + (m_Capacity - 1);
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
	// 预留 1 个空槽区分空/满；容量不足时按 2 倍扩容（摊销 O(1)）
	if (m_Count >= m_Capacity - 1)
	{
		size_t need = m_Capacity * 2;
		if (need <= m_Capacity) // 溢出兜底：至少再扩一页
			need = m_Capacity + PAGE_SIZE / sizeof(void*);
		allocMemory(need);
	}
	*m_pPush = (size_t)ptr;
	if (m_pPush == m_pDataEnd)
		m_pPush = m_pData;
	else
		m_pPush++;

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
			struct timespec abstime;
			struct timeval now;
			gettimeofday(&now, NULL);
			long nsec = now.tv_usec * 1000 + (wait % 1000) * 1000000;
			abstime.tv_sec = now.tv_sec + nsec / 1000000000 + wait / 1000;
			abstime.tv_nsec = nsec % 1000000000;
			while (1) { //循环等待
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
	Lock();
	size_t n = m_Count;
	UnLock();
	return n;
}
