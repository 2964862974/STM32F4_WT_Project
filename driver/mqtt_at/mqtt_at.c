#include <string.h>
#include <stdio.h>
#include "mqtt_at.h"
#include "esp_at.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/*
 * MQTT AT 驱动实现
 *
 * ESP32 AT 固件内部完整实现 MQTT 3.1.1 协议栈，包括：
 *   - CONNECT 报文构建（ClientID/Username/Password/KeepAlive）
 *   - CONNACK 解析
 *   - PUBLISH / SUBSCRIBE / PINGREQ 报文
 *   - QoS 0/1 ACK 处理
 * STM32 只需通过 AT 指令驱动，无需自己构建 MQTT 报文
 *
 * 订阅消息接收：
 *   ESP32 收到 PUBLISH 后主动输出：
 *   +MQTTSUBRECV:0,"topic",len,"payload"\r\n
 *   由 mqtt_at_isr_hook() 在 USART2 中断里逐字节解析
 */

/* -----------------------------------------------------------------------
 * 内部状态
 * ----------------------------------------------------------------------- */
static mqtt_sub_callback_t s_sub_cb   = NULL;
static volatile bool       s_connected = false;

/* ISR 接收缓冲区，用于解析 +MQTTSUBRECV 行
 * 阿里云 OTA upgrade 消息含完整 OSS URL，通常 600~800 字节 */
#define ISR_BUF_SIZE 1024
static char     s_isr_buf[ISR_BUF_SIZE];
static uint32_t s_isr_len = 0;
static bool     s_isr_overflow = false;

/* -----------------------------------------------------------------------
 * mqtt_at_connect
 * 对应 MQTT 3.1.1 CONNECT 报文，由 ESP32 AT 固件构建并发送
 *
 * AT 指令序列：
 *   AT+MQTTUSERCFG=0,1,"ClientID","Username","Password",0,0,""
 *   AT+MQTTCONN=0,"host",port,0
 *
 * linkID 固定为 0（ESP32 AT 支持多路，这里只用一路）
 * ----------------------------------------------------------------------- */
bool mqtt_at_connect(const mqtt_config_t *cfg)
{
    char cmd[768];
    char escaped_client_id[256];

    /* ESP32 AT v4.x 要求字符串参数里的 ',' 用 '\,' 转义
     * ClientID 格式：xxx|securemode=2,signmethod=hmacsha256,timestamp=xxx|
     * 其中两个 ',' 必须转义，否则 AT 解析器把它们当参数分隔符 */
    uint32_t si = 0, di = 0;
    while (cfg->client_id[si] && di < sizeof(escaped_client_id) - 2) {
        if (cfg->client_id[si] == ',') {
            escaped_client_id[di++] = '\\';
        }
        escaped_client_id[di++] = cfg->client_id[si++];
    }
    escaped_client_id[di] = '\0';

    snprintf(cmd, sizeof(cmd),
             "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
             escaped_client_id, cfg->username, cfg->password);
    if (!esp_at_write_command(cmd, 3000)) {
        printf("[MQTT] MQTTUSERCFG failed\r\n");
        return false;
    }

    snprintf(cmd, sizeof(cmd),
             "AT+MQTTCONNCFG=0,%u,0,\"\",\"\",0,0\r\n",
             cfg->keepalive);
    if (!esp_at_write_command(cmd, 3000)) {
        printf("[MQTT] MQTTCONNCFG failed\r\n");
        return false;
    }

    snprintf(cmd, sizeof(cmd),
             "AT+MQTTCONN=0,\"%s\",%u,0\r\n",
             cfg->host, cfg->port);
    if (!esp_at_write_command(cmd, 10000)) {
        printf("[MQTT] MQTTCONN failed\r\n");
        return false;
    }

    s_connected = true;
    printf("[MQTT] Connected to %s:%u\r\n", cfg->host, cfg->port);
    return true;
}

/* -----------------------------------------------------------------------
 * mqtt_at_subscribe
 * 对应 MQTT 3.1.1 SUBSCRIBE 报文
 *
 * AT+MQTTSUB=<linkID>,<topic>,<qos>
 * ----------------------------------------------------------------------- */
bool mqtt_at_subscribe(const char *topic, uint8_t qos)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "AT+MQTTSUB=0,\"%s\",%u\r\n", topic, qos);
    if (!esp_at_write_command(cmd, 5000)) {
        printf("[MQTT] Subscribe failed: %s\r\n", topic);
        return false;
    }
    printf("[MQTT] Subscribed: %s QoS%u\r\n", topic, qos);
    return true;
}

