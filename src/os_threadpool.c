// SPDX-License-Identifier: BSD-3-Clause

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <semaphore.h>


#include "os_threadpool.h"
#include "log/log.h"
#include "utils.h"

os_task_t *tasks[1000];
int num_tasks;

/* Create a task that would be executed by a thread. */
os_task_t *create_task(void (*action)(void *), void *arg, void (*destroy_arg)(void *))
{
	os_task_t *t;

	t = malloc(sizeof(*t));
	DIE(t == NULL, "malloc");

	t->action = action;		// the function
	t->argument = arg;		// arguments for the function
	t->destroy_arg = destroy_arg;	// destroy argument function



	return t;
}

/* Destroy task. */
void destroy_task(os_task_t *t)
{
	if (t->destroy_arg != NULL)
		t->destroy_arg(t->argument);
	free(t);
}

/* Put a new task to threadpool task queue. */
void enqueue_task(os_threadpool_t *tp, os_task_t *t)
{
	assert(tp != NULL);
	assert(t != NULL);

	/* TODO: Enqueue task to the shared task queue. Use synchronization. */
	pthread_mutex_lock(&tp->queueMutex);
	list_add_tail(&(tp->head), &(t->list));
	pthread_cond_signal(&tp->queueNotEmpty);
	pthread_mutex_unlock(&tp->queueMutex);
}

/*
 * Check if queue is empty.
 * This function should be called in a synchronized manner.
 */
static int queue_is_empty(os_threadpool_t *tp)
{
	return list_empty(&tp->head);
}

/*
 * Get a task from threadpool task queue.
 * Block if no task is available.
 * Return NULL if work is complete, i.e. no task will become available,
 * i.e. all threads are going to block.
 */

os_task_t *dequeue_task(os_threadpool_t *tp)
{
	os_task_t *t;

	/* TODO: Dequeue task from the shared task queue. Use synchronization. */

	pthread_mutex_lock(&tp->queueMutex);

	//done by me
	while (list_empty(&tp->head) && !tp->shutdown)
		pthread_cond_wait(&tp->queueNotEmpty, &tp->queueMutex);

	if (tp->shutdown) {
		pthread_mutex_unlock(&tp->queueMutex);
		return NULL;
	}
	//done by me
	os_list_node_t *ptr = tp->head.next;

	t = list_entry(ptr, os_task_t, list);
	list_del(tp->head.next);
	tp->num_tasks++;
	pthread_mutex_unlock(&tp->queueMutex);
	return t;
}

/* Loop function for threads */
static void *thread_loop_function(void *arg)
{
	os_threadpool_t *tp = (os_threadpool_t *) arg;

	while (1) {
		os_task_t *t;

		t = dequeue_task(tp);
		if (t == NULL)
			break;
		t->action(t->argument);
		destroy_task(t);
		pthread_mutex_lock(&tp->queueMutex);
		tp->num_tasks--;
		pthread_mutex_unlock(&tp->queueMutex);
		pthread_cond_signal(&tp->queueNotFull);
	}

	return NULL;
}

/* Wait completion of all threads. This is to be called by the main thread. */
void wait_for_completion(os_threadpool_t *tp)
{
	/* TODO: Wait for all worker threads. Use synchronization. */
	pthread_mutex_lock(&tp->queueMutex);
	while (!queue_is_empty(tp) || (tp->num_tasks > 0))
		pthread_cond_wait(&tp->queueNotFull, &tp->queueMutex);
	tp->shutdown = 1;
	pthread_mutex_unlock(&tp->queueMutex);
	pthread_cond_broadcast(&tp->queueNotEmpty);


	/* Join all worker threads. */
	for (unsigned int i = 0; i < tp->num_threads; i++)
		pthread_join(tp->threads[i], NULL);
}

/* Create a new threadpool. */
os_threadpool_t *create_threadpool(unsigned int num_threads)
{
	os_threadpool_t *tp = NULL;
	int rc;

	tp = malloc(sizeof(*tp));
	DIE(tp == NULL, "malloc");

	list_init(&tp->head);

	/* TODO: Initialize synchronization data. */
	tp->shutdown = 0;
	tp->num_tasks = 0;
	pthread_mutex_init(&tp->queueMutex, NULL);
	pthread_cond_init(&tp->queueNotEmpty, NULL);
	pthread_cond_init(&tp->queueNotFull, NULL);

	tp->num_threads = num_threads;
	tp->threads = malloc(num_threads * sizeof(*tp->threads));
	DIE(tp->threads == NULL, "malloc");
	for (unsigned int i = 0; i < num_threads; ++i) {
		rc = pthread_create(&tp->threads[i], NULL, &thread_loop_function, (void *) tp);
		DIE(rc < 0, "pthread_create");
	}

	return tp;
}

/* Destroy a threadpool. Assume all threads have been joined. */
void destroy_threadpool(os_threadpool_t *tp)
{
	os_list_node_t *n, *p;

	/* TODO: Cleanup synchronization data. */
	pthread_mutex_destroy(&tp->queueMutex);
	pthread_cond_destroy(&tp->queueNotEmpty);
	pthread_cond_destroy(&tp->queueNotFull);

	list_for_each_safe(n, p, &tp->head) {
		list_del(n);
		destroy_task(list_entry(n, os_task_t, list));
	}

	free(tp->threads);
	free(tp);
}
