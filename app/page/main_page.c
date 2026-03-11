#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "rtc.h"
#include "ui.h"
#include "app.h"
#include "pages.h"

static const uint16_t color_black = mkcolor(0, 0, 0);
static const uint16_t color_bg_time = mkcolor(255, 255, 255);
static const uint16_t color_bg_inner = mkcolor(136, 217, 234);
static const uint16_t color_bg_outdoor = mkcolor(253, 135, 75);

void main_page_display(void)
{
    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, color_black);

    do {
        ui_fill_color(15, 15, 224, 154, color_bg_time);
        ui_draw_image(23, 20, &icon_wifi);

        const rtc_date_time_t date = { 2000, 1, 1, 8, 0, 0, 6 };
        main_page_redraw_wifi_ssid(wifi_ssid);
        main_page_redraw_time(&date);
        main_page_redraw_date(&date);
    } while (0);

    do {
        ui_fill_color(15, 165, 114, 304, color_bg_inner);
        ui_write_string(19, 170, "室内环境", color_black, color_bg_inner, &font_24_maple_bold);
        ui_write_string(76, 197, "℃", color_black, color_bg_inner, &font_32_maple_bold);
        ui_write_string(91, 266, "%", color_black, color_bg_inner, &font_32_maple_bold);

        main_page_redraw_inner_temperature(0);
        main_page_redraw_inner_humidity(0);
    } while (0);

    do {
        ui_fill_color(125, 165, 224, 304, color_bg_outdoor);
        ui_write_string(127, 170, "合肥", color_black, color_bg_outdoor, &font_24_maple_bold);
        ui_write_string(192, 200, "℃", color_black, color_bg_outdoor, &font_32_maple_bold);
        ui_draw_image(134, 249, &icon_wenduji);

        main_page_redraw_outdoor_temperature(0);
        main_page_redraw_outdoor_weather_icon(0);
    } while (0);
}

void main_page_redraw_wifi_ssid(const char *ssid)
{
    int ssid_width = strlen(ssid) * font_16_maple.size / 2;
    int ssid_x = 170 - ssid_width;
    if (ssid_x < 0) ssid_x = 0;
    ui_fill_color(50, 20, 224, 40, color_bg_time);
    ui_write_string(50 + ssid_x, 23, ssid, mkcolor(143,143,143), color_bg_time, &font_16_maple);
}

void main_page_redraw_time(const rtc_date_time_t *time)
{
    char str[6];
    char comma = (time->second % 2 == 0) ? ':' : ' ';
    snprintf(str, sizeof(str), "%02u%c%02u", time->hour, comma, time->minute);
    ui_write_string(25 ,42, str, color_black, color_bg_time, &font_76_maple_bold);
}

void main_page_redraw_date(const rtc_date_time_t *time)
{
    char date[18];
    snprintf(date, sizeof(date), "%04u/%02u/%02u 星期%s", time->year, time->month, time->day,
             time->weekday == 1 ? "一" :
             time->weekday == 2 ? "二" :
             time->weekday == 3 ? "三" :
             time->weekday == 4 ? "四" :
             time->weekday == 5 ? "五" :
             time->weekday == 6 ? "六" :
             time->weekday == 7 ? "天" : "X");
    ui_write_string(18, 121, date, mkcolor(143,143,143), color_bg_time, &font_24_maple);
}

void main_page_redraw_inner_temperature(const float temperature)
{
    float temper = temperature;
    char str[3] = {'-', '-'};
    if (temper > 0 && temper < 100)
        snprintf(str, sizeof(str), "%2.0f", temper);
    ui_write_string(26, 198, str, color_black, mkcolor(136, 217, 234), &font_54_maple_medium);
}

void main_page_redraw_inner_humidity(const float humidity)
{
    float humi = humidity;
    char str[3] = {'-', '-'};
    if (humi > 0 && humi < 100)
        snprintf(str, sizeof(str), "%2.0f", humi);
    ui_write_string(28, 243, str, color_black, mkcolor(136, 217, 234), &font_62_maple_bold);
}

void main_page_redraw_outdoor_temperature(const float temperature)
{
    char str[3] = {'-', '-'};
    if (temperature > 0 && temperature < 100)
        snprintf(str, sizeof(str), "%02.0f", temperature);
    ui_write_string(140, 196, str, color_black, mkcolor(253, 135, 75), &font_54_maple_medium);
}

void main_page_redraw_outdoor_weather_icon(const int code)
{
    const image_t *icon = NULL;
    if (code == 0 || code == 2 || code == 38)
        icon = &icon_qing;
    else if (code == 1 || code == 3)
        icon = &icon_yueliang;
    else if (code == 4 || code == 9)
        icon = &icon_yintian;
    else if (code == 5 || code == 6 || code == 7 || code == 8)
        icon = &icon_duoyun;
    else if (code == 10 || code == 13 || code == 14 || code == 15 || code == 16 || code == 17 || code == 18 || code == 19)
        icon = &icon_zhongyu;
    else if (code == 11 || code == 12)
        icon = &icon_leizhenyu;
    else if (code == 20 || code == 21 || code == 22 || code == 23 || code == 24 || code == 25)
        icon = &icon_zhongxue;
    else // 扬沙、龙卷风等
        icon = &icon_qing; // Default to clear weather icon

    ui_draw_image(166, 240, icon);
}
