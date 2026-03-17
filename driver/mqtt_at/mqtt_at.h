#ifndef __MQTT_AT_H__
#define __MQTT_AT_H__

#include <stdbool.h>
#include <stdint.h>

/*
 * MQTT AT 驱动
 * 基于 ESP32-C3 AT 固件的 MQTT 3.1.1 指令封装
 *
 * ESP32 AT MQTT 指令流程：
 *   1. AT+MQTTUSERCFG  — 配置 ClientID / Username / Password
 *   2. AT+MQTTCONN     — 连接 Broker
 *   3. AT+MQTTSUB      — 订阅 Topic
 *   4. AT+MQTTPUB      — 发布消息
 *   5. AT+MQTTCLEAN    — 断开连接
 *
 * 收到订阅消息时 ESP32 主动上报：
 *   +MQTTSUBRECV:0,"topic",len,"payload"
 * 由 USART2 中断捕获后通过回调通知上层
 */

/* MQTT 连接参数 */
typedef struct {
    const char *host;       /* Broker 地址，如 "xxx.iot-as-mqtt.cn-shanghai.aliyuncs.com" */
    uint16_t    port;       /* 端口，阿里云非TLS用 1883 */
    const char *client_id;  /* MQTT ClientID */
    const char *username;   /* MQTT Username */
    const char *password;   /* MQTT Password（阿里云签名） */
    uint16_t    keepalive;  /* 心跳间隔，单位秒，建议 60 */
} mqtt_config_t;

/* 订阅消息回调：topic、payload、payload 长度 */
typedef void (*mqtt_sub_callback_t)(const char *topic, const char *payload, uint32_t len);

bool mqtt_at_connect(const mqtt_config_t *cfg);
bool mqtt_at_subscribe(const char *topic, uint8_t qos);
bool mqtt_at_publish(const char *topic, const char *payload, uint8_t qos);
bool mqtt_at_disconnect(void);
bool mqtt_at_is_connected(void);

/* 注册订阅消息回调，收到 +MQTTSUBRECV 时触发 */
void mqtt_at_set_sub_callback(mqtt_sub_callback_t cb);

/* 供 USART2_IRQHandler 调用，处理 MQTT 主动上报数据 */
void mqtt_at_isr_hook(uint8_t byte);

#endif /* __MQTT_AT_H__ */
