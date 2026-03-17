#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "http_ota.h"
#include "esp_at.h"
#include "w25q32.h"
#include "iwdg.h"

/*
 * 使用 AT+HTTPCGET 流式下载固件
 * ESP32 分多段推送 +HTTPCGET:<len>,<data>
 *
 * 单缓冲方案：ISR 填充 g_page_buf，满 PAGE_SIZE 后通知任务
 * 任务拷贝数据到本地缓冲区后立即清空 g_page_buf 让 ISR 继续
 */

#define HEADER_BUF_SIZE  256
#define PAGE_SIZE        256

/* ISR 填充的页缓冲区 */
static volatile uint8_t  g_page_buf[PAGE_SIZE];
static volatile uint32_t g_page_len   = 0;

static volatile bool     g_active     = false;
static volatile uint32_t g_seg_expect = 0;
static volatile uint32_t g_seg_recv   = 0;
static volatile uint32_t g_total_recv = 0;
static volatile uint32_t g_hook_bytes = 0;   /* 调试：hook 收到的总字节数 */

static char              g_hdr_buf[HEADER_BUF_SIZE];
static volatile uint32_t g_hdr_len    = 0;
static volatile bool     g_in_data    = false;

static SemaphoreHandle_t g_page_sem   = NULL;

/* -----------------------------------------------------------------------
 * ISR hook
 * ----------------------------------------------------------------------- */
bool http_ota_isr_hook(uint8_t byte)
{
    if (!g_active)
        return false;

    BaseType_t woken = pdFALSE;

    if (!g_in_data)
    {
        if (byte == '\r' || byte == '\n') {
            g_hdr_len = 0;
            return true;
        }

        if (g_hdr_len < HEADER_BUF_SIZE - 1)
            g_hdr_buf[g_hdr_len++] = (char)byte;

        if (byte == ',')
        {
            g_hdr_buf[g_hdr_len] = '\0';
            const char *p = strstr(g_hdr_buf, "+HTTPCGET:");
            if (p)
            {
                p += 10;
                uint32_t dlen = 0;
                while (*p >= '0' && *p <= '9')
                    dlen = dlen * 10 + (*p++ - '0');
                g_seg_expect = dlen;
                g_seg_recv   = 0;
                g_in_data    = true;
            }
            else
            {
                g_hdr_len = 0;
            }
        }
        return true;
    }

    /* 数据阶段 */
    if (g_page_len < PAGE_SIZE)
        g_page_buf[g_page_len] = byte;
    g_page_len++;
    g_seg_recv++;
    g_total_recv++;

    bool page_full = (g_page_len >= PAGE_SIZE);
    bool seg_done  = (g_seg_recv >= g_seg_expect);

    if (page_full || seg_done)
    {
        xSemaphoreGiveFromISR(g_page_sem, &woken);
        portYIELD_FROM_ISR(woken);

        if (seg_done)
        {
            g_in_data = false;
            g_hdr_len = 0;
        }
    }

    return true;
}

/* -----------------------------------------------------------------------
 * http_ota_download
 * ----------------------------------------------------------------------- */
uint32_t http_ota_download(const char *url, uint32_t fw_size,
                           uint32_t flash_addr, http_ota_progress_cb_t progress_cb)
{
    if (g_page_sem == NULL)
        g_page_sem = xSemaphoreCreateBinary();

    g_hdr_len    = 0;
    g_in_data    = false;
    g_seg_expect = 0;
    g_seg_recv   = 0;
    g_total_recv = 0;
    g_page_len   = 0;
    g_active     = false;

    xSemaphoreTake(g_page_sem, 0);

    /* AT+HTTPURLCFG 预配置 URL */
    static char cmd[768];
    static char url_buf[768];
    uint32_t url_len = strlen(url);

    snprintf(cmd, sizeof(cmd), "AT+HTTPURLCFG=%lu\r\n", (unsigned long)url_len);
    printf("[HTTP_OTA] Setting URL (%lu bytes)...\r\n", (unsigned long)url_len);
    if (!esp_at_write_command(cmd, 3000)) {
        printf("[HTTP_OTA] HTTPURLCFG cmd failed\r\n");
        return 0;
    }
    if (!esp_at_wait_prompt(3000)) {
        printf("[HTTP_OTA] HTTPURLCFG prompt timeout\r\n");
        return 0;
    }
    snprintf(url_buf, sizeof(url_buf), "%s", url);
    if (!esp_at_write_command(url_buf, 5000)) {
        printf("[HTTP_OTA] HTTPURLCFG url send failed\r\n");
        return 0;
    }
    printf("[HTTP_OTA] URL configured OK\r\n");

    /* 激活 hook，启用 DMA 接收，发送下载命令 */
    g_active = true;
    snprintf(cmd, sizeof(cmd), "AT+HTTPCGET=\"\",,%u\r\n", 10240);
    printf("[HTTP_OTA] Starting download\r\n");
    esp_at_raw_write(cmd);

    /* 等待第一段数据头 */
    uint32_t wait_start = xTaskGetTickCount();
    while (!g_in_data) {
        if ((xTaskGetTickCount() - wait_start) > pdMS_TO_TICKS(15000)) {
            g_hdr_buf[g_hdr_len < HEADER_BUF_SIZE - 1 ? g_hdr_len : HEADER_BUF_SIZE - 1] = '\0';
            printf("[HTTP_OTA] No data header, raw: [%s]\r\n", g_hdr_buf);
            g_active = false;
            /* DMA 全程运行，无需 stop */
            return 0;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    uint32_t written  = 0;
    uint32_t cur_addr = flash_addr;
    uint8_t  local_buf[PAGE_SIZE];

    while (written < fw_size)
    {
        if (xSemaphoreTake(g_page_sem, pdMS_TO_TICKS(60000)) != pdTRUE)
        {
            printf("[HTTP_OTA] timeout, written=%lu/%lu total_recv=%lu\r\n",
                   (unsigned long)written, (unsigned long)fw_size,
                   (unsigned long)g_total_recv);
            g_active = false;
            /* DMA 全程运行，无需 stop */
            return 0;
        }

        /* 拷贝数据到本地缓冲区，立即清空让 ISR 继续填充 */
        uint32_t plen = g_page_len;
        if (plen > PAGE_SIZE) plen = PAGE_SIZE;
        memcpy(local_buf, (const void *)g_page_buf, plen);
        g_page_len = 0;  /* 清空，ISR 可以继续写入 */

        if (plen == 0)
            continue;

        w25q32_write(cur_addr, local_buf, plen);
        cur_addr += plen;
        written  += plen;

        iwdg_feed();

        if (progress_cb)
            progress_cb(written, fw_size);
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    g_active = false;
    /* DMA 全程运行，无需 stop */

    printf("[HTTP_OTA] Done, written=%lu bytes\r\n", (unsigned long)written);
    return written;
}
