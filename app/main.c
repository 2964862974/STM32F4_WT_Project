#include <stdbool.h>
#include <stdio.h>
#include "app.h"
#include "pages.h"
#include "FreeRTOS.h"
#include "task.h"
#include "ui.h"
#include "iwdg.h"
#include "w25q32.h"
#include "aliyun_ota.h"

extern void board_lowlevel_init(void);
extern void board_init(void);

/* 喂狗任务：每 5 秒喂一次，看门狗超�? 32 秒，留足余量 */
static void wdt_feed_task(void *param)
{
    (void)param;
    while (1) {
        iwdg_feed();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* OTA 任务：连接阿里云 MQTT，上报版�?，等待升级通知 */
static void aliyun_ota_task(void *param)
{
    (void)param;
    printf("[OTA] Task started, waiting 3s for WiFi...\n");
    vTaskDelay(pdMS_TO_TICKS(3000));
    printf("[OTA] Connecting to Aliyun MQTT...\n");

    if (!aliyun_ota_connect()) {
        printf("[OTA] Aliyun MQTT connect failed, OTA disabled\n");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        aliyun_ota_process();
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

static void main_init(void *param)
{
    (void)param;
    board_init();

    /* 写入 PENDING：告�? Bootloader �?次启动尚�?�?认稳�?
     * 若看门狗超时复位，Bootloader 检测到 PENDING 会自动回�? */
    w25q32_boot_confirm_write(W25Q32_BOOT_CONFIRM_STATE_PENDING);
    printf("[SYS] Boot123  confirm: PENDING\n");

    /* �?动看门狗，从此刻起必须定期喂�? */
    iwdg_init();

    ui_init();
    welcome_page_display();
    esp32_app_init();
    wifi_page_display();
    esp32_wait_wifi_ready();

    main_page_display();
    main_loop_init();

    /* 所有初始化完成，系统稳定，写入 CONFIRMED */
    w25q32_boot_confirm_write(W25Q32_BOOT_CONFIRM_STATE_CONFIRMED);
    printf("[SYS] Boot1 confirm: CONFIRMED\n");

    xTaskCreate(wdt_feed_task,   "wdt", 128,  NULL, 1, NULL);
    xTaskCreate(aliyun_ota_task, "ota", 1024, NULL, 2, NULL);

    vTaskDelete(NULL);
}

int main(void)
{
    __enable_irq();
    board_lowlevel_init();
    xTaskCreate(main_init, "main_init", 2048, NULL, 9, NULL);
    vTaskStartScheduler();
    while (1) { ; }
}

void vApplicationMallocFailedHook(void)
{
    printf("Malloc failed!\r\n");
    taskDISABLE_INTERRUPTS();
    while (1) { }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    printf("Stack overflow! Task: %s\r\n", pcTaskName);
    (void)xTask;
    taskDISABLE_INTERRUPTS();
    while (1) { }
}

void vAssertCalled(const char *file, int line)
{
    printf("Assert failed! File: %s, Line: %d\r\n", file, line);
    taskDISABLE_INTERRUPTS();
    while (1) { }
}
