#ifndef __FONT_H
#define __FONT_H

// !"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_`abcdefghijklmnopqrstuvwxyz{|}~

#include <stdint.h>

typedef struct
{
    const char *name;
    const uint8_t *model;
} font_chinese_t;

typedef struct
{
    const uint8_t *ascii_model;
    const char *ascii_map;
    const font_chinese_t *chinese;
    uint16_t size;
} font_t;

extern const font_t font_16_maple;
extern const font_t font_24_maple;
extern const font_t font_24_maple_bold;
extern const font_t font_32_maple_bold;
extern const font_t font_42_maple;
extern const font_t font_54_maple_medium;
extern const font_t font_62_maple_bold;
extern const font_t font_76_maple_bold;

#endif /* __FONT_H */
