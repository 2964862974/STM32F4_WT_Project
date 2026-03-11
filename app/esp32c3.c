#include <stddef.h>
#include <stdio.h>
#include "esp_at.h"
#include "cpu_tick.h"
#include "FreeRTOS.h"
#include "app.h"
#include "task.h"
#include "pages.h"

void esp32_app_init(void)
{
    if (!esp_at_init())
    {
        printf("[AT] init failed\n");
        goto err;
    }
    printf("[AT] inited\n");

    if (!esp_at_wifi_init())
    {
        printf("[WIFI] init failed\n");
        goto err;
    }
    printf("[WIFI] inited\n");

    if (!esp_at_connect_wifi(wifi_ssid, wifi_password, NULL))
    {
        printf("[WIFI] connect failed\n");
        goto err;
    }
    printf("[WIFI] connecting\n");

    if (!esp_at_sntp_init())
    {
        printf("[SNTP] init failed\n");
        goto err;
    }
    printf("[SNTP] inited\n");

    return;

err:
    error_page_display("Initialization Failed");
    while (1)
    {
        ;
    }
}

void esp32_wait_wifi_ready(void)
{
    for (uint32_t t = 0; t < 10 * 1000; t += 100)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_wifi_info_t wifi = { 0 };
        if (esp_at_get_wifi_info(&wifi) && wifi.connected)
        {
            printf("[WIFI] Connected to SSID: %s, BSSID: %s, Channel: %d, RSSI: %d\n",
                   wifi.ssid, wifi.bssid, wifi.channel, wifi.rssi);
            return;
        }
    }

    printf("[WIFI] Connection timeout\n");

    error_page_display("Connection Timeout");
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
