#include "test_harness.h"
#include "PtrQueue.h"
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>

static void* producer(void* arg)
{
	PtrQueue* q = (PtrQueue*)arg;
	usleep(80 * 1000);
	q->Push((void*)(uintptr_t)0x1234);
	return NULL;
}

struct MPMCArgs {
	PtrQueue* q;
	int prod_id;
	int count;
	volatile int* pushed;
	volatile int* popped;
	volatile int* done_prods;
	int total_prods;
};

static void* mpmc_producer(void* arg)
{
	MPMCArgs* a = (MPMCArgs*)arg;
	for (int i = 0; i < a->count; i++) {
		// pack producer id + seq into pointer-sized token
		uintptr_t token = (((uintptr_t)(a->prod_id + 1)) << 24) | (uintptr_t)(i + 1);
		a->q->Push((void*)token);
		__sync_add_and_fetch(a->pushed, 1);
	}
	__sync_add_and_fetch(a->done_prods, 1);
	return NULL;
}

static void* mpmc_consumer(void* arg)
{
	MPMCArgs* a = (MPMCArgs*)arg;
	while (1) {
		void* p = a->q->Pop(100);
		if (p != NULL) {
			uintptr_t token = (uintptr_t)p;
			int prod = (int)(token >> 24);
			int seq = (int)(token & 0xFFFFFF);
			if (prod <= 0 || seq <= 0) {
				__sync_add_and_fetch(a->popped, -1000000); // poison failure marker
				break;
			}
			__sync_add_and_fetch(a->popped, 1);
			continue;
		}
		// timeout: exit only when all producers finished and queue empty
		if (__sync_fetch_and_add(a->done_prods, 0) >= a->total_prods
			&& a->q->getCount() == 0) {
			break;
		}
	}
	return NULL;
}

void run_ptrqueue_tests()
{
	test_section("PtrQueue");

	PtrQueue q1((size_t)8);
	CHECK(q1.getCount() == 0);
	CHECK(q1.Pop() == NULL);

	int a = 1, b = 2, c = 3;
	q1.Push(&a);
	q1.Push(&b);
	q1.Push(&c);
	CHECK(q1.getCount() == 3);
	CHECK(q1.Pop() == &a);
	CHECK(q1.Pop() == &b);
	CHECK(q1.Pop() == &c);
	CHECK(q1.getCount() == 0);

	// capacity growth
	PtrQueue q2((size_t)8);
	for (int i = 0; i < 200; i++)
		q2.Push((void*)(uintptr_t)(i + 1));
	CHECK(q2.getCount() == 200);
	bool ok = true;
	for (int i = 0; i < 200; i++) {
		if (q2.Pop() != (void*)(uintptr_t)(i + 1)) {
			ok = false;
			break;
		}
	}
	CHECK(ok == true);

	// blocking pop with timeout + single producer thread
	PtrQueue qb(true, 8);
	CHECK(qb.Pop(50) == NULL);

	pthread_t tid;
	pthread_create(&tid, NULL, producer, &qb);
	void* got = qb.Pop(1000);
	pthread_join(tid, NULL);
	CHECK(got == (void*)(uintptr_t)0x1234);

	// multi-producer / multi-consumer
	test_section("PtrQueue MPMC");
	const int N_PROD = 4;
	const int N_CONS = 4;
	const int PER_PROD = 500;
	const int EXPECT = N_PROD * PER_PROD;

	PtrQueue qm(true, 32);
	volatile int pushed = 0;
	volatile int popped = 0;
	volatile int done_prods = 0;

	pthread_t prod_t[N_PROD];
	pthread_t cons_t[N_CONS];
	MPMCArgs prod_args[N_PROD];
	MPMCArgs cons_args[N_CONS];

	for (int i = 0; i < N_CONS; i++) {
		cons_args[i].q = &qm;
		cons_args[i].prod_id = 0;
		cons_args[i].count = 0;
		cons_args[i].pushed = &pushed;
		cons_args[i].popped = &popped;
		cons_args[i].done_prods = &done_prods;
		cons_args[i].total_prods = N_PROD;
		pthread_create(&cons_t[i], NULL, mpmc_consumer, &cons_args[i]);
	}
	for (int i = 0; i < N_PROD; i++) {
		prod_args[i].q = &qm;
		prod_args[i].prod_id = i;
		prod_args[i].count = PER_PROD;
		prod_args[i].pushed = &pushed;
		prod_args[i].popped = &popped;
		prod_args[i].done_prods = &done_prods;
		prod_args[i].total_prods = N_PROD;
		pthread_create(&prod_t[i], NULL, mpmc_producer, &prod_args[i]);
	}

	for (int i = 0; i < N_PROD; i++)
		pthread_join(prod_t[i], NULL);
	for (int i = 0; i < N_CONS; i++)
		pthread_join(cons_t[i], NULL);

	CHECK(pushed == EXPECT);
	CHECK(popped == EXPECT);
	CHECK(qm.getCount() == 0);
}
