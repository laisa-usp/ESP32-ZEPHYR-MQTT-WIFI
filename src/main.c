#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_ip.h>

#include "ping.h"
#include "tb_mqtt.h"

#include <zephyr/net/socket.h>
#include <errno.h>

/* ThingsBoard */
#define THINGSBOARD_HOST  "192.168.9.192"
#define THINGSBOARD_PORT  1883
#define THINGSBOARD_TOKEN "ofv7vsbetj3lat80za0t"

/* Wi-Fi Config */
#define WIFI_SSID      "SEU NOME DE REDE WIFI"
#define WIFI_PASSWORD  "SUA SENHA"

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

static bool wifi_connected = false;
static bool dhcp_started = false;
static bool dhcp_ok = false;

static char gateway_ip[NET_IPV4_ADDR_LEN];

/* ===== IPv4 / DHCP callback ===== */
static void ipv4_event_handler( struct net_mgmt_event_callback *cb,
                                               uint64_t mgmt_event,
                                               struct net_if *iface
							                   )
{
    ARG_UNUSED(cb);

    if (mgmt_event == NET_EVENT_IPV4_DHCP_BOUND) {
        char ip_buf[NET_IPV4_ADDR_LEN];
        char mask_buf[NET_IPV4_ADDR_LEN];
        struct in_addr netmask;

        dhcp_ok = true;

        printk("Matched DHCP_BOUND\n");

        printk("IP: %s\n",
               net_addr_ntop(AF_INET,
                             &iface->config.dhcpv4.requested_ip,
                             ip_buf, sizeof(ip_buf)));

        net_addr_ntop(AF_INET,
                      &iface->config.ip.ipv4->gw,
                      gateway_ip, sizeof(gateway_ip));

        printk("Gateway: %s\n", gateway_ip);

        netmask = net_if_ipv4_get_netmask_by_addr(
            iface, &iface->config.dhcpv4.requested_ip);

        printk("Netmask: %s\n",
               net_addr_ntop(AF_INET,
                             &netmask,
                             mask_buf, sizeof(mask_buf)));

        printk("Lease time: %u s\n", iface->config.dhcpv4.lease_time);
    }
}

/* ===== Wi-Fi callback ===== */
static void wifi_event_handler(struct net_mgmt_event_callback *cb,
                                            uint64_t mgmt_event,
                                            struct net_if *iface)
{
    const struct wifi_status *status =
        (const struct wifi_status *)cb->info;

    printk("Wi-Fi event received: 0x%016llx\n",
           (unsigned long long)mgmt_event);

    if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
        printk("Matched CONNECT_RESULT\n");

        if (status == NULL) {
            printk("CONNECT_RESULT sem status\n");
            return;
        }

        printk("status=%d conn_status=%d disconn_reason=%d\n",
               status->status,
               status->conn_status,
               status->disconn_reason);

        if (status->status == 0) {
            printk("Wi-Fi connected successfully\n");
            wifi_connected = true;

            if (!dhcp_started) {
                printk("Starting DHCPv4...\n");
                net_dhcpv4_start(iface);
                dhcp_started = true;
            }
        } else {
            printk("Wi-Fi connection failed\n");
            wifi_connected = false;
        }

        return;
    }

    if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
        printk("Matched DISCONNECT_RESULT\n");

        if (status != NULL) {
            printk("status=%d conn_status=%d disconn_reason=%d\n",
                   status->status,
                   status->conn_status,
                   status->disconn_reason);
        } else {
            printk("DISCONNECT_RESULT sem status\n");
        }

        printk("Wi-Fi disconnected\n");
        wifi_connected = false;
        dhcp_started = false;
        dhcp_ok = false;
        return;
    }

    printk("Unhandled Wi-Fi event\n");
}

