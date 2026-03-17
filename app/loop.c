#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rtc.h"
#include "cpu_tick.h"
#include "aht20.h"
#include "esp_at.h"
#include "weather.h"
#include "pages.h"
#include "app.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#define MILLISECONDS(x) (x)
#define SECONDS(x) ((x) * 1000)
#define MINUTES(x) (SECONDS(x) * 60)
#define HOURS(x) (MINUTES(x) * 60)
#define DAYS(x) (HOURS(x) * 24)

#define TIME_SYNC_INTERVAL HOURS(1)
#define WIFI_UPDATE_INTERVAL SECONDS(5)
#define TIME_UPDATE_INTERVAL MILLISECONDS(50)
#define OUTER_UPDATE_INTERVAL MINUTES(10)
#define INNER_UPDATE_INTERVAL SECONDS(10)
#define MLOOP_EVT_TIME_SYNC (1 << 0)
#define MLOOP_EVT_WIFI_UPDATE (1 << 1)
#define MLOOP_EVT_TIME_UPDATE (1 << 2)
#define MLOOP_EVT_OUTER_UPDATE (1 << 3)
#define MLOOP_EVT_INNER_UPDATE (1 << 4)
#define MLOOP_EVT_ALL (MLOOP_EVT_TIME_SYNC | MLOOP_EVT_WIFI_UPDATE | MLOOP_EVT_TIME_UPDATE | MLOOP_EVT_OUTER_UPDATE | MLOOP_EVT_INNER_UPDATE)
TaskHandle_t main_loop_task_handle;
static TimerHandle_t time_sync_handler;
static TimerHandle_t wifi_update_handler;
static TimerHandle_t time_update_handler;
static TimerHandle_t outer_update_handler;
static TimerHandle_t inner_update_handler;
static void time_sync(void)
{
    uint32_t restart_sync_delay = TIME_SYNC_INTERVAL;

    rtc_date_time_t rtc_date = {0};
    esp_date_time_t esp_date = {0};
    if (!esp_at_sntp_get_time(&esp_date))
    {
        printf("[SNTP] get time failed\n");
        restart_sync_delay = SECONDS(1);
        goto err;
    }

    if (esp_date.year <= 2000)
    {
        printf("[SNTP] invalid date received\n");
        restart_sync_delay = SECONDS(1);
        goto err;
    }

    printf("[SNTP] sync time: %04u-%02u-%02u %02u:%02u:%02u (%d)\n",
           esp_date.year, esp_date.month, esp_date.day,
           esp_date.hour, esp_date.minute, esp_date.second, esp_date.weekday);


    rtc_date.year = esp_date.year;
    rtc_date.month = esp_date.month;
    rtc_date.day = esp_date.day;
    rtc_date.hour = esp_date.hour;
    rtc_date.minute = esp_date.minute;
    rtc_date.second = esp_date.second;
    rtc_date.weekday = esp_date.weekday;
    rtc_set_time(&rtc_date);
err://���۳ɹ���񶼻�ִ������������ö�ʱ�����ɹ��˾�1h�󴥷���ʧ���˾�1s������
    xTimerChangePeriod(time_sync_handler, pdMS_TO_TICKS(restart_sync_delay), 0);
    xTaskNotify(main_loop_task_handle, MLOOP_EVT_TIME_UPDATE, eSetBits);//UI��Ҫ����ʱ�䣬����ֱ�Ӵ���ʱ������¼�
}

static void wifi_update(void)
{
    static esp_wifi_info_t last_wifi_info = {0};
    xTimerChangePeriod(wifi_update_handler, pdMS_TO_TICKS(WIFI_UPDATE_INTERVAL), 0);
    esp_wifi_info_t wifi = {0};
    if (!esp_at_get_wifi_info(&wifi))
    {
        printf("[AT] wifi info get failed\n");
        return;
    }

    if (memcmp(&wifi, &last_wifi_info, sizeof(esp_wifi_info_t)) == 0)
    {
        return;
    }

    if (last_wifi_info.connected == wifi.connected)
    {
        return;
    }

    if (wifi.connected)
    {
        printf("[WIFI] connected to %s\n", wifi.ssid);
        printf("[WIFI] SSID: %s, BSSID: %s, Channel: %d, RSSI: %d\n",
               wifi.ssid, wifi.bssid, wifi.channel, wifi.rssi);
        main_page_redraw_wifi_ssid(wifi.ssid);
    }
    else
    {
        printf("[WIFI] disconnected from %s\n", last_wifi_info.ssid);
        main_page_redraw_wifi_ssid("wifi lost");
    }

    memcpy(&last_wifi_info, &wifi, sizeof(esp_wifi_info_t));
    //��ǰwifi�����һ��wifi���Դ�ѭ��
}

static void time_update(void)
{
    static rtc_date_time_t last_date = {0};

    xTimerChangePeriod(time_update_handler, pdMS_TO_TICKS(TIME_UPDATE_INTERVAL), 0);
    rtc_date_time_t date;
    memset(&date, 0, sizeof(rtc_date_time_t));
    rtc_get_time(&date);

    if (memcmp(&date, &last_date, sizeof(rtc_date_time_t)) == 0)
    {
        return;
    }

    memcpy(&last_date, &date, sizeof(rtc_date_time_t));
    main_page_redraw_time(&date);
    main_page_redraw_date(&date);
}

