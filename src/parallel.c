#include "parallel.h"

#include <pthread.h>
#include <stddef.h>

#define PARALLEL_THRESHOLD 1024

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define THREAD_LOCAL _Thread_local
#else
#define THREAD_LOCAL __thread
#endif

// contains items needed for a parallel task (function, context, start + end)
typedef struct ParallelTask{
	ParallelRangeFn fn;
	void *ctx;
	size_t start;
	size_t end;
} ParallelTask;

// per thread counter
static THREAD_LOCAL int g_parallel_depth = 0;

// does a function and sets per thread depth to 1
static void *parallel_worker(void *arg){
	ParallelTask *task = (ParallelTask *)arg;

	g_parallel_depth = 1;
	task->fn(task->start, task->end, task->ctx);

	return NULL;
}

// equivilent of a parallel for loop, splits the range into 2 and then assigns each part to a worker
void parallel_for(size_t count, ParallelRangeFn fn, void *ctx){
	if(!fn || count == 0){
		return;
	}
	// if below parallel threshold then it's not worth paralellizing
	if(count < PARALLEL_THRESHOLD || g_parallel_depth > 0){
		fn(0, count, ctx);
		return;
	}

	// calculate the middle point
	size_t mid = count / 2;
	ParallelTask task = {.fn = fn, .ctx = ctx, .start = 0, .end = mid};	// create a task
	pthread_t thread;	// make a thread object
	int created = pthread_create(&thread, NULL, parallel_worker, &task);	// initialize the thread on the task
	g_parallel_depth++;	// increment depth
	fn(mid, count, ctx);	// do second half of the work (other half will be done by worker)
	g_parallel_depth--;	// decrement depth
	if(created == 0){	// nothing went wrong just rejoin
		pthread_join(thread, NULL);
	}
	else{	// something went wrong just do it out
		task.fn(task.start, task.end, task.ctx);
	}
}

// info needed to run a task (just a function and its context)
typedef struct ParallelRunTask{
	void (*fn)(void *);
	void *ctx;
} ParallelRunTask;

// runs a task
static void *parallel_run_worker(void *arg){
	ParallelRunTask *task = (ParallelRunTask *)arg;

	g_parallel_depth = 1;
	task->fn(task->ctx);

	return NULL;
}

// runs 2 tasks parallel-ly
void parallel_run(void (*fn1)(void *), void *ctx1, void (*fn2)(void *), void *ctx2){
	if(!fn1 && !fn2){	// at least one function has to exist
		return;
	}
	// if only one exists just do the other
	if(!fn1){
		fn2(ctx2);
		return;
	}
	if(!fn2){
		fn1(ctx1);
		return;
	}

	// make task for function1
	ParallelRunTask task = {.fn = fn1, .ctx = ctx1};
	pthread_t thread;	// create thread object
	if(pthread_create(&thread, NULL, parallel_run_worker, &task) != 0){
		// if it fails to make a thread just do both here
		fn1(ctx1);
		fn2(ctx2);
		return;
	}

	// do function 2 part of the work
	g_parallel_depth++;
	fn2(ctx2);
	g_parallel_depth--;

	pthread_join(thread, NULL);
}
