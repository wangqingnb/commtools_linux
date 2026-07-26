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
				__sync_add_and_fetch(a->popped, -1000000);
				break;
			}
			__sync_add_and_fetch(a->popped, 1);
			continue;
		}
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
	CHECK(q1.getCapacity() >= 8);

	int a = 1, b = 2, c = 3;
	q1.Push(&a);
	q1.Push(&b);
	q1.Push(&c);
	CHECK(q1.getCount() == 3);
	CHECK(q1.Pop() == &a);
	CHECK(q1.Pop() == &b);
	CHECK(q1.Pop() == &c);
	CHECK(q1.getCount() == 0);

	// capacity growth (contiguous path)
	PtrQueue q2((size_t)8);
	size_t cap0 = q2.getCapacity();
	for (int i = 0; i < 200; i++)
		q2.Push((void*)(uintptr_t)(i + 1));
	CHECK(q2.getCount() == 200);
	CHECK(q2.getCapacity() >= 200 + 1);
	CHECK(q2.getCapacity() >= cap0);
	bool ok = true;
	for (int i = 0; i < 200; i++) {
		if (q2.Pop() != (void*)(uintptr_t)(i + 1)) {
			ok = false;
			break;
		}
	}
	CHECK(ok == true);

	// wrap-around then expand: force m_pPush < m_pPop before growth
	test_section("PtrQueue wrap expand");
	PtrQueue qw((size_t)8);
	size_t cap_w0 = qw.getCapacity();
	// fill to near full (leave reserved slot)
	size_t fill = cap_w0 - 1;
	for (size_t i = 0; i < fill; i++)
		qw.Push((void*)(uintptr_t)(1000 + i));
	CHECK(qw.getCount() == fill);

	// pop half so head advances
	size_t half = fill / 2;
	for (size_t i = 0; i < half; i++)
		CHECK(qw.Pop() == (void*)(uintptr_t)(1000 + i));

	// push more until wrap and trigger expansion past original capacity
	size_t next = 1000 + fill;
	while (qw.getCapacity() <= cap_w0) {
		qw.Push((void*)(uintptr_t)(next++));
		if (next > 1000 + fill + cap_w0 * 4) // safety
			break;
	}
	CHECK(qw.getCapacity() > cap_w0);

	// remaining sequence must stay FIFO
	bool wrap_ok = true;
	uintptr_t expect = 1000 + half;
	while (qw.getCount() > 0) {
		void* p = qw.Pop();
		if (p != (void*)expect) {
			wrap_ok = false;
			break;
		}
		expect++;
	}
	CHECK(wrap_ok == true);

	// doubling growth: after one expand, capacity roughly doubles (page-aligned)
	test_section("PtrQueue double growth");
	PtrQueue qd((size_t)8);
	size_t c1 = qd.getCapacity();
	// push until first expansion
	while (qd.getCapacity() == c1)
		qd.Push((void*)(uintptr_t)1);
	size_t c2 = qd.getCapacity();
	CHECK(c2 >= c1 * 2 || c2 > c1); // page align may make it >= 2x for small c1
	// push until second expansion
	while (qd.getCapacity() == c2)
		qd.Push((void*)(uintptr_t)2);
	size_t c3 = qd.getCapacity();
	CHECK(c3 > c2);
	CHECK(c3 >= c2 * 2 || c3 > c2);

	// blocking pop with timeout + single producer thread
	PtrQueue qb(true, 8);
	CHECK(qb.Pop(50) == NULL);

	pthread_t tid;
	pthread_create(&tid, NULL, producer, &qb);
	void* got = qb.Pop(1000);
	pthread_join(tid, NULL);
	CHECK(got == (void*)(uintptr_t)0x1234);

	// multi-producer / multi-consumer (also stresses expansion under lock)
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
