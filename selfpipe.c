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

#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#define MINIRPC_INTERNAL
#include "internal.h"

struct selfpipe {
	int pipe[2];
	int set;
	pthread_mutex_t lock;
};

struct selfpipe *selfpipe_create(void)
{
	struct selfpipe *sp;
	int flags;
	int i;

	sp=g_slice_new0(struct selfpipe);
	pthread_mutex_init(&sp->lock, NULL);
	if (pipe(sp->pipe))
		goto bad_free;
	for (i=0; i < 2; i++) {
		flags=fcntl(sp->pipe[i], F_GETFL);
		if (flags == -1)
			goto bad_close;
		if (fcntl(sp->pipe[i], F_SETFL, flags | O_NONBLOCK))
			goto bad_close;
	}
	return sp;

bad_close:
	close(sp->pipe[1]);
	close(sp->pipe[0]);
bad_free:
	g_slice_free(struct selfpipe, sp);
	return NULL;
}

void selfpipe_destroy(struct selfpipe *sp)
{
	close(sp->pipe[1]);
	close(sp->pipe[0]);
	g_slice_free(struct selfpipe, sp);
}

void selfpipe_set(struct selfpipe *sp)
{
	pthread_mutex_lock(&sp->lock);
	/* In order to be resilient to someone else (improperly) reading
	   from the read end of the pipe, we always write even if the pipe
	   is already readable. */
	write(sp->pipe[1], "a", 1);
	sp->set=1;
	pthread_mutex_unlock(&sp->lock);
}

void selfpipe_clear(struct selfpipe *sp)
{
	char buf[32];

	pthread_mutex_lock(&sp->lock);
	while (read(sp->pipe[0], buf, sizeof(buf)) == sizeof(buf));
	sp->set=0;
	pthread_mutex_unlock(&sp->lock);
}

int selfpipe_is_set(struct selfpipe *sp)
{
	int ret;

	pthread_mutex_lock(&sp->lock);
	ret=sp->set;
	pthread_mutex_unlock(&sp->lock);
	return ret;
}

int selfpipe_fd(struct selfpipe *sp)
{
	return sp->pipe[0];
}