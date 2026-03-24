#ifndef TB_MQTT_H
#define TB_MQTT_H

int tb_mqtt_publish_telemetry(const char *host,
                              uint16_t port,
                              const char *access_token,
                              const char *json_payload);

#endif
