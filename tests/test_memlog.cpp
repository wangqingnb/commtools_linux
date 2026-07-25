#include "test_harness.h"
#include "MemLog.h"
#include <pthread.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>

struct LogThreadArgs {
	CSysLog* log;
	int tid;
	int count;
};

struct DrainArgs {
	CSysLog* log;
	std::string* out;
};

static void* log_writer(void* arg)
{
	LogThreadArgs* a = (LogThreadArgs*)arg;
	for (int i = 0; i < a->count; i++) {
		a->log->WriteLogWithFormat("T%d-%d\n", a->tid, i);
	}
	return NULL;
}

static void* log_writer_cstr(void* arg)
{
	LogThreadArgs* a = (LogThreadArgs*)arg;
	for (int i = 0; i < a->count; i++) {
		char buf[64];
		std::snprintf(buf, sizeof(buf), "C%d-%d\n", a->tid, i);
		a->log->WriteLog(buf);
	}
	return NULL;
}

static void* log_drainer(void* arg)
{
	DrainArgs* d = (DrainArgs*)arg;
	for (int k = 0; k < 200; k++) {
		std::string chunk = d->log->GetLog();
		if (!chunk.empty())
			*(d->out) += chunk;
		usleep(1000);
	}
	for (;;) {
		std::string chunk = d->log->GetLog();
		if (chunk.empty())
			break;
		*(d->out) += chunk;
	}
	return NULL;
}

static std::string drain_all(CSysLog& log)
{
	std::string all;
	for (;;) {
		std::string chunk = log.GetLog();
		if (chunk.empty())
			break;
		all += chunk;
	}
	return all;
}

static int count_token(const std::string& s, const std::string& token)
{
	int n = 0;
	size_t pos = 0;
	while ((pos = s.find(token, pos)) != std::string::npos) {
		n++;
		pos += token.size();
	}
	return n;
}

void run_memlog_tests()
{
	test_section("CSysLog basic");

	CSysLog log;
	log.WriteLog("hello\n");
	std::string s = "world\n";
	log.WriteLog(s);
	log.WriteLogWithFormat("n=%d\n", 42);

	std::string out = drain_all(log);
	CHECK(out.find("hello\n") != std::string::npos);
	CHECK(out.find("world\n") != std::string::npos);
	CHECK(out.find("n=42\n") != std::string::npos);
	CHECK(drain_all(log).empty());

	test_section("CSysLog multithread");

	const int N_THREADS = 4;
	const int PER_THREAD = 200;
	CSysLog mlog;
	pthread_t tids[N_THREADS];
	LogThreadArgs args[N_THREADS];

	for (int i = 0; i < N_THREADS; i++) {
		args[i].log = &mlog;
		args[i].tid = i;
		args[i].count = PER_THREAD;
		pthread_create(&tids[i], NULL, log_writer, &args[i]);
	}
	for (int i = 0; i < N_THREADS; i++)
		pthread_join(tids[i], NULL);

	std::string all = drain_all(mlog);
	CHECK((int)all.size() > 0);

	bool all_found = true;
	for (int t = 0; t < N_THREADS && all_found; t++) {
		for (int i = 0; i < PER_THREAD; i++) {
			char token[64];
			std::snprintf(token, sizeof(token), "T%d-%d\n", t, i);
			if (all.find(token) == std::string::npos) {
				all_found = false;
				break;
			}
		}
	}
	CHECK(all_found == true);

	// WriteLog(const char*) writers + concurrent GetLog reader
	CSysLog mlog2;
	pthread_t wids[N_THREADS];
	pthread_t rid;
	LogThreadArgs wargs[N_THREADS];
	std::string concurrent_out;
	DrainArgs drain;
	drain.log = &mlog2;
	drain.out = &concurrent_out;

	for (int i = 0; i < N_THREADS; i++) {
		wargs[i].log = &mlog2;
		wargs[i].tid = i;
		wargs[i].count = PER_THREAD;
		pthread_create(&wids[i], NULL, log_writer_cstr, &wargs[i]);
	}
	pthread_create(&rid, NULL, log_drainer, &drain);

	for (int i = 0; i < N_THREADS; i++)
		pthread_join(wids[i], NULL);
	pthread_join(rid, NULL);

	concurrent_out += drain_all(mlog2);

	bool c_ok = true;
	for (int t = 0; t < N_THREADS && c_ok; t++) {
		for (int i = 0; i < PER_THREAD; i++) {
			char token[64];
			std::snprintf(token, sizeof(token), "C%d-%d\n", t, i);
			if (count_token(concurrent_out, token) != 1) {
				c_ok = false;
				break;
			}
		}
	}
	CHECK(c_ok == true);
}
