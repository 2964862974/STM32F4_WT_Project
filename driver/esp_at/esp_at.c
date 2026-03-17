#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "stm32f4xx.h"
#include "esp_at.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#define ESP_AT_DEBUG 0

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* -----------------------------------------------------------------------
 * RX DMA 环形缓冲区（仅 OTA 下载时启用）
 * DMA1_Stream5 Ch4, USART2 RX, 循环模式
 * ----------------------------------------------------------------------- */
#define RX_DMA_BUF_SIZE 2048
static uint8_t  rx_dma_buf[RX_DMA_BUF_SIZE];
static uint32_t rx_dma_rd_pos = 0;

static QueueHandle_t  at_ack_semaphore;

typedef enum
{
    AT_ACK_NONE,
    AT_ACK_OK,
    AT_ACK_ERROR,
    AT_ACK_BUSY,
    AT_ACK_READY,
} at_ack_t;
static volatile at_ack_t at_ack_received;
typedef struct
{
    at_ack_t ack;
    const char *string;
} at_ack_match_t;

static const at_ack_match_t at_ack_matches[] =
    {
        {AT_ACK_OK, "OK\r\n"},
        {AT_ACK_OK, "SET OK\r\n"},   /* AT+HTTPURLCFG 发完 URL 后回 SET OK */
        {AT_ACK_ERROR, "ERROR\r\n"},
        {AT_ACK_BUSY, "busy p\r\n"},
        {AT_ACK_READY, "ready\r\n"},
};

static char rxbuf[1024];
static uint32_t rxlen = 0;
static const char *rxline ;
static void esp_at_usart_write(const char *data);

static void esp_at_io_init(void)
{

    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_StructInit(&GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}
static void esp_at_usart_init(void)
{
    USART_InitTypeDef USART_InitStructure;
    USART_StructInit(&USART_InitStructure);

    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;

    USART_Init(USART2, &USART_InitStructure);
    USART_DMACmd(USART2, USART_DMAReq_Tx, ENABLE);
    USART_DMACmd(USART2, USART_DMAReq_Rx, ENABLE);
    USART_Cmd(USART2, ENABLE);

    /* 清除 IDLE 标志后再启用 IDLE 中断，防止初始化时误触发 */
    (void)USART2->SR;
    (void)USART2->DR;
    USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);
}
static void esp_at_dma_init(void)
{
    /* TX DMA: DMA1_Stream6 Ch4 */
    DMA_InitTypeDef DMA_InitStructure;
    DMA_StructInit(&DMA_InitStructure);
    DMA_InitStructure.DMA_Channel = DMA_Channel_4;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART2->DR;
    DMA_InitStructure.DMA_DIR = DMA_DIR_MemoryToPeripheral;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Enable;
    DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_INC8;
    DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_Init(DMA1_Stream6, &DMA_InitStructure);

    /* RX DMA: DMA1_Stream5 Ch4, 循环模式，初始化后直接启动 */
    DMA_StructInit(&DMA_InitStructure);
    DMA_InitStructure.DMA_Channel = DMA_Channel_4;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART2->DR;
    DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)rx_dma_buf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory;
    DMA_InitStructure.DMA_BufferSize = RX_DMA_BUF_SIZE;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
    DMA_Init(DMA1_Stream5, &DMA_InitStructure);
    DMA_Cmd(DMA1_Stream5, ENABLE);
}
static void esp_at_int_init(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    NVIC_InitTypeDef NVIC_InitStructure;

    /* USART2 中断（仅 IDLE） */
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}
bool esp_at_init(void)
{
    at_ack_semaphore = xSemaphoreCreateBinary();
    configASSERT(at_ack_semaphore);
    esp_at_io_init();
    esp_at_dma_init();     /* DMA 先于 USART，避免 IDLE 中断触发时 DMA 未就绪 */
    esp_at_usart_init();
    esp_at_int_init();
    esp_at_write_command("AT\r\n", 100);
    if (!esp_at_write_command("AT\r\n", 100))
        return false;
    if (!esp_at_write_command("AT+RESTORE\r\n", 2000))
        return false;
    if (!esp_at_wait_ready(5000))
        return false;

    /* 查询固件版本，串口打印出来方便排查 */
    esp_at_write_command("AT+GMR\r\n", 2000);
    printf("[ESP32] Version: %s\r\n", esp_at_get_response());

    /* 关闭命令回显，避免 http_ota_isr_hook 的 header 缓冲区被回显内容填满 */
    esp_at_write_command("ATE0\r\n", 1000);

    return true;
}

