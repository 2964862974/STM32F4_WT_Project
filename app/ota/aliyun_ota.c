#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "aliyun_ota.h"
#include "mqtt_at.h"
#include "http_ota.h"
#include "w25q32.h"
#include "ota.h"
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"


/*
 * 阿里云 OTA 模块实现
 *
 * 阿里云 OTA 升级消息格式（JSON）：
 * {
 *   "code": "1000",
 *   "data": {
 *     "size": 131072,
 *     "version": "2.0.0",
 *     "url": "https://ota-pre.iot-thing.cn-shanghai.aliyuncs.com/xxx/app.bin",
 *     "md5": "93e6a4d5f9c3b2a1...",
 *     "sign": "xxx",
 *     "signMethod": "Md5"
 *   },
 *   "id": 1,
 *   "message": "success"
 * }
 */

/* -----------------------------------------------------------------------
 * Topic 定义
 * ----------------------------------------------------------------------- */
#define TOPIC_INFORM    "/ota/device/inform/"   ALIYUN_PRODUCT_KEY "/" ALIYUN_DEVICE_NAME
#define TOPIC_UPGRADE   "/ota/device/upgrade/"  ALIYUN_PRODUCT_KEY "/" ALIYUN_DEVICE_NAME
#define TOPIC_PROGRESS  "/ota/device/progress/" ALIYUN_PRODUCT_KEY "/" ALIYUN_DEVICE_NAME
/* 主动拉取固件信息：发布请求，阿里云通过 TOPIC_UPGRADE 回复 */
#define TOPIC_GET       "/sys/" ALIYUN_PRODUCT_KEY "/" ALIYUN_DEVICE_NAME "/thing/ota/firmware/get"

/* -----------------------------------------------------------------------
 * 内部状态：收到升级通知后存储升级信息
 * ----------------------------------------------------------------------- */
static volatile bool s_upgrade_pending = false;

static struct {
    char     url[512];
    uint32_t size;
    char     version[32];
} s_upgrade_info;

/* -----------------------------------------------------------------------
 * CRC32 增量更新（与 Bootloader 保持一致的 4-bit 查表法）
 * 初始值传入 0xFFFFFFFF，最终结果取反
 * ----------------------------------------------------------------------- */
static uint32_t crc32_update_acc(uint32_t crc, const uint8_t *buf, uint32_t len)
{
    static const uint32_t table[16] = {
        0x00000000,0x1DB71064,0x3B6E20C8,0x26D930AC,
        0x76DC4190,0x6B6B51F4,0x4DB26158,0x5005713C,
        0xEDB88320,0xF00F9344,0xD6D6A3E8,0xCB61B38C,
        0x9B64C2B0,0x86D3D2D4,0xA00AE278,0xBDBDF21C,
    };
    for (uint32_t i = 0; i < len; i++) {
        crc = (crc >> 4) ^ table[(crc ^ buf[i])        & 0x0F];
        crc = (crc >> 4) ^ table[(crc ^ (buf[i] >> 4)) & 0x0F];
    }
    return crc;
}

/* -----------------------------------------------------------------------
 * 解析阿里云 OTA 升级消息
 * 从 JSON 中提取 url、size、version
 * ----------------------------------------------------------------------- */
static bool parse_upgrade_json(const char *json)
{
    const char *p;

    /* 解析 url：strstr 找到 "url": 后，跳过6字节到达 value 的左引号 */
    p = strstr(json, "\"url\":");
    if (!p) return false;
    p += 6;  /* 跳过 "url": 共6字节，p 现在指向 value 左引号 */
    while (*p == ' ') p++;  /* 跳过可能的空格 */
    if (*p != '"') return false;
    p++;  /* 跳过左引号，p 指向 URL 内容 */
    char *end = strchr(p, '"');
    if (!end) return false;
    uint32_t ulen = end - p;
    if (ulen >= sizeof(s_upgrade_info.url)) return false;
    memcpy(s_upgrade_info.url, p, ulen);
    s_upgrade_info.url[ulen] = '\0';

    /* 将 https:// 降级为 http://，减少 TLS 开销，ESP32 AT 更稳定 */
    if (strncmp(s_upgrade_info.url, "https://", 8) == 0) {
        memmove(s_upgrade_info.url + 4, s_upgrade_info.url + 5,
                strlen(s_upgrade_info.url + 5) + 1);
    }

    /* 解析 size */
    p = strstr(json, "\"size\":");
    if (!p) return false;
    s_upgrade_info.size = (uint32_t)atoi(p + 7);

    /* 解析 version：跳过 "version": 共10字节到达 value 左引号 */
    p = strstr(json, "\"version\":");
    if (!p) return false;
    p += 10;
    while (*p == ' ') p++;
    if (*p != '"') return false;
    p++;
    end = strchr(p, '"');
    if (!end) return false;
    uint32_t vlen = end - p;
    if (vlen >= sizeof(s_upgrade_info.version)) return false;
    memcpy(s_upgrade_info.version, p, vlen);
    s_upgrade_info.version[vlen] = '\0';

    return true;
}

