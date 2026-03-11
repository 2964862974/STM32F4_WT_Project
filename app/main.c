#include <stdbool.h>
#include <stdio.h>
#include "app.h"
#include "pages.h"
#include "FreeRTOS.h"
#include "task.h"
#include "ui.h"
extern void board_lowlevel_init(void);
extern void board_init(void);

static void main_init(void* param)
{
    (void)param;
    board_init();
    ui_init();
    welcome_page_display();

    esp32_app_init();
    wifi_page_display();
    esp32_wait_wifi_ready();


    main_page_display();
    main_loop_init();
    vTaskDelete(NULL);//删除main_init任务，不会删除创建的其他任务

}
int main(void)
{
    board_lowlevel_init();
    xTaskCreate(main_init, "main_init", 1024, NULL, 9, NULL);
    vTaskStartScheduler();//开始执行main_init任务
    while(1)
    {
        ;//data should not reach here
    }

}
void vApplicationMallocFailedHook(void)
{
    printf("Malloc failed!\r\n");
    taskDISABLE_INTERRUPTS();
    while(1)
    {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    printf("Stack overflow! Task: %s\r\n", pcTaskName);
    (void)xTask;

    taskDISABLE_INTERRUPTS();
    while(1)
    {
    }
}

void vAssertCalled(const char *file, int line)
{
    printf("Assert failed! File: %s, Line: %d\r\n", file, line);

    taskDISABLE_INTERRUPTS();
    while(1)
    {
    }
}
