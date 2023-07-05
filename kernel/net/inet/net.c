#include <net/sock.h>
#include <net/inet/net.h>
#include <net/inet/ethernet.h>
#include <net/inet/arp.h>
#include <net/inet/ip.h>
#include <net/inet/icmp.h>
#include <net/inet/udp.h>
#include <net/inet/tcp.h>
#include <net/if.h>
#include <proc/sched.h>
#include <stdio.h>

/* network devices */
struct net_device_t net_devices[NR_NET_DEVICES];
int nr_net_devices = 0;

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
				case IP_PROTO_UDP:
					udp_receive(skb);
					break;
				case IP_PROTO_TCP:
					tcp_receive(skb);
					break;
				case IP_PROTO_ICMP:
					icmp_receive(skb);

					/* handle ICMP requests */
					if (skb->h.icmp_header->type == ICMP_TYPE_ECHO) {
						icmp_reply_echo(skb);
						return;
					}

					break;
				default:
					break;
			}

			/* deliver message to sockets */
			net_deliver_skb(skb);

			break;
		default:
			break;
	}
}

/*
 * Network handler thread.
 */
static void net_handler_thread(void *arg)
{
	struct net_device_t *net_dev = (struct net_device_t *) arg;
	struct list_head_t *pos1, *n1, *pos2, *n2;
	struct sk_buff_t *skb;
	uint32_t flags;
	int ret;

	for (;;) {
		/* disable interrupts */
		irq_save(flags);

		/* handle incoming packets */
		list_for_each_safe(pos1, n1, &net_dev->skb_input_list) {
			/* get packet */
			skb = list_entry(pos1, struct sk_buff_t, list);
			list_del(&skb->list);

			/* handle packet */
			skb_handle(skb);

			/* free packet */
			skb_free(skb);
		}

		/* handle outcoming packets */
		list_for_each_safe(pos2, n2, &net_dev->skb_output_list) {
			/* get packet */
			skb = list_entry(pos2, struct sk_buff_t, list);

			/* rebuild ethernet header */
			ret = ethernet_rebuild_header(net_dev, skb);

			/* send packet and remove it from list */
			if (ret == 0) {
				list_del(&skb->list);
				net_dev->send_packet(skb);
				skb_free(skb);
			}
		}

		/* wait for incoming packets */
		current_task->timeout = jiffies + ms_to_jiffies(NET_HANDLE_FREQ_MS);
		task_sleep(&net_dev->wait);
		current_task->timeout = 0;

		/* enable interrupts */
		irq_restore(flags);
	}
}

/*
 * Register a network device.
 */
struct net_device_t *register_net_device(uint32_t io_base)
{
	struct net_device_t *net_dev;
	char tmp[32];
	size_t len;

	/* network devices table full */
	if (nr_net_devices >= NR_NET_DEVICES)
		return NULL;

	/* set net device */
	net_dev = &net_devices[nr_net_devices];
	net_dev->index = nr_net_devices;
	net_dev->io_base = io_base;
	net_dev->wait = NULL;
	INIT_LIST_HEAD(&net_dev->skb_input_list);
	INIT_LIST_HEAD(&net_dev->skb_output_list);

	/* set name */
	len = sprintf(tmp, "eth%d", nr_net_devices);
	
	/* allocate name */
	net_dev->name = (char *) kmalloc(len + 1);
	if (!net_dev->name)
		return NULL;

	/* set name */
	memcpy(net_dev->name, tmp, len + 1);

	/* create kernel thread to handle receive packets */
	net_dev->thread = create_kernel_thread(net_handler_thread, net_dev);
	if (!net_dev->thread) {
		kfree(net_dev->name);
		return NULL;
	}

	/* update number of net devices */
	nr_net_devices++;

	return net_dev;
}

/*
 * Find a network device.
 */
struct net_device_t *net_device_find(const char *name)
{
	int i;

	if (!name)
		return NULL;

	for (i = 0; i < nr_net_devices; i++)
		if (strcmp(net_devices[i].name, name) == 0)
			return &net_devices[i];

	return NULL;
}

/*
 * Get network devices configuration.
 */
int net_device_ifconf(struct ifconf *ifc)
{
	size_t size = ifc->ifc_len;
	struct ifreq ifr;
	int i, done = 0;

	/* for each network device */
	for (i = 0; i < nr_net_devices; i++) {
		if (size < sizeof(struct ifreq))
			break;

		/* reset interface */
		memset(&ifr, 0, sizeof(struct ifreq));

		/* set name */
		strcpy(ifr.ifr_ifrn.ifrn_name, net_devices[i].name);

		/* set address */
		(*(struct sockaddr_in *) &ifr.ifr_ifru.ifru_addr).sin_family = AF_INET;
		(*(struct sockaddr_in *) &ifr.ifr_ifru.ifru_addr).sin_addr = inet_iton(net_devices[i].ip_addr);

		/* update length */
		size -= sizeof(struct ifreq);
		done += sizeof(struct ifreq);
	}

	/* set length */
	ifc->ifc_len = done;

	return 0;
}

/*
 * Handle network packet (put it in device queue).
 */
void net_handle(struct net_device_t *net_dev, struct sk_buff_t *skb)
{
	if (!skb)
		return;

	/* put socket buffer in net device list */
	list_add_tail(&skb->list, &net_dev->skb_input_list);

	/* wake up handler */
	task_wakeup_all(&net_dev->wait);
}

/*
 * Transmit a network packet (put it in device queue).
 */
void net_transmit(struct net_device_t *net_dev, struct sk_buff_t *skb)
{
	if (!skb)
		return;

	/* put socket buffer in net device list */
	list_add_tail(&skb->list, &net_dev->skb_output_list);

	/* wake up handler */
	task_wakeup_all(&net_dev->wait);
}