static void esp_at_usart_write(const char *data)
{
    uint32_t len = strlen(data);
    DMA_Cmd(DMA1_Stream6, DISABLE);
    DMA1_Stream6->NDTR = len;
    DMA1_Stream6->M0AR = (uint32_t)data;
    DMA_ClearFlag(DMA1_Stream6, DMA_FLAG_TCIF6);
    DMA_Cmd(DMA1_Stream6, ENABLE);
}

void esp_at_raw_write(const char *data)
{
    esp_at_usart_write(data);
}

static at_ack_t match_internal_ack(const char *str)
{
    for (uint32_t i = 0; i < ARRAY_SIZE(at_ack_matches); i++)
    {
        if (strcmp(str, at_ack_matches[i].string) == 0)
            return at_ack_matches[i].ack;
    }

    return AT_ACK_NONE;
}

/* 等待 ESP32 输出 '>' 提示符（用于 MQTTPUBRAW / CIPSEND 等透传模式）
 * 超时返回 false */
bool esp_at_wait_prompt(uint32_t timeout_ms)
{
    uint32_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms)) {
        /* 检查 rxbuf 里是否出现了 '>' */
        if (rxlen > 0 && rxbuf[rxlen - 1] == '>') {
            rxlen = 0;
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

static at_ack_t esp_at_usart_wait_receive(uint32_t timeout)
{
    rxlen = 0;
    rxline = rxbuf;
    if (xSemaphoreTake(at_ack_semaphore, pdMS_TO_TICKS(timeout)) == pdTRUE)
    {
        return at_ack_received;
    }
    else
    {
        return AT_ACK_NONE;
    }
}

bool esp_at_wait_ready(uint32_t timeout)
{
    return esp_at_usart_wait_receive(timeout) == AT_ACK_READY;
}

bool esp_at_write_command(const char *command, uint32_t timeout)
{
#if ESP_AT_DEBUG
    printf("[DEBUG] Send: %s\n", command);
#endif

    esp_at_usart_write(command);
    at_ack_t ack = esp_at_usart_wait_receive(timeout);

#if ESP_AT_DEBUG
    printf("[DEBUG] Response:\n%s\n", rxbuf);
#endif

    return ack == AT_ACK_OK;
}

const char *esp_at_get_response(void)
{
    return rxbuf;
}

bool esp_at_wifi_init(void)
{
    return esp_at_write_command("AT+CWMODE=1\r\n", 2000);
}

bool esp_at_connect_wifi(const char *ssid, const char *pwd, const char *mac)
{
    if (ssid == NULL || pwd == NULL)
        return false;

    char cmd[128];
    int len = snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd);
    if (mac)
        snprintf(cmd + len, sizeof(cmd) - len, ",\"%s\"", mac);

    return esp_at_write_command(cmd, 5000);
}

static bool parse_cwstate_response(const char *response, esp_wifi_info_t *info)
{
    //    AT+CWSTATE?
    //    +CWSTATE:2,"Xiaomi Mi MIX 3_5577"

    //    OK
    response = strstr(response, "+CWSTATE:");
    if (response == NULL)
        return false;

    int wifi_state;
    if (sscanf(response, "+CWSTATE:%d,\"%63[^\"]", &wifi_state, info->ssid) != 2)
        return false;

    info->connected = (wifi_state == 2);

    return true;
}

static bool parse_cwjap_response(const char *response, esp_wifi_info_t *info)
{
    //    AT+CWJAP?
    //    +CWJAP:"Xiaomi Mi MIX 3_5577","da:b5:3a:e3:2f:60",9,-48,0,1,3,0,1

    //    OK
    response = strstr(response, "+CWJAP:");
    if (response == NULL)
        return false;

    if (sscanf(response, "+CWJAP:\"%63[^\"]\",\"%17[^\"]\",%d,%d", info->ssid, info->bssid, &info->channel, &info->rssi) != 4)
        return false;

    return true;
}

bool esp_at_get_wifi_info(esp_wifi_info_t *info)
{
    if (!esp_at_write_command("AT+CWSTATE?\r\n", 2000))
        return false;

    if (!parse_cwstate_response(esp_at_get_response(), info))
        return false;

    if (!info->connected)
        return true;

    if (!esp_at_write_command("AT+CWJAP?\r\n", 2000))
        return false;

    if (!parse_cwjap_response(esp_at_get_response(), info))
        return false;

    return true;
}

bool wifi_is_connected(void)
{
    esp_wifi_info_t info;
    if (esp_at_get_wifi_info(&info))
    {
        return info.connected;
    }
    return false;
}

bool esp_at_sntp_init(void)
{
    if (!esp_at_write_command("AT+CIPSNTPCFG=1,8\r\n", 2000))
        return false;

    return true;
}

static uint8_t month_str_to_num(const char *month_str)
{
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (uint8_t i = 0; i < 12; i++)
    {
        if (strcmp(month_str, months[i]) == 0)
        {
            return i + 1;
        }
    }
    return 0;
}

static uint8_t weekday_str_to_num(const char *weekday_str)
{
    const char *weekdays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    for (uint8_t i = 0; i < 7; i++)
    {
        if (strcmp(weekday_str, weekdays[i]) == 0)
        {
            return i + 1;
        }
    }
    return 0;
}

static bool parse_cipsntptime_response(const char *response, esp_date_time_t *date)
{
    //	AT+CIPSNTPTIME?
    //	+CIPSNTPTIME:Sun Jul 27 14:07:19 2025
    //	OK
    char weekday_str[8];
    char month_str[4];
    response = strstr(response, "+CIPSNTPTIME:");
    if (sscanf(response, "+CIPSNTPTIME:%3s %3s %hhu %hhu:%hhu:%hhu %hu",
               weekday_str, month_str,
               &date->day, &date->hour, &date->minute, &date->second, &date->year) != 7)
        return false;

    date->weekday = weekday_str_to_num(weekday_str);
    date->month = month_str_to_num(month_str);

    return true;
}

bool esp_at_sntp_get_time(esp_date_time_t *date)
{
    if (!esp_at_write_command("AT+CIPSNTPTIME?\r\n", 2000))
        return false;

    if (!parse_cipsntptime_response(esp_at_get_response(), date))
        return false;

    return true;
}

const char *esp_at_http_get(const char *url)
{
    //    AT+HTTPCLIENT=2,1,"https://api.seniverse.com/v3/weather/now.json?key=SfRic8Wmp-Qh3OeFk&location=WTEMH46Z5N09&language=en&unit=c",,,2
    //    +HTTPCLIENT:261,{"results":[{"location":{"id":"WTEMH46Z5N09","name":"Hefei","country":"CN","path":"Hefei,Hefei,Anhui,China","timezone":"Asia/Shanghai","timezone_offset":"+08:00"},"now":{"text":"Cloudy","code":"4","temperature":"32"},"last_update":"2025-07-26T16:30:00+08:00"}]}

    //    OK
    char *txbuf = rxbuf;
    snprintf(txbuf, sizeof(rxbuf), "AT+HTTPCLIENT=2,1,\"%s\",,,2\r\n", url);
    bool ret = esp_at_write_command(txbuf, 5000);
    return ret ? esp_at_get_response() : NULL;
}

#include "mqtt_at.h"
#include "http_ota.h"

/* -----------------------------------------------------------------------
 * RX DMA 环形缓冲区处理：读取 DMA 写入位置，逐字节喂给 hooks
 * 和原 USART2_IRQHandler RXNE 逻辑一致
 * ----------------------------------------------------------------------- */
static void process_rx_dma(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint32_t wr_pos = RX_DMA_BUF_SIZE - DMA_GetCurrDataCounter(DMA1_Stream5);

    while (rx_dma_rd_pos != wr_pos)
    {
        uint8_t byte = rx_dma_buf[rx_dma_rd_pos++];
        if (rx_dma_rd_pos >= RX_DMA_BUF_SIZE)
            rx_dma_rd_pos = 0;

        if (http_ota_isr_hook(byte))
            continue;

        mqtt_at_isr_hook(byte);

        if (rxlen < sizeof(rxbuf) - 1)
        {
            rxbuf[rxlen++] = byte;
            if (rxbuf[rxlen - 1] == '\n')
            {
                rxbuf[rxlen] = '\0';
                at_ack_t ack = match_internal_ack(rxline);
                rxline = rxbuf + rxlen;
                if (ack != AT_ACK_NONE)
                {
                    at_ack_received = ack;
                    xSemaphoreGiveFromISR(at_ack_semaphore, &xHigherPriorityTaskWoken);
                    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
                }
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * USART2 中断：仅 IDLE，DMA 数据间隙触发，处理环形缓冲区新数据
 * ----------------------------------------------------------------------- */
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_IDLE) != RESET)
    {
        /* 清除 IDLE 标志：先读 SR 再读 DR */
        (void)USART2->SR;
        (void)USART2->DR;
        process_rx_dma();
    }
}
