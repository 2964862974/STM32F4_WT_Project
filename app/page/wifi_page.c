#include <stdint.h>
#include <string.h>
#include "st7789.h"
#include "font.h"
#include "image.h"
#include "app.h"

void wifi_page_display(void)
{
    const uint16_t color_background = mkcolor(0, 0, 0);
    st7789_fill_color(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1, color_background);
    st7789_draw_image(30, 15, &img_wifi);
    st7789_write_string(88, 191, "WiFi", mkcolor(0,255,234), color_background, &font_32_maple_bold);
    int ssid_width = strlen(wifi_ssid) * font_24_maple.size / 2;
    int ssid_x = (ST7789_WIDTH - ssid_width + 1) / 2;
    if (ssid_x < 0) ssid_x = 0;
    st7789_write_string(ssid_x, 231, wifi_ssid, mkcolor(255, 255, 255), color_background, &font_24_maple);
    st7789_write_string(72, 275, "Á¬½ÓÖÐ", mkcolor(148,198,255), color_background, &font_32_maple_bold);
}
