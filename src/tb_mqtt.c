#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>

#include "tb_mqtt.h"

#define MQTT_RX_BUFFER_SIZE 256
#define MQTT_TX_BUFFER_SIZE 256

static struct mqtt_client client;
static uint8_t rx_buffer[MQTT_RX_BUFFER_SIZE];
static uint8_t tx_buffer[MQTT_TX_BUFFER_SIZE];
static struct sockaddr_storage broker;

static volatile bool mqtt_connected = false;
static uint16_t next_message_id = 1;

static void mqtt_evt_handler(struct mqtt_client *const c,
                             const struct mqtt_evt *evt)
{
    ARG_UNUSED(c);

    switch (evt->type) {
    case MQTT_EVT_CONNACK:
        printk("MQTT EVT_CONNACK rc=%d\n", evt->param.connack.return_code);
        if (evt->param.connack.return_code == 0) {
            mqtt_connected = true;
        }
        break;

    case MQTT_EVT_DISCONNECT:
        printk("MQTT disconnected\n");
        mqtt_connected = false;
        break;

    default:
        printk("MQTT event: %d\n", evt->type);
        break;
    }
}

static int broker_init(const char *host, uint16_t port)
{
    struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;

    memset(&broker, 0, sizeof(broker));

    broker4->sin_family = AF_INET;
    broker4->sin_port = htons(port);

    if (zsock_inet_pton(AF_INET, host, &broker4->sin_addr) != 1) {
        printk("Invalid broker IPv4 address: %s\n", host);
        return -EINVAL;
    }

    return 0;
}

static int wait_for_connack(struct mqtt_client *client_ctx)
{
    struct zsock_pollfd fds[1];
    int ret;
    int tries = 0;

    fds[0].fd = client_ctx->transport.tcp.sock;
    fds[0].events = ZSOCK_POLLIN;
    fds[0].revents = 0;

    while (!mqtt_connected && tries < 20) {
        ret = zsock_poll(fds, 1, 500);
        if (ret < 0) {
            printk("zsock_poll failed: %d errno=%d\n", ret, errno);
            return ret;
        }

        if (ret > 0 && (fds[0].revents & ZSOCK_POLLIN)) {
            ret = mqtt_input(client_ctx);
            if (ret != 0) {
                printk("mqtt_input failed: %d\n", ret);
                return ret;
            }
        }

        ret = mqtt_live(client_ctx);
        if (ret != 0 && ret != -EAGAIN) {
            printk("mqtt_live failed: %d\n", ret);
            return ret;
        }

        tries++;
    }

    if (!mqtt_connected) {
        printk("Timeout waiting for MQTT CONNACK\n");
        return -ETIMEDOUT;
    }

    return 0;
}

int tb_mqtt_publish_telemetry(const char *host,
                              uint16_t port,
                              const char *access_token,
                              const char *json_payload)
{
    int ret;
    struct mqtt_publish_param param;
    struct mqtt_topic topic;

    struct mqtt_utf8 username = {
        .utf8 = (uint8_t *)access_token,
        .size = strlen(access_token),
    };

    struct mqtt_utf8 client_id = {
        .utf8 = (uint8_t *)"esp32s3-zephyr",
        .size = strlen("esp32s3-zephyr"),
    };

    mqtt_connected = false;

    ret = broker_init(host, port);
    if (ret < 0) {
        return ret;
    }

    mqtt_client_init(&client);

    client.broker = &broker;
    client.evt_cb = mqtt_evt_handler;
    client.client_id = client_id;
    client.user_name = &username;
    client.password = NULL;
    client.protocol_version = MQTT_VERSION_3_1_1;

    client.rx_buf = rx_buffer;
    client.rx_buf_size = sizeof(rx_buffer);
    client.tx_buf = tx_buffer;
    client.tx_buf_size = sizeof(tx_buffer);

    client.transport.type = MQTT_TRANSPORT_NON_SECURE;

    printk("Connecting MQTT to %s:%u\n", host, port);

    ret = mqtt_connect(&client);
    if (ret < 0) {
        printk("mqtt_connect failed: %d errno=%d\n", ret, errno);
        return ret;
    }

    ret = wait_for_connack(&client);
    if (ret < 0) {
        mqtt_disconnect(&client, NULL);
        return ret;
    }

    topic.topic = (struct mqtt_utf8) {
        .utf8 = (uint8_t *)"v1/devices/me/telemetry",
        .size = strlen("v1/devices/me/telemetry"),
    };
    topic.qos = MQTT_QOS_0_AT_MOST_ONCE;

    memset(&param, 0, sizeof(param));
    param.message.topic = topic;
    param.message.payload.data = (uint8_t *)json_payload;
    param.message.payload.len = strlen(json_payload);
    param.message_id = next_message_id++;
    param.dup_flag = 0;
    param.retain_flag = 0;

    ret = mqtt_publish(&client, &param);
    if (ret < 0) {
        printk("mqtt_publish failed: %d\n", ret);
        mqtt_disconnect(&client, NULL);
        return ret;
    }

    printk("Telemetry published: %s\n", json_payload);

    k_sleep(K_MSEC(300));
    mqtt_disconnect(&client, NULL);

    return 0;
}
