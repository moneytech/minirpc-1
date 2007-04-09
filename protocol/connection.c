#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#define LIBPROTOCOL
#include "internal.h"

#define POLLEVENTS (EPOLLIN|EPOLLERR|EPOLLHUP)

struct queued_message {
	struct list_head lh_msgs;
	struct ISRMessage *msg;	
};

static unsigned conn_hash(struct list_head *entry, unsigned buckets)
{
	struct isr_connection *conn=list_entry(entry, struct isr_connection,
				lh_conns);
	return conn->fd % buckets;
}

static int conn_match(struct list_head *entry, void *data)
{
	struct isr_connection *conn=list_entry(entry, struct isr_connection,
				lh_conns);
	int *fd=data;
	return (*fd == conn->fd);
}

static struct isr_connection *conn_lookup(struct isr_conn_set *set, int fd)
{
	struct list_head *head;
	
	head=hash_get(set->conns, conn_match, fd, &fd);
	if (head == NULL)
		return NULL;
	return list_entry(head, struct isr_connection, lh_conns);
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

int add_conn(struct isr_connection **new_conn, struct isr_conn_set *set,
			int fd, void *data)
{
	struct isr_connection *conn;
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
	pthread_mutex_init(&conn->send_msgs_lock);
	pthread_mutex_init(&conn->pending_replies_lock, NULL);
	conn->set=set;
	conn->fd=fd;
	conn->data=data;
	conn->send_buf=malloc(set->buflen);
	if (conn->send_buf == NULL) {
		free(conn);
		return -ENOMEM;
	}
	conn->recv_buf=malloc(set->buflen);
	if (conn->recv_buf == NULL) {
		free(conn->send_buf);
		free(conn);
		return -ENOMEM;
	}
	conn->pending_replies=hash_alloc(set->msg_buckets, conn_hash);
	if (conn->pending_replies == NULL) {
		free(conn->recv_buf);
		free(conn->send_buf);
		free(conn);
		return -ENOMEM;
	}
	event.events=POLLEVENTS;
	event.data.ptr=conn;
	if (epoll_ctl(set->epoll_fd, EPOLL_CTL_ADD, fd, &event)) {
		ret=-errno;
		hash_free(conn->pending_replies);
		free(conn->recv_buf);
		free(conn->send_buf);
		free(conn);
		return ret;
	}
	pthread_mutex_lock(&set->lock);
	hash_add(set->conns, &conn->lh_conns);
	pthread_mutex_unlock(&set->lock);
	*new_conn=conn;
	return 0;
}

void remove_conn(struct isr_connection *conn)
{
	struct isr_conn_set *set=conn->set;
	
	/* XXX data already in buffer? */
	pthread_mutex_lock(&set->lock);
	hash_remove(set->conns, &conn->lh_conns);
	pthread_mutex_unlock(&set->lock);
	hash_free(conn->pending_replies);
	free(conn->recv_buf);
	free(conn->send_buf);
	free(conn);
}

static int need_writable(struct isr_connection *conn, int writable)
{
	struct epoll_event event;
	
	event.data.ptr=conn;
	event.events=POLLEVENTS;
	if (writable)
		event.events |= EPOLLOUT;
	return epoll_ctl(conn->set->epoll_fd, EPOLL_CTL_MOD, conn->fd, &event);
}

static void conn_kill(struct isr_connection *conn)
{
	/* XXX */
}

static int process_buffer(struct isr_connection *conn, unsigned *start)
{
	asn_dec_rval_t rval;
	int ret=0;
	
	rval=ber_decode(NULL, &asn_DEF_ISRMessage, (void**)&conn->recv_msg,
				conn->recv_buf + *start,
				conn->recv_offset - *start);
	switch (rval.code) {
	case RC_OK:
		if (asn_check_constraints(&asn_DEF_ISRMessage, conn->recv_msg,
					NULL, NULL)) {
			free_message(conn->recv_msg);
			conn->recv_msg=NULL;
			ret=-EINVAL;
			break;
		}
		process_incoming_message(conn->recv_msg);
		conn->recv_msg=NULL;
		break;
	case RC_WMORE:
		ret=-EAGAIN;
		break;
	case RC_FAIL:
		free_message(conn->recv_msg);
		conn->recv_msg=NULL;
		ret=-EINVAL;
		break;
	}
	*start += rval.consumed;
	return ret;
}

static void try_read_conn(struct isr_connection *conn)
{
	ssize_t count;
	unsigned start;
	int ret;
	
	while (1) {
		count=read(conn->fd, conn->recv_buf + conn->recv_offset,
					conn->set->buflen - conn->recv_offset);
		if (count == -1 && errno == EINTR) {
			continue;
		} else if (count == -1 && errno == EAGAIN) {
			return;
		} else if (count == 0 || count == -1) {
			conn_kill(conn);
			return;
		}
		conn->recv_offset += count;
		
		start=0;
		while (1) {
			ret=process_buffer(conn, &start);
			if (ret == -EINVAL) {
				conn_kill(conn);
				return;
			}
			if (ret == -EAGAIN) {
				memmove(conn->recv_buf, conn->recv_buf + start,
						conn->recv_offset - start);
				conn->recv_offset -= start;
				break;
			}
		}
		
		if (conn->recv_offset == conn->set->buflen) {
			conn_kill(conn);
			break;
		}
	}
}

static int form_buffer(struct isr_connection *conn)
{
	asn_enc_rval_t rval;
	struct queued_message *queued;
	
	pthread_mutex_lock(&conn->send_msgs_lock);
	if (list_is_empty(&conn->send_msgs)) {
		need_writable(conn, 0);
		pthread_mutex_unlock(&conn->send_msgs_lock);
		return -EAGAIN;
	}
	queued=list_entry(conn->send_msgs.next, struct queued_message, lh_msgs);
	list_del_init(&queued->lh_msgs);
	pthread_mutex_unlock(&conn->send_msgs_lock);
	
	rval=der_encode_to_buffer(&asn_DEF_ISRMessage, queued->msg,
				conn->recv_buf, conn->set->buflen);
	if (rval.encoded == -1)
		return -EINVAL;
	conn->send_offset=0;
	conn->send_length=rval.encoded;
	return 0;
}

static void try_write_conn(struct isr_connection *conn)
{
	ssize_t count;
	int ret;
	
	while (1) {
		if (conn->send_offset == conn->send_length) {
			ret=form_buffer(conn);
			if (ret == -EAGAIN)
				break;
			else if (ret == -EINVAL) {
				conn_kill(conn);
				break;
			}
		}
		
		count=write(conn->fd, conn->send_buf + conn->send_offset,
					conn->send_length - conn->send_offset);
		if (count == 0 || (count == -1 && errno == EAGAIN)) {
			break;
		} else if (count == -1 && errno == EINTR) {
			continue;
		} else if (count == -1) {
			conn_kill(conn);
			break;
		}
		conn->send_offset += count;
	}
}

void listener(struct isr_conn_set *set, int maxevents)
{
	struct epoll_event events[maxevents];
	int count;
	int i;
	
	while (1) {
		count=epoll_wait(set->epoll_fd, events, maxevents, -1);
		for (i=0; i<count; i++) {
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

int send_message(struct isr_connection *conn, struct ISRMessage *msg)
{
	struct queued_message *queued;
	int ret;
	
	queued=malloc(sizeof(*queued));
	if (queued == NULL)
		return -ENOMEM;
	INIT_LIST_HEAD(&queued->lh_msgs);
	queued->msg=msg;
	pthread_mutex_lock(&conn->send_msgs_lock);
	/* XXX extra syscall even when we don't need it */
	ret=need_writable(conn, 1);
	if (ret) {
		free(queued);
		pthread_mutex_unlock(&conn->send_msgs_lock);
		return ret;
	}
	list_add_tail(&queued->lh_msgs, &conn->send_msgs);
	pthread_mutex_unlock(&conn->send_msgs_lock);
	return 0;
}

int set_alloc(struct isr_conn_set **new_set, int fds, unsigned conn_buckets,
			unsigned msg_buckets, unsigned buflen, int is_server,
			new_request_fn *func)
{
	struct isr_conn_set *set;
	
	set=malloc(sizeof(*set));
	if (set == NULL)
		return -ENOMEM;
	pthread_mutex_init(&set->lock, NULL);
	set->conns=hash_alloc(conn_buckets, conn_hash);
	if (set->conns == NULL) {
		free(set);
		return -ENOMEM;
	}
	set->buflen=buflen;
	set->is_server=is_server;
	set->request_fn=func;
	set->msg_buckets=msg_buckets;
	set->epoll_fd=epoll_create(fds);
	if (set->epoll_fd < 0) {
		hash_free(set->conns);
		free(set);
		return -errno;
	}
	*new_set=set;
	return 0;
}

void set_free(struct isr_conn_set *set)
{
	close(set->epoll_fd);
	hash_free(set->conns);
	free(set);
}