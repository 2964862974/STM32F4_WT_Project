#include "st7789.h"
#include "font.h"
#include "image.h"

void error_page_display(const char *msg)
{
    const uint16_t color_background = mkcolor(0, 0, 0);
    st7789_fill_color(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1, color_background);
    st7789_draw_image(40, 37, &img_error);
    st7789_write_string(10, 197, "Error:", mkcolor(255,0,0), color_background, &font_32_maple_bold);
    st7789_write_string(10, 233, msg, mkcolor(255, 255, 255), color_background, &font_16_maple);
}
