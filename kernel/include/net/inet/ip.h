#ifndef _IP_H_
#define _IP_H_

#include <net/sk_buff.h>

#define IP_PROTO_IP			0x00
#define IP_PROTO_ICMP			0x01
#define IP_PROTO_TCP			0x06
#define IP_PROTO_UDP			0x11

#define IP_START_DYN_PORT		49152

#define IPV4_DEFAULT_TTL		64

#define ip_version(ip_packet)		(((ip_packet)->version & 0xF0) >> 4)

/*
 * IPv4 header.
 */
struct ip_header {
	uint8_t		ihl:4;
	uint8_t		version:4;
	uint8_t		tos;
	uint16_t	length;
	uint16_t	id;
	uint16_t	fragment_offset;
	uint8_t		ttl;
	uint8_t		protocol;
	uint16_t	chksum;
	uint8_t		src_addr[4];
	uint8_t		dst_addr[4];
};

/*
 * Decode an IP address.
 */
static inline void inet_ntoi(uint32_t value, uint8_t *buf)
{
	int i;
	for (i = 0; i < 4; i++)
		buf[i] = ((uint8_t *) &value)[i];
}

/*
 * Encode an IP address.
 */
static inline uint32_t inet_iton(uint8_t *buf)
{
	return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
}

void ip_build_header(struct ip_header *ip_header, uint8_t tos, uint16_t length, uint16_t id,
		     uint8_t ttl, uint8_t protocol, uint8_t *src_addr, uint8_t *dst_addr);
void ip_receive(struct sk_buff *skb);
void ip_route(struct net_device *dev, const uint8_t *dest_ip, uint8_t *route_ip);

#endif