/* -----------------------------------------------------------------------
 * mqtt_at_publish
 * 对应 MQTT 3.1.1 PUBLISH 报文
 *
 * AT+MQTTPUB=<linkID>,<topic>,<data>,<qos>,<retain>
 * ----------------------------------------------------------------------- */
bool mqtt_at_publish(const char *topic, const char *payload, uint8_t qos)
{
    char cmd[256];
    uint32_t pay_len = strlen(payload);

    /* AT+MQTTPUBRAW 避免 payload 里的双引号转义问题
     * 流程：发指令 → 等 '>' → 发 payload → 等 OK */
    snprintf(cmd, sizeof(cmd),
             "AT+MQTTPUBRAW=0,\"%s\",%lu,%u,0\r\n",
             topic, (unsigned long)pay_len, qos);

    esp_at_raw_write(cmd);

    /* 等待 ESP32 回复 '>' 提示符 */
    if (!esp_at_wait_prompt(3000)) {
        printf("[MQTT] Publish prompt timeout: %s\r\n", topic);
        return false;
    }

    /* 发送原始 JSON payload，ESP32 收完后回复 OK */
    if (!esp_at_write_command(payload, 5000)) {
        printf("[MQTT] Publish ack timeout: %s\r\n", topic);
        return false;
    }

    printf("[MQTT] Published to %s (%lu bytes)\r\n", topic, (unsigned long)pay_len);
    return true;
}

/* -----------------------------------------------------------------------
 * mqtt_at_disconnect
 * 对应 MQTT 3.1.1 DISCONNECT 报文
 *
 * AT+MQTTCLEAN=<linkID>
 * ----------------------------------------------------------------------- */
bool mqtt_at_disconnect(void)
{
    s_connected = false;
    return esp_at_write_command("AT+MQTTCLEAN=0\r\n", 3000);
}

bool mqtt_at_is_connected(void)
{
    return s_connected;
}

void mqtt_at_set_sub_callback(mqtt_sub_callback_t cb)
{
    s_sub_cb = cb;
}

/* -----------------------------------------------------------------------
 * mqtt_at_isr_hook
 * 在 USART2_IRQHandler 里逐字节调用，解析 ESP32 主动上报的订阅消息
 *
 * ESP32 收到 PUBLISH 后输出格式：
 *   +MQTTSUBRECV:0,"topic/name",payload_len,"payload_content"\r\n
 *
 * 解析流程：
 *   1. 逐字节累积到 s_isr_buf
 *   2. 遇到 '\n' 说明一行结束
 *   3. 检查是否以 "+MQTTSUBRECV:" 开头
 *   4. 解析出 topic 和 payload，调用回调
 * ----------------------------------------------------------------------- */
void mqtt_at_isr_hook(uint8_t byte)
{
    if (s_isr_overflow) {
        /* 溢出状态：丢弃字节直到行结束 */
        if (byte == '\n') {
            s_isr_len = 0;
            s_isr_overflow = false;
        }
        return;
    }

    if (s_isr_len >= ISR_BUF_SIZE - 1) {
        /* 缓冲区满，标记溢出，等待行结束后重置 */
        s_isr_overflow = true;
        s_isr_len = 0;
        return;
    }

    s_isr_buf[s_isr_len++] = (char)byte;

    if (byte != '\n')
        return;

    /* 一行结束，检查是否是订阅消息 */
    s_isr_buf[s_isr_len] = '\0';

    if (strncmp(s_isr_buf, "+MQTTSUBRECV:", 13) == 0 && s_sub_cb != NULL) {
        /*
         * 格式：+MQTTSUBRECV:0,"topic",len,"payload"\r\n
         * 解析 topic 和 payload
         */
        char    topic[128]   = {0};
        char    payload[768] = {0};
        uint32_t pay_len     = 0;

        /* 跳过 linkID 字段，找第一个引号（topic 开始） */
        char *p = strchr(s_isr_buf, '"');
        if (p) {
            /* 解析 topic */
            char *topic_end = strchr(p + 1, '"');
            if (topic_end) {
                uint32_t tlen = topic_end - (p + 1);
                if (tlen < sizeof(topic)) {
                    memcpy(topic, p + 1, tlen);
                    topic[tlen] = '\0';
                }

                /* 跳过 ",len," 找 payload 引号 */
                p = strchr(topic_end + 1, '"');
                if (p) {
                    char *pay_end = strrchr(p + 1, '"');
                    if (pay_end && pay_end > p) {
                        pay_len = pay_end - (p + 1);
                        if (pay_len < sizeof(payload)) {
                            memcpy(payload, p + 1, pay_len);
                            payload[pay_len] = '\0';
                            s_sub_cb(topic, payload, pay_len);
                        }
                    }
                }
            }
        }
    }

    s_isr_len = 0;
}