static void inner_update(void)
{
    static float last_inner_temperature = 0.0f;
    static float last_inner_humidity = 0.0f;
    xTimerChangePeriod(inner_update_handler, pdMS_TO_TICKS(INNER_UPDATE_INTERVAL), 0);
    if (!aht20_start_measurement())
    {
        printf("[AHT20] start measurement failed\n");
        return;
    }

    if (!aht20_wait_for_measurement())
    {
        printf("[AHT20] wait for measurement failed\n");
        return;
    }

    float temperature = 0.0f;
    float humidity = 0.0f;
    aht20_wait_for_measurement();
    if (!aht20_read_measurement(&temperature, &humidity))
    {
        printf("[AHT20] get data failed\n");
        return;
    }

    if (temperature == last_inner_temperature && humidity == last_inner_humidity)
    {
        return;
    }

    last_inner_temperature = temperature;
    last_inner_humidity = humidity;

    printf("[AHT20] Temperature: %.1f, Humidity: %.1f\n", temperature, humidity);
    main_page_redraw_inner_temperature(temperature);
    main_page_redraw_inner_humidity(humidity);
}

static void outer_update(void)
{
    static weather_info_t last_weather = {0};

    xTimerChangePeriod(outer_update_handler, pdMS_TO_TICKS(OUTER_UPDATE_INTERVAL), 0);
    weather_info_t weather = {0};
    const char *weather_http_response = esp_at_http_get(weather_url);
    if (weather_http_response == NULL)
    {
        printf("[WEAHTER] http error\n");
        return;
    }

    if (!parse_seniverse_response(weather_http_response, &weather))
    {
        printf("[WEAHTER] parse failed\n");
        return;
    }

    if (memcmp(&weather, &last_weather, sizeof(weather_info_t)) == 0)
    {
        return;
    }

    memcpy(&last_weather, &weather, sizeof(weather_info_t));
    printf("[WEATHER] %s, %s, %.1f\n", weather.city, weather.weather, weather.temperature);
    main_page_redraw_outdoor_temperature(weather.temperature);
    main_page_redraw_outdoor_weather_icon(weather.weather_code);
}


static void main_loop_task(void *param)
{
    (void)param;
    uint32_t event = 0;

    while (1)
    {
        event = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if(event&MLOOP_EVT_TIME_SYNC)
        {
            time_sync();
        }
        if(event&MLOOP_EVT_WIFI_UPDATE)
        {
            wifi_update();
        }
        if(event&MLOOP_EVT_TIME_UPDATE)
        {
            time_update();
        }
        if(event&MLOOP_EVT_OUTER_UPDATE)
        {
            outer_update();
        }
        if(event&MLOOP_EVT_INNER_UPDATE)
        {
            inner_update();
        }
    }
}

static void mloop_timer_callback(TimerHandle_t xTimer){
    uint32_t event = (uint32_t)pvTimerGetTimerID(xTimer);
    xTaskNotify(main_loop_task_handle, event, eSetBits);
}
void main_loop_init(void)
{
    time_sync_handler = xTimerCreate("time sync", 1, pdFALSE, (void *)MLOOP_EVT_TIME_SYNC, mloop_timer_callback);
    wifi_update_handler = xTimerCreate("wifi update", pdMS_TO_TICKS(WIFI_UPDATE_INTERVAL), pdTRUE, (void *)MLOOP_EVT_WIFI_UPDATE, mloop_timer_callback);
    time_update_handler = xTimerCreate("time update", pdMS_TO_TICKS(TIME_UPDATE_INTERVAL), pdTRUE, (void *)MLOOP_EVT_TIME_UPDATE, mloop_timer_callback);
    outer_update_handler = xTimerCreate("outer update", pdMS_TO_TICKS(OUTER_UPDATE_INTERVAL), pdTRUE, (void *)MLOOP_EVT_OUTER_UPDATE, mloop_timer_callback);
    inner_update_handler = xTimerCreate("inner update", pdMS_TO_TICKS(INNER_UPDATE_INTERVAL), pdTRUE, (void *)MLOOP_EVT_INNER_UPDATE, mloop_timer_callback);
    xTaskCreate(main_loop_task, "main_loop", 1024, NULL, 5, &main_loop_task_handle);
    xTaskNotify(main_loop_task_handle, MLOOP_EVT_ALL, eSetBits);
}

void main_loop_suspend(void)
{
    xTimerStop(wifi_update_handler,  0);
    xTimerStop(outer_update_handler, 0);
    xTimerStop(inner_update_handler, 0);
    xTimerStop(time_sync_handler,    0);
    xTimerStop(time_update_handler,  0);
}

void main_loop_resume(void)
{
    xTimerStart(wifi_update_handler,  0);
    xTimerStart(outer_update_handler, 0);
    xTimerStart(inner_update_handler, 0);
    xTimerStart(time_sync_handler,    0);
    xTimerStart(time_update_handler,  0);
}
