/*
 * miniRPC - TCP RPC library with asynchronous operations and TLS support
 *
 * Copyright (C) 2007-2008 Carnegie Mellon University
 *
 * This software is distributed under the terms of the Eclipse Public License,
 * Version 1.0 which can be found in the file named LICENSE.  ANY USE,
 * REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES RECIPIENT'S
 * ACCEPTANCE OF THIS AGREEMENT
 */

#include <pthread.h>
#include <assert.h>
#include <errno.h>
#define MINIRPC_POLLSET
#define MINIRPC_INTERNAL
#include "internal.h"
#include "pollset_impl.h"

extern const struct pollset_ops *ops_poll;
#ifdef HAVE_EPOLL
extern const struct pollset_ops *ops_epoll;
#else
const struct pollset_ops *ops_epoll = NULL;
#endif


static void poll_fd_free(struct poll_fd *pfd)
{
	g_slice_free(struct poll_fd, pfd);
}

/* pset lock must be held */
static void pollset_free_dead(struct pollset *pset)
{
	struct poll_fd *pfd;

	while ((pfd=g_queue_pop_head(pset->dead)) != NULL)
		poll_fd_free(pfd);
}

static void wakeup_pipe_err(void *ignored, int fd)
{
	(void)ignored;
	(void)fd;
	assert(0);
}

struct pollset *pollset_alloc(void)
{
	struct pollset *pset;
	int ret;

	pset=g_slice_new0(struct pollset);
	pset->wakeup=selfpipe_create();
	if (pset->wakeup == NULL) {
		g_slice_free(struct pollset, pset);
		return NULL;
	}
	pthread_mutex_init(&pset->lock, NULL);
	pthread_mutex_init(&pset->poll_lock, NULL);
	pthread_cond_init(&pset->serial_cond, NULL);
	pset->running_serial=NOT_RUNNING;
	pset->members=g_hash_table_new_full(g_int_hash, g_int_equal, NULL,
				(GDestroyNotify)poll_fd_free);
	pset->dead=g_queue_new();
	pset->ops = ops_epoll ?: ops_poll;
again:
	ret=pset->ops->create(pset);
	if (ret == -ENOSYS && pset->ops == ops_epoll) {
		pset->ops=ops_poll;
		goto again;
	} else if (ret) {
		goto cleanup;
	}
	if (pollset_add(pset, selfpipe_fd(pset->wakeup), POLLSET_READABLE,
				NULL, NULL, NULL, NULL, wakeup_pipe_err)) {
		pset->ops->destroy(pset);
		goto cleanup;
	}
	return pset;

cleanup:
	g_hash_table_destroy(pset->members);
	g_queue_free(pset->dead);
	selfpipe_destroy(pset->wakeup);
	g_slice_free(struct pollset, pset);
	return NULL;
}

void pollset_free(struct pollset *pset)
{
	pthread_mutex_lock(&pset->lock);
	assert(pset->running_serial == NOT_RUNNING);
	pset->ops->destroy(pset);
	g_hash_table_destroy(pset->members);
	pollset_free_dead(pset);
	g_queue_free(pset->dead);
	selfpipe_destroy(pset->wakeup);
	pthread_mutex_unlock(&pset->lock);
	g_slice_free(struct pollset, pset);
}

int pollset_add(struct pollset *pset, int fd, poll_flags_t flags,
			void *private, poll_callback_fn readable,
			poll_callback_fn writable, poll_callback_fn hangup,
			poll_callback_fn error)
{
	struct poll_fd *pfd;
	int ret;

	pfd=g_slice_new0(struct poll_fd);
	pfd->fd=fd;
	pfd->flags=flags;
	pfd->private=private;
	pfd->readable_fn=readable;
	pfd->writable_fn=writable;
	pfd->hangup_fn=hangup;
	pfd->error_fn=error;
	pthread_mutex_lock(&pset->lock);
	if (g_hash_table_lookup(pset->members, &pfd->fd) != NULL) {
		pthread_mutex_unlock(&pset->lock);
		poll_fd_free(pfd);
		return -EEXIST;
	}
	g_hash_table_replace(pset->members, &pfd->fd, pfd);
	ret=pset->ops->add(pset, pfd);
	if (ret)
		g_hash_table_remove(pset->members, &pfd->fd);
	pthread_mutex_unlock(&pset->lock);
	return ret;
}

int pollset_modify(struct pollset *pset, int fd, poll_flags_t flags)
{
	struct poll_fd *pfd;
	poll_flags_t old;
	int ret=0;

	pthread_mutex_lock(&pset->lock);
	pfd=g_hash_table_lookup(pset->members, &fd);
	if (pfd != NULL && flags != pfd->flags) {
		old=pfd->flags;
		pfd->flags=flags;
		ret=pset->ops->modify(pset, pfd);
		if (ret)
			pfd->flags=old;
	}
	pthread_mutex_unlock(&pset->lock);
	if (pfd == NULL)
		return -EBADF;
	else
		return ret;
}

void pollset_del(struct pollset *pset, int fd)
{
	struct poll_fd *pfd;
	int64_t need_serial;

	pthread_mutex_lock(&pset->lock);
	pfd=g_hash_table_lookup(pset->members, &fd);
	g_hash_table_steal(pset->members, &fd);
	need_serial=++pset->serial;
	pthread_mutex_unlock(&pset->lock);
	if (pfd == NULL)
		return;

	pset->ops->remove(pset, pfd);
	pollset_wake(pset);
	pthread_mutex_lock(&pset->lock);
	pfd->dead=1;
	g_queue_push_tail(pset->dead, pfd);
	while (pset->running_serial != NOT_RUNNING &&
				!pthread_equal(pset->running_thread,
				pthread_self()) &&
				pset->running_serial < need_serial)
		pthread_cond_wait(&pset->serial_cond, &pset->lock);
	pthread_mutex_unlock(&pset->lock);
}

int pollset_poll(struct pollset *pset)
{
	int ret;

	pthread_mutex_lock(&pset->poll_lock);
	pthread_mutex_lock(&pset->lock);
	pset->running_serial=pset->serial;
	pset->running_thread=pthread_self();
	pthread_mutex_unlock(&pset->lock);
	ret=pset->ops->poll(pset);
	pthread_mutex_lock(&pset->lock);
	pollset_free_dead(pset);
	pset->running_serial=NOT_RUNNING;
	pthread_cond_broadcast(&pset->serial_cond);
	pthread_mutex_unlock(&pset->lock);
	selfpipe_clear(pset->wakeup);
	pthread_mutex_unlock(&pset->poll_lock);
	return ret;
}

void pollset_wake(struct pollset *pset)
{
	selfpipe_set(pset->wakeup);
}
