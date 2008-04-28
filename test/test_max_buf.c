/*
 * miniRPC - TCP RPC library with asynchronous operations
 *
 * Copyright (C) 2007-2008 Carnegie Mellon University
 *
 * This code is distributed "AS IS" without warranty of any kind under the
 * terms of the GNU Lesser General Public License version 2.1, as shown in
 * the file COPYING.
 */

#include "common.h"

int main(int argc, char **argv)
{
	struct mrpc_conn_set *sset;
	struct mrpc_conn_set *cset;
	struct mrpc_connection *conn;
	char *port;
	int ret;

	sset=spawn_server(&port, proto_server, sync_server_accept, NULL, 1);
	mrpc_set_disconnect_func(sset, disconnect_normal);
	mrpc_set_ioerr_func(sset, handle_ioerr);
	mrpc_set_max_buf_len(sset, 128);
	if (mrpc_conn_set_create(&cset, proto_client, NULL))
		die("Couldn't allocate conn set");
	mrpc_set_disconnect_func(cset, disconnect_user);
	mrpc_set_ioerr_func(cset, handle_ioerr);
	mrpc_set_max_buf_len(cset, 128);

	ret=mrpc_conn_create(&conn, cset, NULL);
	if (ret)
		die("%s", strerror(ret));
	ret=mrpc_connect(conn, AF_UNSPEC, "localhost", port);
	if (ret)
		die("%s", strerror(ret));

	start_monitored_dispatcher(cset);
	expect(send_buffer_sync(conn), MINIRPC_ENCODING_ERR);
	expect(proto_ping(conn), 0);
	expect(recv_buffer_sync(conn), MINIRPC_ENCODING_ERR);
	expect(proto_ping(conn), 0);
	msg_buffer_sync(conn);
	expect(proto_ping(conn), 0);
	mrpc_conn_close(conn);
	mrpc_conn_unref(conn);
	mrpc_conn_set_unref(cset);
	mrpc_listen_close(sset);
	mrpc_conn_set_unref(sset);
	expect_disconnects(1, 1, 0);
	expect_ioerrs(3);
	free(port);
	return 0;
}
