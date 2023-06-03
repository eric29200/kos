#include <net/socket.h>
#include <fs/fs.h>
#include <proc/sched.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stderr.h>
#include <string.h>
#include <fcntl.h>
#include <uio.h>

/* sockets table */
struct socket_t sockets[NR_SOCKETS];

/* socket file operations */
struct file_operations_t socket_fops;

/*
 * Allocate a socket.
 */
static struct socket_t *sock_alloc()
{
	int i;

	/* find a free socket */
	for (i = 0; i < NR_SOCKETS; i++)
		if (sockets[i].state == SS_FREE)
			break;

	/* no free sockets */
	if (i >= NR_SOCKETS)
		return NULL;

	/* reset socket */
	memset(&sockets[i], 0, sizeof(struct socket_t));

	/* get an empty inode */
	sockets[i].inode = get_empty_inode(NULL);
	if (!sockets[i].inode) {
		sockets[i].state = SS_FREE;
		return NULL;
	}

	return &sockets[i];
}

/*
 * Release a socket.
 */
static void sock_release(struct socket_t *sock)
{
	/* release socket */
	if (sock->ops && sock->ops->release)
		sock->ops->release(sock);

	/* mark socket free */
	sock->state = SS_FREE;
}

/*
 * Create a socket.
 */
static int sock_create(int domain, int type)
{
	struct prot_ops *sock_ops;
	struct socket_t *sock;
	struct file_t *filp;
	int fd;

	/* only internet sockets */
	switch (domain) {
		case AF_INET:
			sock_ops = &inet_ops;
			break;
		case AF_UNIX:
			sock_ops = &unix_ops;
			break;
		default:
			return -EINVAL;
	}

	/* allocate a socket */
	sock = sock_alloc();
	if (!sock)
		return -EMFILE;

	/* set socket */
	sock->state = SS_UNCONNECTED;
	sock->family = domain;
	sock->type = type;
	sock->ops = sock_ops;

	/* get a new empty file */
	filp = get_empty_filp();
	if (!filp) {
		sock_release(sock);
		return -EMFILE;
	}

	/* find a free file slot */
	for (fd = 0; fd < NR_FILE; fd++)
		if (!current_task->files->filp[fd])
			break;

	/* no free slot */
	if (fd >= NR_FILE) {
		filp->f_ref = 0;
		sock_release(sock);
		return -EMFILE;
	}

	/* set file */
	current_task->files->filp[fd] = filp;
	FD_CLR(fd, &current_task->files->close_on_exec);
	current_task->files->filp[fd]->f_mode = O_RDWR;
	current_task->files->filp[fd]->f_flags = 0;
	current_task->files->filp[fd]->f_pos = 0;
	current_task->files->filp[fd]->f_ref = 1;
	current_task->files->filp[fd]->f_inode = sock->inode;
	current_task->files->filp[fd]->f_op = &socket_fops;

	return fd;
}

/*
 * Find socket on an inode.
 */
static struct socket_t *sock_lookup(struct inode_t *inode)
{
	int i;

	if (!inode)
		return NULL;

	for (i = 0; i < NR_SOCKETS; i++)
		if (sockets[i].inode == inode)
			return &sockets[i];

	return NULL;
}

/*
 * Find socket on a file descriptor.
 */
static struct socket_t *sockfd_lookup(int fd)
{
	if (fd < 0 || fd >= NR_FILE || !current_task->files->filp[fd])
		return NULL;

	return sock_lookup(current_task->files->filp[fd]->f_inode);
}

/*
 * Close a socket.
 */
static int sock_close(struct file_t *filp)
{
	struct socket_t *sock;
	int ret = 0;

	/* get socket */
	sock = sock_lookup(filp->f_inode);
	if (!sock)
		return -EINVAL;

	/* close protocol operation */
	if (sock->ops && sock->ops->close)
		ret = sock->ops->close(sock);

	/* free socket */
	sock_release(sock);

	return ret;
}

/*
 * Poll on a socket.
 */
static int sock_poll(struct file_t *filp, struct select_table_t *wait)
{
	struct socket_t *sock;
	int mask = 0;

	/* get socket */
	sock = sock_lookup(filp->f_inode);
	if (!sock)
		return -EINVAL;

	/* check if there is a message in the queue */
	if (sock->ops && sock->ops->poll)
		mask = sock->ops->poll(sock, wait);

	return mask;
}

/*
 * Socket read.
 */
