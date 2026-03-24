# ESP32-ZEPHYR-MQTT-WIFI
Código referente ao tutorial IoT. Código para ESP32S3 com Zephyr e MQTT client.

A configuração de rede e do MQTT broker está hard coded no main.c:
/* ThingsBoard */
#define THINGSBOARD_HOST  "192.168.9.192"
#define THINGSBOARD_PORT  1883
#define THINGSBOARD_TOKEN "ofv7vsbetj3lat80za0t"

/* Wi-Fi Config */
#define WIFI_SSID      "SEU NOME DE REDE WIFI"
#define WIFI_PASSWORD  "SUA SENHA"

O tópico MQTT utilizado é: "v1/devices/me/telemetry".
A qualidade de serviço utilizada neste exemplo é de nível 0: MQTT_QOS_0_AT_MOST_ONCE.