/* -----------------------------------------------------------------------
 * MQTT 订阅回调
 * 收到 upgrade topic 消息时触发，解析后设置 pending 标志
 * 注意：此函数在 ISR 上下文中调用，不能做耗时操作
 * ----------------------------------------------------------------------- */
static void on_mqtt_message(const char *topic, const char *payload, uint32_t len)
{
    (void)len;

    if (strstr(topic, "/ota/device/upgrade/") == NULL)
        return;

    printf("[ALIYUN OTA] Upgrade message received\r\n");

    if (parse_upgrade_json(payload)) {
        s_upgrade_pending = true;
        printf("[ALIYUN OTA] Version: %s, Size: %lu, URL: %s\r\n",
               s_upgrade_info.version,
               (unsigned long)s_upgrade_info.size,
               s_upgrade_info.url);
    } else {
        printf("[ALIYUN OTA] Failed to parse upgrade JSON\r\n");
    }
}

/* -----------------------------------------------------------------------
 * 上报下载进度到阿里云
 * step: 0~100 表示百分比，-1 表示失败，-2 表示成功
 * ----------------------------------------------------------------------- */
static void report_progress(int step, const char *desc)
{
    char payload[128];
    /* ESP32 AT+MQTTPUB payload 里的双引号需要用 \" 转义
     * snprintf 里 \\\" 输出为 \" 是正确的 */
    snprintf(payload, sizeof(payload),
             "{\"id\":\"1\",\"params\":{\"step\":\"%d\",\"desc\":\"%s\"}}",
             step, desc);
    mqtt_at_publish(TOPIC_PROGRESS, payload, 0);
}

/* -----------------------------------------------------------------------
 * 进度回调：每 10% 上报一次
 * ----------------------------------------------------------------------- */
static int s_last_pct = -1;
static void on_download_progress(uint32_t received, uint32_t total)
{
    int pct = (int)(received * 100 / total);
    if (pct / 10 != s_last_pct / 10) {
        s_last_pct = pct;
        /* 下载期间不发 MQTT，避免抢占 USART2 干扰 HTTP 下载 */
        printf("[ALIYUN OTA] %d%%  (%lu/%lu)\r\n",
               pct, (unsigned long)received, (unsigned long)total);
    }
}

/* -----------------------------------------------------------------------
 * 下载固件到 W25Q32 下载区
 * ----------------------------------------------------------------------- */
static bool download_firmware(const char *url, uint32_t fw_size)
{
    /* 擦除下载区 */
    uint32_t sectors = (fw_size + W25Q32_SECTOR_SIZE - 1) / W25Q32_SECTOR_SIZE;
    printf("[ALIYUN OTA] Erasing %lu sectors...\r\n", (unsigned long)sectors);
    for (uint32_t i = 0; i < sectors; i++)
        w25q32_erase_sector(W25Q32_OTA_FW_ADDR + i * W25Q32_SECTOR_SIZE);

    printf("[ALIYUN OTA] Downloading %lu bytes...\r\n", (unsigned long)fw_size);
    s_last_pct = -1;

    /* 流式下载，直接写入 W25Q32 */
    uint32_t written = http_ota_download(url, fw_size, W25Q32_OTA_FW_ADDR, on_download_progress);
    if (written != fw_size) {
        printf("[ALIYUN OTA] Download failed: got %lu, expected %lu\r\n",
               (unsigned long)written, (unsigned long)fw_size);
        report_progress(-1, "download failed");
        return false;
    }

    /* 计算 CRC32：从 W25Q32 读回数据校验 */
    printf("[ALIYUN OTA] Verifying CRC32...\r\n");
    uint32_t crc_acc = 0xFFFFFFFF;
    uint8_t  buf[256];
    uint32_t offset = 0;
    while (offset < fw_size) {
        uint32_t chunk = fw_size - offset;
        if (chunk > sizeof(buf)) chunk = sizeof(buf);
        w25q32_read(W25Q32_OTA_FW_ADDR + offset, buf, chunk);
        crc_acc = crc32_update_acc(crc_acc, buf, chunk);
        offset += chunk;
    }

    /* 写 OTA flag，触发 Bootloader 升级 */
    w25q32_ota_flag_t flag;
    flag.magic    = W25Q32_OTA_FLAG_MAGIC;
    flag.fw_size  = fw_size;
    flag.fw_crc32 = ~crc_acc;
    flag.version  = (uint32_t)atoi(s_upgrade_info.version);
    w25q32_ota_write_flag(&flag);

    printf("[ALIYUN OTA] Download complete. CRC32=0x%08lX\r\n",
           (unsigned long)flag.fw_crc32);
    report_progress(100, "download complete");
    return true;
}

