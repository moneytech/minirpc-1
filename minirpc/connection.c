#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#define MINIRPC_INTERNAL
#include "internal.h"

#define POLLEVENTS (EPOLLIN|EPOLLERR|EPOLLHUP)

static unsigned conn_hash(struct list_head *entry, unsigned buckets)
{
	struct mrpc_connection *conn=list_entry(entry, struct mrpc_connection,
				lh_conns);
	return conn->fd % buckets;
}

static int conn_match(struct list_head *entry, void *data)
{
	struct mrpc_connection *conn=list_entry(entry, struct mrpc_connection,
				lh_conns);
	int *fd=data;
	return (*fd == conn->fd);
}

/* XXX unused since epoll provides a data pointer */
static struct mrpc_connection *conn_lookup(struct mrpc_conn_set *set, int fd)
{
	struct list_head *head;
	
	head=hash_get(set->conns, conn_match, fd, &fd);
	if (head == NULL)
		return NULL;
	return list_entry(head, struct mrpc_connection, lh_conns);
}

static int set_nonblock(int fd)
{
	int flags;
	
	flags=fcntl(fd, F_GETFL);
	if (flags == -1)
		return -errno;
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK))
		return -errno;
	return 0;
}

int mrpc_conn_add(struct mrpc_connection **new_conn, struct mrpc_conn_set *set,
			int fd, void *data)
{
	struct mrpc_connection *conn;
	struct epoll_event event;
	int ret;
	
	ret=set_nonblock(fd);
	if (ret)
		return ret;
	conn=malloc(sizeof(*conn));
	if (conn == NULL)
		return -ENOMEM;
	memset(conn, 0, sizeof(*conn));
	INIT_LIST_HEAD(&conn->lh_conns);
	INIT_LIST_HEAD(&conn->send_msgs);
	pthread_rwlock_init(&conn->operations_lock, NULL);
	pthread_mutex_init(&conn->send_msgs_lock, NULL);
	pthread_mutex_init(&conn->pending_replies_lock, NULL);
	pthread_mutex_init(&conn->sync_wakeup_lock, NULL);
	pthread_mutex_init(&conn->next_sequence_lock, NULL);
	conn->set=set;
	conn->fd=fd;
	conn->private=data;
	conn->pending_replies=hash_alloc(set->msg_buckets, request_hash);
	if (conn->pending_replies == NULL) {
		free(conn);
		return -ENOMEM;
	}
	event.events=POLLEVENTS;
	event.data.ptr=conn;
	if (epoll_ctl(set->epoll_fd, EPOLL_CTL_ADD, fd, &event)) {
		ret=-errno;
		hash_free(conn->pending_replies);
		free(conn);
		return ret;
	}
	pthread_mutex_lock(&set->conns_lock);
	hash_add(set->conns, &conn->lh_conns);
	pthread_mutex_unlock(&set->conns_lock);
	*new_conn=conn;
	return 0;
}

void mrpc_conn_remove(struct mrpc_connection *conn)
{
	struct mrpc_conn_set *set=conn->set;
	
	/* XXX data already in buffer? */
	pthread_mutex_lock(&set->conns_lock);
	hash_remove(set->conns, &conn->lh_conns);
	pthread_mutex_unlock(&set->conns_lock);
	hash_free(conn->pending_replies);
	free(conn);
}

int mrpc_conn_set_operations(struct mrpc_connection *conn,
			struct mrpc_protocol *protocol, void *ops)
{
	if (conn->set->protocol != protocol)
		return MINIRPC_INVALID_ARGUMENT;
	pthread_rwlock_wrlock(&conn->operations_lock);
	conn->operations=ops;
	pthread_rwlock_unlock(&conn->operations_lock);
	return MINIRPC_OK;
}

static int need_writable(struct mrpc_connection *conn, int writable)
{
	struct epoll_event event;
	
	event.data.ptr=conn;
	event.events=POLLEVENTS;
	if (writable)
		event.events |= EPOLLOUT;
	return epoll_ctl(conn->set->epoll_fd, EPOLL_CTL_MOD, conn->fd, &event);
}

static void conn_kill(struct mrpc_connection *conn)
{
	close(conn->fd);
	/* XXX */
}

