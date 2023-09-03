#ifndef _ICMP_H_
#define _ICMP_H_

#include <net/sk_buff.h>

#define ICMP_TYPE_ECHO		0x08

/*
 * ICMP header.
 */
struct icmp_header {
	uint8_t		type;
	uint8_t		code;
	uint16_t	chksum;
	union {
		struct {
			uint16_t	id;
			uint16_t	sequence;
		} echo;
		uint32_t gateway;
	} un;
};

void icmp_receive(struct sk_buff *skb);
void icmp_reply_echo(struct sk_buff *skb);

#endif