/* -----------------------------------------------------------------------
 * 公开接口实现
 * ----------------------------------------------------------------------- */

bool aliyun_ota_connect(void)
{
    /*
     * 阿里云 MQTT 三元组认证
     * ClientID 格式：${DeviceName}|securemode=3,signmethod=hmacsha256,timestamp=${ts}|
     * Username 格式：${DeviceName}&${ProductKey}
     * Password：HMAC-SHA256 签名（测试阶段可用工具预先生成后硬编码）
     *
     * 生成工具：https://iot.console.aliyun.com/product 设备详情页 → 一键生成
     * 或使用阿里云提供的 Python 脚本生成
     */
    mqtt_config_t cfg = {
        .host      = ALIYUN_MQTT_HOST,
        .port      = ALIYUN_MQTT_PORT,
        .client_id = "k1qu5q0WFBN.D001|securemode=2,signmethod=hmacsha256,timestamp=1773630140918|",
        .username  = "D001&k1qu5q0WFBN",
        .password  = "8eaeb69ff8f75dddb6b58317d8cf1c599908e8f86e96a9f557d365b84f26c0a5",
        .keepalive = 60,
    };

    /* 注册订阅消息回调 */
    mqtt_at_set_sub_callback(on_mqtt_message);

    if (!mqtt_at_connect(&cfg)) {
        printf("[ALIYUN OTA] MQTT connect failed\r\n");
        return false;
    }

    /* 订阅 OTA 升级 Topic，QoS 1 保证消息不丢失 */
    if (!mqtt_at_subscribe(TOPIC_UPGRADE, 1)) {
        printf("[ALIYUN OTA] Subscribe failed\r\n");
        return false;
    }

    /* 连接成功后立即上报当前版本，触发阿里云检查是否有待推送的升级 */
    if (!aliyun_ota_report_version())
        return false;

    /* 主动拉取固件信息：适用于"验证中"状态
     * 阿里云收到后会通过 TOPIC_UPGRADE 推送待升级固件信息（如果有的话）
     * payload 格式固定：{"id":"1","version":"1.0"} */
    uint32_t ver = ota_get_current_version();
    char pull_payload[64];
    snprintf(pull_payload, sizeof(pull_payload),
             "{\"id\":\"1\",\"version\":\"%lu\"}", (unsigned long)ver);
    printf("[ALIYUN OTA] Pulling firmware info...\r\n");
    mqtt_at_publish(TOPIC_GET, pull_payload, 0);

    return true;
}

bool aliyun_ota_report_version(void)
{
    uint32_t ver = ota_get_current_version();
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"id\":\"1\",\"params\":{\"version\":\"%lu\"}}",
             (unsigned long)ver);
    printf("[ALIYUN OTA] Reporting version: %lu\r\n", (unsigned long)ver);
    return mqtt_at_publish(TOPIC_INFORM, payload, 0);
}

bool aliyun_ota_process(void)
{
    if (!s_upgrade_pending)
        return false;

    s_upgrade_pending = false;

    printf("[ALIYUN OTA] Starting upgrade to version %s\r\n", s_upgrade_info.version);

    if (s_upgrade_info.size == 0 || s_upgrade_info.size > W25Q32_OTA_FW_MAX_SIZE) {
        printf("[ALIYUN OTA] Invalid firmware size: %lu\r\n",
               (unsigned long)s_upgrade_info.size);
        return false;
    }

    /* 暂停所有定时器任务，独占 USART2 进行 HTTP 下载 */
    main_loop_suspend();

    bool ok = download_firmware(s_upgrade_info.url, s_upgrade_info.size);

    main_loop_resume();

    if (!ok)
        return false;

    printf("[ALIYUN OTA] Rebooting to apply update...\r\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    NVIC_SystemReset();

    return true; /* 不会执行到这里 */
}
