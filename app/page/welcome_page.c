#include "ui.h"

void welcome_page_display(void)
{
    const uint16_t color_background = mkcolor(0, 0, 0);
    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, color_background);
    ui_draw_image(30, 10, &img_meihua);
    ui_write_string(40, 205, "÷Ƕʽ", mkcolor(237,128,147), color_background, &font_32_maple_bold);
    ui_write_string(56, 233, "ʱ", mkcolor(86,165,255), color_background, &font_32_maple_bold);
    ui_write_string(60, 275, "Loading...", mkcolor(255, 255, 255), color_background, &font_24_maple);
    // ui_write_string(20, 300, version, mkcolor(127, 127, 127), color_background, &font_16_maple);
}
