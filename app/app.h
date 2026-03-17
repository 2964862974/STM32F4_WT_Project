#ifndef __APP_H
#define __APP_H

#include "image.h"

static const char *version = "v1.0.0";
static const char *weather_url = "https://api.seniverse.com/v3/weather/now.json?key=SfRic8Wmp-Qh3OeFk&location=WTEMH46Z5N09&language=en&unit=c";
static const char *wifi_ssid = "hjt123";
static const char *wifi_password = "hjt1234567";

void esp32_app_init(void);
void esp32_wait_wifi_ready(void);

void main_loop_init(void);
void main_loop(void);
void main_loop_suspend(void);
void main_loop_resume(void);

#endif /* __APP_H */