static int process_incoming_header(struct mrpc_connection *conn)
{
	int ret;
	
	ret=unserialize(xdr_mrpc_header, conn->recv_hdr_buf,
				MINIRPC_HEADER_LEN, conn->recv_msg->hdr,
				sizeof(conn->recv_msg->hdr));
	if (ret)
		return ret;
	if (conn->recv_msg->hdr.datalen > conn->set->maxbuf) {
		/* XXX doesn't get returned to client if request */
  		return MINIRPC_ENCODING_ERR;
	}
	
	conn->recv_msg->data=malloc(conn->recv_msg->hdr.datalen);
	if (conn->recv_msg->data == NULL) {
		/* XXX */
		return MINIRPC_NOMEM;
	}
	conn->recv_state=STATE_DATA;
	return 0;
}

static void try_read_conn(struct mrpc_connection *conn)
{
	ssize_t count;
	char *buf;
	unsigned len;
	
	printf("try_read_conn\n");
	while (1) {
		if (conn->recv_msg == NULL) {
			conn->recv_msg=mrpc_alloc_message(conn);
			if (conn->recv_msg == NULL) {
				/* XXX */
			}
		}
		
		switch (conn->recv_state) {
		case STATE_HEADER:
			buf=&conn->recv_hdr_buf;
			len=MINIRPC_HEADER_LEN;
			break;
		case STATE_DATA:
			buf=conn->recv_msg->data;
			len=conn->recv_msg->hdr.datalen;
			break;
		}
		
		if (conn->recv_offset < len) {
			printf("Read, start %d, count %d\n", conn->recv_offset,
						len - conn->recv_offset);
			count=read(conn->fd, buf + conn->recv_offset,
						len - conn->recv_offset);
			if (count == -1 && errno == EINTR) {
				continue;
			} else if (count == -1 && errno == EAGAIN) {
				return;
			} else if (count == 0 || count == -1) {
				conn_kill(conn);
				return;
			}
			conn->recv_offset += count;
		}
		
		if (conn->recv_offset == len) {
			switch (conn->recv_state) {
			case STATE_HEADER:
				if (process_incoming_header(conn)) {
					/* XXX */
					;
				}
				break;
			case STATE_DATA:
				if (process_incoming_message(conn)) {
					/* XXX */
				}
				conn->recv_state=STATE_HEADER;
				conn->recv_msg=NULL;
				break;
			}
		}
	}
}

static int get_next_message(struct mrpc_connection *conn)
{
	int ret;
	
	pthread_mutex_lock(&conn->send_msgs_lock);
	if (list_is_empty(&conn->send_msgs)) {
		need_writable(conn, 0);
		pthread_mutex_unlock(&conn->send_msgs_lock);
		return 0;
	}
	conn->send_msg=list_first_entry(&conn->send_msgs, struct mrpc_message,
				lh_msgs);
	list_del_init(&conn->send_msg->lh_msgs);
	pthread_mutex_unlock(&conn->send_msgs_lock);
	
	conn->send_state=STATE_HEADER;
	ret=serialize_len((xdrproc_t)xdr_mrpc_header, &conn->send_msg->hdr,
				conn->send_hdr_buf, MINIRPC_HEADER_LEN);
	if (ret) {
		/* XXX message dropped on floor */
		mrpc_free_message(conn->send_msg);
		conn->send_msg=NULL;
		return ret;
	}
	conn->send_offset=0;
	return 0;
}

/* XXX cork would be useful here */
static void try_write_conn(struct mrpc_connection *conn)
{
	ssize_t count;
	int ret;
	char *buf;
	unsigned len;
	
	printf("try_write_conn\n");
	while (1) {
		if (conn->send_msg == NULL) {
			if (get_next_message(conn)) {
				/* XXX */
				conn_kill(conn);
				break;
			}
			if (conn->send_msg == NULL)
				break;
		}
		
		switch (conn->send_state) {
		case STATE_HEADER:
			buf=&conn->send_hdr_buf;
			len=MINIRPC_HEADER_LEN;
			break;
		case STATE_DATA:
			buf=conn->send_msg->data;
			len=conn->send_msg->hdr.datalen;
			break;
		}
		
		count=write(conn->fd, buf + conn->send_offset,
					len - conn->send_offset);
		if (count == 0 || (count == -1 && errno == EAGAIN)) {
			break;
		} else if (count == -1 && errno == EINTR) {
			continue;
		} else if (count == -1) {
			conn_kill(conn);
			break;
		}
		conn->send_offset += count;
		if (conn->send_offset == len) {
			switch (conn->send_state) {
			case STATE_HEADER:
				conn->send_state=STATE_DATA;
				conn->send_offset=0;
				break;
			case STATE_DATA:
				conn->send_state=STATE_HEADER;
				mrpc_free_message(conn->send_msg);
				conn->send_msg=NULL;
				break;
			}
		}
	}
}