/*===TCP socket testing===*/
static int test_tcp_connect(const char *host, uint16_t port)
{
    int ret;
    int sock;
    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    ret = zsock_inet_pton(AF_INET, host, &addr.sin_addr);
    if (ret != 1) {
        printk("inet_pton falhou para %s, ret=%d\n", host, ret);
        return -EINVAL;
    }

    sock = zsock_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printk("socket() falhou: %d errno=%d\n", sock, errno);
        return -errno;
    }

    printk("socket criado: %d\n", sock);
    printk("tentando conectar em %s:%u...\n", host, port);

    ret = zsock_connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        printk("connect() falhou: ret=%d errno=%d\n", ret, errno);
        zsock_close(sock);
        return -errno;
    }

    printk("TCP successfully conencted %s:%u\n", host, port);

    zsock_close(sock);
    printk("socket fechado\n");

    return 0;
}


/* ===== Wi-Fi connect request ===== */
static int wifi_connect(struct net_if *iface)
{
    struct wifi_connect_req_params cnx = {0};
    int ret;

    cnx.ssid = (const uint8_t *)WIFI_SSID;
    cnx.ssid_length = strlen(WIFI_SSID);

    cnx.psk = (const uint8_t *)WIFI_PASSWORD;
    cnx.psk_length = strlen(WIFI_PASSWORD);

    cnx.security = WIFI_SECURITY_TYPE_PSK;
    cnx.channel = WIFI_CHANNEL_ANY;
    cnx.band = WIFI_FREQ_BAND_UNKNOWN;
    cnx.mfp = WIFI_MFP_OPTIONAL;
    cnx.timeout = SYS_FOREVER_MS;

    printk("Sending Wi-Fi connect request...\n");

    ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx, sizeof(cnx));

    printk("net_mgmt(NET_REQUEST_WIFI_CONNECT) returned: %d\n", ret);
    return ret;
}

/* ===== Main ===== */
int main(void)
{
    struct net_if *iface;
    int ret;
    int tries;

    printk("=== MAIN STARTED ===\n");

    iface = net_if_get_default();
    if (iface == NULL) {
        printk("No default network interface\n");
        return -1;
    }

    printk("Default network interface found: %p\n", iface);

    net_mgmt_init_event_callback(&wifi_cb,
                                 wifi_event_handler,
                                 NET_EVENT_WIFI_CONNECT_RESULT |
                                 NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&wifi_cb);

    net_mgmt_init_event_callback(&ipv4_cb,
                                 ipv4_event_handler,
                                 NET_EVENT_IPV4_DHCP_BOUND);
    net_mgmt_add_event_callback(&ipv4_cb);

    ret = wifi_connect(iface);
    if (ret != 0) {
        printk("wifi_connect() failed: %d\n", ret);
        return ret;
    }

    printk("Waiting Wi-Fi connection...\n");
    tries = 0;
    while (!wifi_connected && tries < 20) {
        k_sleep(K_SECONDS(1));
        printk("still waiting Wi-Fi...\n");
        tries++;
    }

    if (!wifi_connected) {
        printk("Wi-Fi could not connect within the timeout\n");
        return -1;
    }

    printk("Waiting DHCP...\n");
    tries = 0;
    while (!dhcp_ok && tries < 20) {
        k_sleep(K_SECONDS(1));
        printk("still waiting DHCP...\n");
        tries++;
    }

    if (!dhcp_ok) {
        printk("DHCP could not assign IP within the timeout\n");
        return -1;
    }
    

    printk("Starting ping to gateway...\n");
    /* ping(gateway_ip, 4);*/
    ping("192.168.9.192", 4);

k_sleep(K_SECONDS(1));

    printk("Starting ping to gateway...\n");
    /* ping(gateway_ip, 4);*/
    ping("192.168.9.192", 4);

k_sleep(K_SECONDS(1));

    printk("Starting TCP test to ThingsBoard host...\n");
    ret = test_tcp_connect("192.168.9.192", 1883);
    printk("TCP test ret=%d\n", ret);

k_sleep(K_SECONDS(1));

    while (1) {
        printk("Sending telemetry to ThingsBoard...\n");

        ret = tb_mqtt_publish_telemetry(
            THINGSBOARD_HOST,
            THINGSBOARD_PORT,
            THINGSBOARD_TOKEN,
            "{\"temperature\":25,\"humidity\":60}"
        );

        if (ret == 0) {
            printk("Telemetry sent successfully\n");
        } else {
            printk("Telemetry failed (%d)\n", ret);
        }

        k_sleep(K_SECONDS(10));
    }

    return 0;
}
