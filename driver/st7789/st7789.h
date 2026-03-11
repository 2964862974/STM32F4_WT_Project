#ifndef __ST7789_H
#define __ST7789_H

#include <stdbool.h>
#include <stdint.h>
#include "font.h"
#include "image.h"

#define ST7789_WIDTH  240
#define ST7789_HEIGHT 320

#define mkcolor(r, g, b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))

void st7789_init(void);
void st7789_init_display(void);
void st7789_set_backlight(bool on);
void st7789_output_lock(void);
void st7789_output_unlock(void);
void st7789_fill_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t color);
void st7789_draw_image(uint16_t x, uint16_t y, const image_t *image);
void st7789_write_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color, const font_t *font);

#endif /* __ST7789_H */