/* XXX signal handling */
/* XXX need provisions for connection timeout */
static void *listener(void *data)
{
	struct mrpc_conn_set *set=data;
	struct epoll_event events[set->expected_fds];
	int count;
	int i;
	
	while (1) {
		count=epoll_wait(set->epoll_fd, events, set->expected_fds, -1);
		for (i=0; i<count; i++) {
			if (events[i].data.ptr == set)
				return NULL;
			if (events[i].events & (EPOLLERR | EPOLLHUP)) {
				conn_kill(events[i].data.ptr);
				continue;
			}
			if (events[i].events & EPOLLOUT)
				try_write_conn(events[i].data.ptr);
			if (events[i].events & EPOLLIN)
				try_read_conn(events[i].data.ptr);
		}
	}
}

int send_message(struct mrpc_message *msg)
{
	struct mrpc_connection *conn=msg->conn;
	int ret;
	
	pthread_mutex_lock(&conn->send_msgs_lock);
	/* XXX extra syscall even when we don't need it */
	ret=need_writable(conn, 1);
	if (ret) {
		pthread_mutex_unlock(&conn->send_msgs_lock);
		mrpc_free_message(msg);  /* XXX?? */
		return ret;
	}
	list_add_tail(&msg->lh_msgs, &conn->send_msgs);
	pthread_mutex_unlock(&conn->send_msgs_lock);
	return 0;
}

int mrpc_conn_set_alloc(struct mrpc_conn_set **new_set,
			const struct mrpc_protocol *protocol, int expected_fds,
			unsigned conn_buckets, unsigned msg_buckets,
			unsigned msg_max_buf_len)
{
	struct mrpc_conn_set *set;
	struct epoll_event event;
	int ret=-ENOMEM;
	
	set=malloc(sizeof(*set));
	if (set == NULL)
		goto bad_alloc;
	memset(set, 0, sizeof(*set));
	pthread_mutex_init(&set->conns_lock, NULL);
	pthread_mutex_init(&set->events_lock, NULL);
	pthread_cond_init(&set->events_threads_cond, NULL);
	INIT_LIST_HEAD(&set->events);
	set->conns=hash_alloc(conn_buckets, conn_hash);
	if (set->conns == NULL)
		goto bad_conns;
	set->protocol=protocol;
	set->maxbuf=msg_max_buf_len;
	set->expected_fds=expected_fds;
	set->msg_buckets=msg_buckets;
	if (pipe(set->shutdown_pipe)) {
		ret=-errno;
		goto bad_shutdown_pipe;
	}
	if (pipe(set->events_notify_pipe)) {
		ret=-errno;
		goto bad_events_pipe;
	}
	if (set_nonblock(set->events_notify_pipe[0]))
		goto bad_nonblock;
	if (set_nonblock(set->events_notify_pipe[1]))
		goto bad_nonblock;
	set->epoll_fd=epoll_create(expected_fds);
	if (set->epoll_fd < 0) {
		ret=-errno;
		goto bad_epoll;
	}
	event.events=EPOLLIN;
	event.data.ptr=set;
	if (epoll_ctl(set->epoll_fd, EPOLL_CTL_ADD, set->shutdown_pipe[0],
				&event)) {
		ret=-errno;
		goto bad_epoll_pipe;
	}
	ret=pthread_create(&set->thread, NULL, listener, set);
	if (ret) {
		ret=-ret;
		goto bad_pthread;
	}
	*new_set=set;
	return 0;

bad_pthread:
bad_epoll_pipe:
	close(set->epoll_fd);
bad_epoll:
bad_nonblock:
	close(set->events_notify_pipe[0]);
	close(set->events_notify_pipe[1]);
bad_events_pipe:
	close(set->shutdown_pipe[0]);
	close(set->shutdown_pipe[1]);
bad_shutdown_pipe:
	hash_free(set->conns);
bad_conns:
	free(set);
bad_alloc:
	return ret;
}

/* XXX drops lots of stuff on the floor */
void mrpc_conn_set_free(struct mrpc_conn_set *set)
{
	write(set->shutdown_pipe[1], "s", 1);
	pthread_mutex_lock(&set->events_lock);
	while (set->event_queue_threads)
		pthread_cond_wait(&set->events_threads_cond, &set->events_lock);
	pthread_mutex_unlock(&set->events_lock);
	pthread_join(set->thread, NULL);
	close(set->epoll_fd);
	hash_free(set->conns);
	free(set);
}