static int sock_read(struct file_t *filp, char *buf, int len)
{
	struct socket_t *sock;
	struct msghdr_t msg;
	struct iovec_t iov;

	/* get socket */
	sock = sock_lookup(filp->f_inode);
	if (!sock)
		return -EINVAL;

	/* receive message not implemented */
	if (!sock->ops || !sock->ops->recvmsg)
		return -EINVAL;

	/* build message */
	memset(&msg, 0, sizeof(struct msghdr_t));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	iov.iov_base = buf;
	iov.iov_len = len;

	return sock->ops->recvmsg(sock, &msg, filp->f_flags & O_NONBLOCK, 0);
}

/*
 * Socket write.
 */
static int sock_write(struct file_t *filp, const char *buf, int len)
{
	struct socket_t *sock;
	struct msghdr_t msg;
	struct iovec_t iov;

	/* get socket */
	sock = sock_lookup(filp->f_inode);
	if (!sock)
		return -EINVAL;

	/* send message not implemented */
	if (!sock->ops || !sock->ops->sendmsg)
		return -EINVAL;

	/* build message */
	memset(&msg, 0, sizeof(struct msghdr_t));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	iov.iov_base = (char *) buf;
	iov.iov_len = len;

	return sock->ops->sendmsg(sock, &msg, filp->f_flags & O_NONBLOCK, 0);
}

/*
 * Socket file operations.
 */
struct file_operations_t socket_fops = {
	.read		= sock_read,
	.write		= sock_write,
	.poll		= sock_poll,
	.close		= sock_close,
};

/*
 * Create a socket.
 */
int do_socket(int domain, int type, int protocol)
{
	struct socket_t *sock;
	int sockfd, ret;

	/* create a new socket */
	sockfd = sock_create(domain, type);
	if (sockfd < 0)
		return sockfd;

	/* check socket file descriptor */
	if (sockfd < 0 || sockfd >= NR_OPEN || current_task->files->filp[sockfd] == NULL)
		return -EBADF;

	/* find socket */
	sock = sock_lookup(current_task->files->filp[sockfd]->f_inode);
	if (!sock)
		return -EINVAL;

	/* create not implemented */
	if (!sock->ops || !sock->ops->create) {
		sock_release(sock);
		return -EINVAL;
	}

	/* create socket */
	ret = sock->ops->create(sock, protocol);
	if (ret) {
		sock_release(sock);
		return ret;
	}

	return sockfd;
}

/*
 * Bind system call (attach an address to a socket).
 */
int do_bind(int sockfd, const struct sockaddr *addr, size_t addrlen)
{
	struct socket_t *sock;

	/* check socket file descriptor */
	if (sockfd < 0 || sockfd >= NR_OPEN || current_task->files->filp[sockfd] == NULL)
		return -EBADF;

	/* find socket */
	sock = sock_lookup(current_task->files->filp[sockfd]->f_inode);
	if (!sock)
		return -EINVAL;

	/* bind not implemented */
	if (!sock->ops || !sock->ops->bind)
		return -EINVAL;

	return sock->ops->bind(sock, addr, addrlen);
}

/*
 * Connect system call.
 */
int do_connect(int sockfd, const struct sockaddr *addr, size_t addrlen)
{
	struct socket_t *sock;

	/* check socket file descriptor */
	if (sockfd < 0 || sockfd >= NR_OPEN || current_task->files->filp[sockfd] == NULL)
		return -EBADF;

	/* find socket */
	sock = sock_lookup(current_task->files->filp[sockfd]->f_inode);
	if (!sock)
		return -EINVAL;

	/* connect not implemented */
	if (!sock->ops || !sock->ops->connect)
		return -EINVAL;

	return sock->ops->connect(sock, addr, addrlen);
}

/*
 * Listen system call.
 */
int do_listen(int sockfd, int backlog)
{
	struct socket_t *sock;

	/* unused backlog */
	UNUSED(backlog);

	/* check socket file descriptor */
	if (sockfd < 0 || sockfd >= NR_OPEN || current_task->files->filp[sockfd] == NULL)
		return -EBADF;

	/* find socket */
	sock = sock_lookup(current_task->files->filp[sockfd]->f_inode);
	if (!sock)
		return -EINVAL;

	/* update socket state */
	sock->state = SS_LISTENING;

	return 0;
}

/*
 * Accept system call.
 */
