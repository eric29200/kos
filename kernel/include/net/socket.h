#ifndef _SOCKET_H_
#define _SOCKET_H_

#include <proc/wait.h>
#include <stddef.h>

#define NR_SOCKETS		32

/* addresses families */
#define AF_UNIX		 	1
#define AF_INET			2

/* protocol families */
#define PF_UNIX			1
#define PF_INET			2

/* socket types */
#define SOCK_STREAM		1
#define SOCK_DGRAM		2
#define SOCK_RAW		3

/* flags for send/recv */
#define MSG_OOB			1
#define MSG_PEEK		2

/* flags for shutdown */
#define RCV_SHUTDOWN		1
#define SEND_SHUTDOWN		2
#define SHUTDOWN_MASK		3

/* socket options */
#define SOL_SOCKET		1
#define SO_DEBUG		1
#define SO_REUSEADDR		2
#define SO_TYPE			3
#define SO_ERROR		4
#define SO_DONTROUTE		5
#define SO_BROADCAST		6
#define SO_SNDBUF		7
#define SO_RCVBUF		8
#define SO_SNDBUFFORCE		32
#define SO_RCVBUFFORCE		33
#define SO_KEEPALIVE		9
#define SO_OOBINLINE		10
#define SO_NO_CHECK		11
#define SO_PRIORITY		12
#define SO_LINGER		13
#define SO_BSDCOMPAT		14
#define SO_PASSCRED		16
#define SO_PEERCRED		17
#define SO_RCVLOWAT		18
#define SO_SNDLOWAT		19
#define SO_RCVTIMEO		20
#define SO_SNDTIMEO		21

/*
 * Socket address.
 */
struct sockaddr {
	uint16_t		sa_family;
	char			sa_data[14];
};

/*
 * Message header.
 */
struct msghdr_t {
	void *			msg_name;
	size_t			msg_namelen;
	struct iovec_t *	msg_iov;
	size_t			msg_iovlen;
	void *			msg_control;
	size_t			msg_controllen;
	int			msg_flags;
};

/*
 * Socket state.
 */
typedef enum {
	SS_FREE = 0,
	SS_UNCONNECTED,
	SS_LISTENING,
	SS_CONNECTING,
	SS_CONNECTED,
	SS_DISCONNECTING,
	SS_DEAD
} socket_state_t;

/*
 * Socket structure.
 */
struct socket_t {
	uint16_t		family;
	uint16_t		type;
	socket_state_t		state;
	struct prot_ops *	ops;
	struct wait_queue_t *	wait;
	struct inode_t *	inode;
	void *			data;
};

/*
 * Protocol operations.
 */
struct prot_ops {
	int (*create)(struct socket_t *, int);
	int (*dup)(struct socket_t *, struct socket_t *);
	int (*release)(struct socket_t *);
	int (*poll)(struct socket_t *, struct select_table_t *);
	int (*recvmsg)(struct socket_t *, struct msghdr_t *, int, int);
	int (*sendmsg)(struct socket_t *, const struct msghdr_t *, int, int);
	int (*bind)(struct socket_t *, const struct sockaddr *, size_t);
	int (*accept)(struct socket_t *, struct socket_t *, struct sockaddr *);
	int (*connect)(struct socket_t *, const struct sockaddr *, size_t);
	int (*shutdown)(struct socket_t *, int);
	int (*getpeername)(struct socket_t *, struct sockaddr *, size_t *);
	int (*getsockname)(struct socket_t *, struct sockaddr *, size_t *);
	int (*getsockopt)(struct socket_t *, int, int, void *, size_t);
	int (*setsockopt)(struct socket_t *, int, int, void *, size_t);
};

/* protocole operations */
extern struct prot_ops inet_ops;
extern struct prot_ops unix_ops;

/* socket system calls */
int sys_socket(int domain, int type, int protocol);
int sys_bind(int sockfd, const struct sockaddr *addr, size_t addrlen);
int sys_connect(int sockfd, const struct sockaddr *addr, size_t addrlen);
int sys_listen(int sockfd, int backlog);
int sys_accept(int sockfd, struct sockaddr *addr, size_t addrlen);
int sys_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, size_t addrlen);
int sys_sendmsg(int sockfd, const struct msghdr_t *msg, int flags);
int sys_recvfrom(int sockfd, const void *buf, size_t len, int flags, struct sockaddr *src_addr, size_t addrlen);
int sys_recvmsg(int sockfd, struct msghdr_t *msg, int flags);
int sys_shutdown(int sockfd, int how);
int sys_getpeername(int sockfd, struct sockaddr *addr, size_t *addrlen);
int sys_getsockname(int sockfd, struct sockaddr *addr, size_t *addrlen);
int sys_getsockopt(int sockfd, int level, int optname, void *optval, size_t optlen);
int sys_setsockopt(int sockfd, int level, int optname, void *optval, size_t optlen);

#endif
