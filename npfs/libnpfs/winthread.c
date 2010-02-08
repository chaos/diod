/*
 * winthread
 *	Thread portability layer for win32.
 *
 * condition variables discussed in
 * http://www.cs.wustl.edu/~schmidt/win32-cv-1.html
 */
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include "npfs.h"

enum {
	StackSize = 10*4096, // XXX?  wild guess.. whats good here?
};

int
pthread_mutex_init(pthread_mutex_t *mux, const pthread_mutexattr_t *dummy)
{
	mux->init = 1;
	mux->handle = CreateMutex(NULL, FALSE, NULL);
	return 0;
}

int
pthread_mutex_destroy(pthread_mutex_t *mux)
{
	CloseHandle(mux->handle);
	return 0;
}

int
pthread_mutex_lock(pthread_mutex_t *mux)
{
	HANDLE imux;

	if(!mux->init) {
		// CreateMutex is atomic and will reopen an existing mutex.
		// XXX probably minor security issue here, someone can deadlock us
		// by grabbing our lock.
		imux = CreateMutex(NULL, FALSE, L"Local\\pthread_mutex_initlock");
		if(!imux)
			return EINVAL;
		WaitForSingleObject(imux, INFINITE);
		if(!mux->init)
			pthread_mutex_init(mux, NULL);
		ReleaseMutex(imux);
	}
	WaitForSingleObject(mux->handle, INFINITE);
	return 0;
}

int
pthread_mutex_unlock(pthread_mutex_t *mux)
{
	ReleaseMutex(mux->handle);
	return 0;
}

int
pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *dummy)
{
	assert(!dummy);
	cond->waiters = 0;
	cond->broadcast = 0;
	pthread_mutex_init(&cond->lock, NULL);
	cond->queue = CreateSemaphore(NULL, 0, 0x7fffffff, NULL);
	if(!cond->queue)
		return ENOMEM;
	cond->done = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(!cond->done) {
		CloseHandle(cond->queue);
		return ENOMEM;
	}
	return 0;
}

int
pthread_cond_destroy(pthread_cond_t *cond)
{
	int busy;

	if(cond->waiters > 0)
		return EBUSY;
	pthread_mutex_destroy(&cond->lock);
	CloseHandle(cond->queue);
	CloseHandle(cond->done);
	return 0;
}

int
pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mux)
{
	int x, last;

	pthread_mutex_lock(&cond->lock);
	cond->waiters++;
	pthread_mutex_unlock(&cond->lock);

	// this does a pthread_mutex_unlock on mux
	x = SignalObjectAndWait(mux->handle, cond->queue, INFINITE, FALSE);
	assert(x == 0);

	pthread_mutex_lock(&cond->lock);
	cond->waiters--;
	last = cond->broadcast && cond->waiters == 0;
	pthread_mutex_unlock(&cond->lock);

	if(last)
		SignalObjectAndWait(cond->done, mux->handle, INFINITE, FALSE);
	else
		pthread_mutex_lock(mux);
	return 0;
}

int
pthread_cond_signal(pthread_cond_t *cond)
{
	int wake;

	pthread_mutex_lock(&cond->lock);
	wake = cond->waiters > 0;
	pthread_mutex_unlock(&cond->lock);
	if(wake)
		ReleaseSemaphore(cond->queue, 1, 0);
	return 0;
}

int
pthread_cond_broadcast(pthread_cond_t *cond)
{
	int wake;

	pthread_mutex_lock(&cond->lock);
	wake = 0;
	if(cond->waiters > 0) {
		cond->broadcast = 1;
		wake = 1;
	}
	if(wake)
		ReleaseSemaphore(cond->queue, cond->waiters, 0);
	pthread_mutex_unlock(&cond->lock);
	if(wake) {
		WaitForSingleObject(cond->done, INFINITE);
		cond->broadcast = 0;
	} 
	return 0;
}

static pthread_once_t handInit = PTHREAD_ONCE_INIT;
static pthread_key_t handKey;

static void
init(void) {
	pthread_key_create(&handKey, NULL);
}

static DWORD
trampoline(void *arg)
{
	pthread_t thr = (pthread_t)arg;
	pthread_setspecific(handKey, thr);
	thr->retval = thr->run(thr->arg);
	if(thr->detached)
		free(thr);
	return 0;
}


int
pthread_create(pthread_t *pthr, const pthread_attr_t *dummy, void *(*run)(void*), void *arg)
{
	HANDLE h;
	pthread_t thr;

	assert(!dummy);
	pthread_once(&handInit, init);
	thr = malloc(sizeof *thr);
	thr->detached = 0;
	thr->arg = arg;
	thr->run = run;
	h = CreateThread(NULL, StackSize, trampoline, thr, 0, NULL);
	if(!h) {
		free(thr);
		return EAGAIN;
	}
	thr->handle = h;
	*pthr = thr;
	return 0;
}

int
pthread_join(pthread_t thr, void **retval)
{
	if(WaitForSingleObject(thr->handle, INFINITE) != WAIT_OBJECT_0)
		return ESRCH;
	*retval = thr->retval;
	return 0;
}

int
pthread_detach(pthread_t thr)
{
	thr->detached = 1;
	return 0;
}

pthread_t
pthread_self(void)
{
	return pthread_getspecific(handKey);
}

int
pthread_once(pthread_once_t *once, void (*func)(void))
{
	static pthread_mutex_t oncelock = PTHREAD_MUTEX_INITIALIZER;

	if(!once->init) {
		pthread_mutex_lock(&oncelock);
		if(!once->init)
			func();
		once->init = 1;
		pthread_mutex_unlock(&oncelock);
	}
	return 0;
}

int
pthread_key_create(pthread_key_t *key, void (*destroy)(void*))
{
	DWORD tlsidx;

	tlsidx = TlsAlloc();
	if(tlsidx == TLS_OUT_OF_INDEXES)
		return EAGAIN;
	key->tlsidx = tlsidx;
	key->destroy = destroy;
	return 0;
}

void *
pthread_getspecific(pthread_key_t key)
{
	return TlsGetValue(key.tlsidx);
}

int
pthread_setspecific(pthread_key_t key, const void *val)
{
	PVOID old;

	old = pthread_getspecific(key);
	if(old)
		key.destroy(old);
	if(!TlsSetValue(key.tlsidx, (void*)val))
		return EINVAL;
	return 0;
}

