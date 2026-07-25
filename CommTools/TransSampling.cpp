#include "TransSampling.h"
#include "commroute.h"

TransSampling::TransSampling()
{
	HisStatistics.b_tick = HisStatistics.e_tick = GetTickCount64();
	CurStatistics.b_tick = CurStatistics.e_tick = HisStatistics.b_tick;
	HisStatistics.tx_num_all = HisStatistics.tx_num_suc = 0;
	CurStatistics.tx_num_all = CurStatistics.tx_num_suc = 0;

	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&m_mutex, &attr);

}

TransSampling::~TransSampling()
{
	pthread_mutex_destroy(&m_mutex);
}

void TransSampling::Lock()
{
	pthread_mutex_lock(&m_mutex);
}

void TransSampling::UnLock()
{
	pthread_mutex_unlock(&m_mutex);
}


void TransSampling::AddSucTx(int iNum)
{
	Lock();
	HisStatistics.tx_num_suc += iNum;
	CurStatistics.tx_num_suc += iNum;
	CurStatistics.e_tick = GetTickCount64();
	UnLock();
}

void TransSampling::AddTx(int iNum)
{
	Lock();
	HisStatistics.tx_num_all += iNum;
	CurStatistics.tx_num_all += iNum;
	CurStatistics.e_tick = GetTickCount64();
	UnLock();
}


void TransSampling::Reset()
{
	Lock();
	CurStatistics.tx_num_all =  CurStatistics.tx_num_suc = 0;
	CurStatistics.b_tick = CurStatistics.e_tick = GetTickCount64();

	//if (CurStatistics.e_tick - CurStatistics.b_tick) >  1000 * m_Period)
	UnLock();
}

void TransSampling::GetStatistics(PStatistics pStatistics, bool bReset)
{
	Lock();
	  *pStatistics = CurStatistics;
	  if (bReset) 
		  Reset();
    UnLock();
}
