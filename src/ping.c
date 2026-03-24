#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/icmp.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/printk.h>

static enum net_verdict icmp_echo_reply_handler(struct net_icmp_ctx *ctx,
						struct net_pkt *pkt,
						struct net_icmp_ip_hdr *ip_hdr,
						struct net_icmp_hdr *icmp_hdr,
						void *user_data)
{
	ARG_UNUSED(ctx);
	ARG_UNUSED(pkt);
	ARG_UNUSED(ip_hdr);
	ARG_UNUSED(icmp_hdr);

	if (user_data != NULL) {
		uint32_t sent_cycles = *(uint32_t *)user_data;
		uint32_t now_cycles = k_cycle_get_32();
		uint32_t delta_cycles = now_cycles - sent_cycles;

		printk("ICMP echo reply received, RTT cycles: %u\n", delta_cycles);
	} else {
		printk("ICMP echo reply received\n");
	}

	return NET_OK;
}

int ping(const char *ipv4_addr, uint8_t count)
{
	int ret;
	struct net_icmp_ctx icmp_context;
	struct net_if *iface;
	struct sockaddr_in dst_addr;

	iface = net_if_get_default();
	if (iface == NULL) {
		printk("No default network interface\n");
		return -ENODEV;
	}

	memset(&dst_addr, 0, sizeof(dst_addr));
	dst_addr.sin_family = AF_INET;
	dst_addr.sin_port = 0;

	ret = net_addr_pton(AF_INET, ipv4_addr, &dst_addr.sin_addr);
	if (ret < 0) {
		printk("Invalid IPv4 address: %s\n", ipv4_addr);
		return ret;
	}

	ret = net_icmp_init_ctx(&icmp_context,
				AF_INET,
				NET_ICMPV4_ECHO_REPLY,
				0,
				icmp_echo_reply_handler);
	if (ret < 0) {
		printk("Failed to init ping, err: %d\n", ret);
		return ret;
	}

	for (int i = 0; i < count; i++) {
		uint32_t cycles = k_cycle_get_32();

		ret = net_icmp_send_echo_request(&icmp_context,
						 iface,
						 (struct sockaddr *)&dst_addr,
						 NULL,
						 &cycles);
		if (ret < 0) {
			printk("Failed to send ping, err: %d\n", ret);
		} else {
			printk("Ping request %d sent to %s\n", i + 1, ipv4_addr);
		}

		k_sleep(K_SECONDS(2));
	}

	net_icmp_cleanup_ctx(&icmp_context);
	return 0;
}
