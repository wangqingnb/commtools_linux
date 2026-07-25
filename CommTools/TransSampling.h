#pragma once
#include "commroute.h"

#pragma pack(1)
//缴费统计信息，用于统计成功率、TPS
typedef struct _TStatistics {
	int tx_num_all;  //总交易笔数
	int tx_num_suc;  //成功交易笔数
	_U64  b_tick;  //采样起始系统tick
	_U64  e_tick;  //采样结束系统时间
}  TStatistics, *PStatistics;
#pragma pack()

class TransSampling
{
private:
	TStatistics CurStatistics;  //当前周期
	TStatistics HisStatistics;  //启动以来
	pthread_mutex_t m_mutex;
	void Lock();
	void UnLock();

public:
	TransSampling();
	~TransSampling();
	void AddSucTx(int iNum=1);  //添加成功交易量
	void AddTx(int iNum=1);     //添加总交易量
	void Reset();
	void GetStatistics(PStatistics pStatistics, bool bReset = true);
};
