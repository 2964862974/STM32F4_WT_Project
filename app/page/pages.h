#ifndef __PAGES_H__
#define __PAGES_H__

#include "rtc.h"

void welcome_page_display(void);
void wifi_page_display(void);
void error_page_display(const char *msg);
void main_page_display(void);
void main_page_redraw_wifi_ssid(const char *ssid);
void main_page_redraw_time(const rtc_date_time_t *time);
void main_page_redraw_date(const rtc_date_time_t *time);
void main_page_redraw_inner_temperature(const float temperature);
void main_page_redraw_inner_humidity(const float humidity);
void main_page_redraw_outdoor_temperature(const float temperature);
void main_page_redraw_outdoor_weather_icon(const int code);

#endif /* __PAGES_H__ */
