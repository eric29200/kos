#include <net/net.h>
#include <net/socket.h>
#include <net/ethernet.h>
#include <net/arp.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <proc/sched.h>
#include <string.h>
#include <list.h>

/* network devices */
static struct net_device_t net_devices[NR_NET_DEVICES];
static int nb_net_devices = 0;

/* sockets (defined in socket.c) */
extern struct socket_t sockets[NR_SOCKETS];

/*
 * Register a newtork device.
 */
struct net_device_t *register_net_device(uint32_t io_base)
{
  struct net_device_t *net_dev;

  /* network devices table full */
  if (nb_net_devices >= NR_NET_DEVICES)
    return NULL;

  /* set net device */
  net_dev = &net_devices[nb_net_devices++];
  net_dev->io_base = io_base;

  return net_dev;
}

/*
 * Compute checksum.
 */
uint16_t net_checksum(void *data, size_t size)
{
  uint16_t *chunk, ret;
  uint32_t chksum;

  for (chksum = 0, chunk = (uint16_t *) data; size > 1; size -= 2)
    chksum += *chunk++;

  if (size == 1)
    chksum += *((uint8_t *) chunk);

  chksum = (chksum & 0xFFFF) + (chksum >> 16);
  chksum += (chksum >> 16);
  ret = ~chksum;

  return ret;
}

/*
 * Deliver a packet to sockets.
 */
static void skb_deliver_to_sockets(struct sk_buff_t *skb)
{
  struct sk_buff_t *skb_new;
  int i;

  /* find matching sockets */
  for (i = 0; i < NR_SOCKETS; i++) {
    /* unused socket */
    if (sockets[i].state == SS_FREE)
      continue;

    /* check protocol */
    if (sockets[i].protocol != skb->nh.ip_header->protocol)
      continue;

    /* clone socket buffer */
    skb_new = skb_clone(skb);
    if (!skb_new)
      break;

    /* push skb in socket queue */
    list_add_tail(&skb_new->list, &sockets[i].skb_list);

    /* wake up socket */
    task_wakeup_all(&sockets[i].waiting_chan);
  }
}

/*
 * Handle a socket buffer.
 */
void skb_handle(struct sk_buff_t *skb)
{
  /* decode ethernet header */
  ethernet_receive(skb);

  /* handle packet */
  switch(htons(skb->eth_header->type)) {
    case ETHERNET_TYPE_ARP:
      /* decode ARP header */
      arp_receive(skb);

      /* reply to ARP request or add arp table entry */
      if (ntohs(skb->nh.arp_header->opcode) == ARP_REQUEST)
        arp_reply_request(skb);
      else if (ntohs(skb->nh.arp_header->opcode) == ARP_REPLY)
        arp_add_table(skb->nh.arp_header);

      break;
    case ETHERNET_TYPE_IP:
      /* decode IP header */
      ip_receive(skb);

      /* handle IPv4 only */
      if (skb->nh.ip_header->version != 4)
        break;

      /* check if packet is adressed to us */
      if (memcmp(skb->dev->ip_addr, skb->nh.ip_header->dst_addr, 4) != 0)
        break;

      /* go to next layer */
      switch (skb->nh.ip_header->protocol) {
        case IP_PROTO_ICMP:
          icmp_receive(skb);

          /* handle ICMP requests */
          if (skb->h.icmp_header->type == ICMP_TYPE_ECHO)
            icmp_reply_echo(skb);

          break;
        default:
          break;
      }

      /* deliver message to sockets */
      skb_deliver_to_sockets(skb);

      break;
    default:
      break;
  }
}