int do_accept(int sockfd, struct sockaddr *addr, size_t addrlen)
{
	struct socket_t *sock, *new_sock;
	int new_sockfd, ret;

	/* unused address length */
	UNUSED(addrlen);

	/* check socket file descriptor */
	if (sockfd < 0 || sockfd >= NR_OPEN || current_task->files->filp[sockfd] == NULL)
		return -EBADF;

	/* find socket */
	sock = sock_lookup(current_task->files->filp[sockfd]->f_inode);
	if (!sock)
		return -EINVAL;

	/* create new socket */
	new_sockfd = sock_create(sock->family, sock->type);
	if (new_sockfd < 0)
		return new_sockfd;

	/* check socket file descriptor */
	if (new_sockfd < 0 || new_sockfd >= NR_OPEN || current_task->files->filp[new_sockfd] == NULL)
		return -EBADF;

	/* find socket */
	new_sock = sock_lookup(current_task->files->filp[new_sockfd]->f_inode);
	if (!new_sock)
		return -EINVAL;

	/* duplicate socket */
	ret = sock->ops->dup(sock, new_sock);
	if (ret) {
		sys_close(new_sockfd);
		return ret;
	}

	/* accept not implemented */
	if (!new_sock->ops || !new_sock->ops->accept) {
		sys_close(new_sockfd);
		return -EINVAL;
	}

	/* call accept protocol */
	ret = new_sock->ops->accept(sock, new_sock, addr);
	if (ret < 0) {
		sys_close(new_sockfd);
		return ret;
	}

	return new_sockfd;
}

/*
 * Send to system call.
 */
int do_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, size_t addrlen)
{
	struct socket_t *sock;
	struct iovec_t iovec;
	struct file_t *filp;
	struct msghdr_t msg;

	/* unused address length */
	UNUSED(addrlen);

	/* check socket file descriptor */
	if (sockfd < 0 || sockfd >= NR_OPEN || current_task->files->filp[sockfd] == NULL)
		return -EBADF;

	/* find socket */
	filp = current_task->files->filp[sockfd];
	sock = sock_lookup(filp->f_inode);
	if (!sock)
		return -EINVAL;

	/* send message not implemented */
	if (!sock->ops || !sock->ops->sendmsg)
		return -EINVAL;

	/* build buffer */
	iovec.iov_base = (void *) buf;
	iovec.iov_len = len;

	/* build message */
	msg.msg_name = (void *) dest_addr;
	msg.msg_namelen = sizeof(struct sockaddr);
	msg.msg_iov = &iovec;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	/* send message */
	return sock->ops->sendmsg(sock, &msg, filp->f_flags & O_NONBLOCK, flags);
}

/*
 * Receive from system call.
 */
int do_recvfrom(int sockfd, const void *buf, size_t len, int flags, struct sockaddr *src_addr, size_t addrlen)
{
	struct socket_t *sock;
	struct iovec_t iovec;
	struct file_t *filp;
	struct msghdr_t msg;

	/* unused address length */
	UNUSED(addrlen);

	/* check socket file descriptor */
	if (sockfd < 0 || sockfd >= NR_OPEN || current_task->files->filp[sockfd] == NULL)
		return -EBADF;

	/* find socket */
	filp = current_task->files->filp[sockfd];
	sock = sock_lookup(filp->f_inode);
	if (!sock)
		return -EINVAL;

	/* receive message not implemented */
	if (!sock->ops || !sock->ops->recvmsg)
		return -EINVAL;

	/* build buffer */
	iovec.iov_base = (void *) buf;
	iovec.iov_len = len;

	/* build message */
	msg.msg_name = (void *) src_addr;
	msg.msg_namelen = sizeof(struct sockaddr);
	msg.msg_iov = &iovec;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	return sock->ops->recvmsg(sock, &msg, filp->f_flags & O_NONBLOCK, flags);
}

/*
 * Receive a message system call.
 */
int do_recvmsg(int sockfd, struct msghdr_t *msg, int flags)
{
	struct socket_t *sock;
	struct file_t *filp;

	/* check socket file descriptor */
	if (sockfd < 0 || sockfd >= NR_OPEN || current_task->files->filp[sockfd] == NULL)
		return -EBADF;

	/* find socket */
	filp = current_task->files->filp[sockfd];
	sock = sock_lookup(filp->f_inode);
	if (!sock)
		return -EINVAL;

	/* receive message not implemented */
	if (!sock->ops || !sock->ops->recvmsg)
		return -EINVAL;

	return sock->ops->recvmsg(sock, msg, filp->f_flags & O_NONBLOCK, flags);
}

/*
 * Shutdown system call.
 */
int do_shutdown(int sockfd, int how)
{
	struct socket_t *sock;

	/* check socket file descriptor */
	if (sockfd < 0 || sockfd >= NR_OPEN || current_task->files->filp[sockfd] == NULL)
		return -EBADF;

	/* find socket */
	sock = sock_lookup(current_task->files->filp[sockfd]->f_inode);
	if (!sock)
		return -EINVAL;

	/* shutdown not implemented */
	if (!sock->ops || !sock->ops->shutdown)
		return -EINVAL;

	return sock->ops->shutdown(sock, how);
}

/*
 * Get peer name system call.
 */
