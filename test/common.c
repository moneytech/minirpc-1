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

#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include <glib.h>
#include "common.h"

static gint disc_normal;
static gint disc_ioerr;
static gint disc_user;
static gint ioerrs;

void _message(const char *file, int line, const char *func, const char *fmt,
			...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s line %d: %s(): ", file, line, func);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

struct mrpc_conn_set *spawn_server(unsigned *listen_port,
			const struct mrpc_protocol *protocol,
			mrpc_accept_fn accept, void *set_data, int threads)
{
	struct mrpc_conn_set *set;
	unsigned port=0;
	int ret;
	int i;

	if (mrpc_conn_set_create(&set, protocol, set_data))
		die("Couldn't allocate connection set");
	if (mrpc_set_accept_func(set, accept))
		die("Couldn't set accept function");
	ret=mrpc_listen(set, "localhost", &port);
	if (ret)
		die("%s", strerror(ret));
	for (i=0; i<threads; i++)
		mrpc_start_dispatch_thread(set);
	if (listen_port)
		*listen_port=port;
	return set;
}

void disconnect_fatal(void *conn_data, enum mrpc_disc_reason reason)
{
	die("Unexpected disconnect: reason %d", reason);
}

void disconnect_normal(void *conn_data, enum mrpc_disc_reason reason)
{
	if (reason != MRPC_DISC_CLOSED)
		die("Unexpected disconnect: reason %d", reason);
	g_atomic_int_inc(&disc_normal);
}

void disconnect_ioerr(void *conn_data, enum mrpc_disc_reason reason)
{
	if (reason != MRPC_DISC_IOERR)
		die("Unexpected disconnect: reason %d", reason);
	g_atomic_int_inc(&disc_ioerr);
}

void disconnect_user(void *conn_data, enum mrpc_disc_reason reason)
{
	if (reason != MRPC_DISC_USER)
		die("Unexpected disconnect: reason %d", reason);
	g_atomic_int_inc(&disc_user);
}

void handle_ioerr(void *conn_private, char *msg)
{
	g_atomic_int_inc(&ioerrs);
}

void expect_disconnects(int user, int normal, int ioerr)
{
	int count;

	count=g_atomic_int_get(&disc_user);
	if (user != -1 && count != user)
		die("Expected %d user disconnects, got %d", user, count);
	count=g_atomic_int_get(&disc_normal);
	if (normal != -1 && count != normal)
		die("Expected %d normal disconnects, got %d", normal, count);
	count=g_atomic_int_get(&disc_ioerr);
	if (ioerr != -1 && count != ioerr)
		die("Expected %d ioerr disconnects, got %d", ioerr, count);
}

void expect_ioerrs(int count)
{
	int have;

	have=g_atomic_int_get(&ioerrs);
	if (have != count)
		die("Expected %d I/O errors, got %d", count, have);
}