int do_getpeername(int sockfd, struct sockaddr *addr, size_t *addrlen)
{
	struct socket_t *sock;

	/* check socket file descriptor */
	if (sockfd < 0 || sockfd >= NR_OPEN || current_task->files->filp[sockfd] == NULL)
		return -EBADF;

	/* find socket */
	sock = sock_lookup(current_task->files->filp[sockfd]->f_inode);
	if (!sock)
		return -EINVAL;

	/* getpeername not implemented */
	if (!sock->ops || !sock->ops->getpeername)
		return -EINVAL;

	return sock->ops->getpeername(sock, addr, addrlen);
}

/*
 * Get sock name system call.
 */
int do_getsockname(int sockfd, struct sockaddr *addr, size_t *addrlen)
{
	struct socket_t *sock;

	/* check socket file descriptor */
	if (sockfd < 0 || sockfd >= NR_OPEN || current_task->files->filp[sockfd] == NULL)
		return -EBADF;

	/* find socket */
	sock = sock_lookup(current_task->files->filp[sockfd]->f_inode);
	if (!sock)
		return -EINVAL;

	/* getsockname not implemented */
	if (!sock->ops || !sock->ops->getsockname)
		return -EINVAL;

	return sock->ops->getsockname(sock, addr, addrlen);
}

/*
 * Get socket options.
 */
static int sock_getsockopt(struct socket_t *sock, int optname, void *optval, size_t optlen)
{
	UNUSED(sock);
	UNUSED(optval);
	UNUSED(optlen);

	printf("sock_getsockopt(%d) undefined\n", optname);

	return 0;
}

/*
 * Set socket options.
 */
static int sock_setsockopt(struct socket_t *sock, int optname, void *optval, size_t optlen)
{
	UNUSED(sock);
	UNUSED(optval);
	UNUSED(optlen);

	switch (optname) {
		case SO_PASSCRED:
			break;
		default:
			printf("sock_setsockopt(%d) undefined\n", optname);
			break;
	}

	return 0;
}

/*
 * Get socket options system call.
 */
int do_getsockopt(int sockfd, int level, int optname, void *optval, size_t optlen)
{
	struct socket_t *sock;

	/* check socket file descriptor */
	if (sockfd < 0 || sockfd >= NR_OPEN || current_task->files->filp[sockfd] == NULL)
		return -EBADF;

	/* find socket */
	sock = sock_lookup(current_task->files->filp[sockfd]->f_inode);
	if (!sock)
		return -EINVAL;

	/* socket options */
	if (level == SOL_SOCKET)
		return sock_getsockopt(sock, optname, optval, optlen);

	/* setsockopt not implemented */
	if (!sock->ops || !sock->ops->getsockopt)
		return -EINVAL;

	return sock->ops->getsockopt(sock, level, optname, optval, optlen);
}

/*
 * Set socket options system call.
 */
int do_setsockopt(int sockfd, int level, int optname, void *optval, size_t optlen)
{
	struct socket_t *sock;

	/* check socket file descriptor */
	if (sockfd < 0 || sockfd >= NR_OPEN || current_task->files->filp[sockfd] == NULL)
		return -EBADF;

	/* find socket */
	sock = sock_lookup(current_task->files->filp[sockfd]->f_inode);
	if (!sock)
		return -EINVAL;

	/* socket options */
	if (level == SOL_SOCKET)
		return sock_setsockopt(sock, optname, optval, optlen);

	/* setsockopt not implemented */
	if (!sock->ops || !sock->ops->setsockopt)
		return -EINVAL;

	return sock->ops->setsockopt(sock, level, optname, optval, optlen);
}

/*
 * Create a pair of connected sockets.
 */
int do_socketpair(int domain, int type, int protocol, int sv[2])
{
	struct socket_t *sock1, *sock2;
	int fd1, fd2, ret;

	/* create first socket */
	fd1 = sys_socket(domain, type, protocol);
	if (fd1 < 0)
		return fd1;

	/* create second socket */
	fd2 = sys_socket(domain, type, protocol);
	if (fd2 < 0) {
		sys_close(fd1);
		return fd2;
	}

	/* get sockets */
	sock1 = sockfd_lookup(fd1);
	sock2 = sockfd_lookup(fd2);
	if (!sock1 || !sock2 || !sock1->ops->socketpair) {
		sys_close(fd1);
		sys_close(fd2);
		return -EINVAL;
	}

	/* connect sockets */
	ret = sock1->ops->socketpair(sock1, sock2);
	if (ret < 0) {
		sys_close(fd1);
		sys_close(fd2);
		return ret;
	}

	/* set output */
	sv[0] = fd1;
	sv[1] = fd2;

	return 0;
}