#include "st7789.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "ui.h"
#include "string.h"
#include <stdio.h>
static QueueHandle_t ui_queue;
typedef enum
{
    UI_ACTION_FILL_COLOR,
    UI_ACTION_WRITE_STRING,
    UI_ACTION_DRAW_IMAGE,
} ui_action_t;
typedef struct
{
    ui_action_t action;
    union
    {
        struct
        {
            uint16_t x1, y1, x2, y2;
            uint16_t color;
        } fill_color;
        struct
        {
            uint16_t x, y;
            char str[64];
            uint16_t color;
            uint16_t bg_color;
            const font_t *font;
        } write_string;
        struct
        {
            uint16_t x, y;
            const image_t *image;
        } draw_image;
    };
} ui_message_t;//用联合体节省内存
static void ui_func(void *param)
{
    ui_message_t ui_msg;
    st7789_init();
    while(1){
        xQueueReceive(ui_queue, &ui_msg, portMAX_DELAY);
        switch (ui_msg.action)
        {   case UI_ACTION_FILL_COLOR:
            st7789_fill_color(ui_msg.fill_color.x1, ui_msg.fill_color.y1, ui_msg.fill_color.x2, ui_msg.fill_color.y2, ui_msg.fill_color.color);
            break;
        case UI_ACTION_WRITE_STRING:
            st7789_write_string(ui_msg.write_string.x, ui_msg.write_string.y, ui_msg.write_string.str, ui_msg.write_string.color, ui_msg.write_string.bg_color, ui_msg.write_string.font);
            break;
        case UI_ACTION_DRAW_IMAGE:
            st7789_draw_image(ui_msg.draw_image.x, ui_msg.draw_image.y, ui_msg.draw_image.image);
            break;
        default:
            printf("Unknown UI action: %d\r\n", ui_msg.action);
            break;
        }
    }
}
void ui_init(void)
{
    ui_queue = xQueueCreate(16, sizeof(ui_message_t));
    configASSERT(ui_queue);
    xTaskCreate(ui_func, "ui_func", 2048, NULL, 8, NULL);

}

void ui_write_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color, const font_t *font)
{

    ui_message_t msg;
    msg.action = UI_ACTION_WRITE_STRING;
    msg.write_string.x = x;
    msg.write_string.y = y;
    strncpy(msg.write_string.str, str, sizeof(msg.write_string.str) - 1);
    msg.write_string.str[sizeof(msg.write_string.str) - 1] = '\0'; // 强行封口，防爆栈
    msg.write_string.color = color;
    msg.write_string.bg_color = bg_color;
    msg.write_string.font = font;
    xQueueSend(ui_queue, &msg, 0);
}
void ui_draw_image(uint16_t x, uint16_t y, const image_t *image)
{
    ui_message_t msg;
    msg.action = UI_ACTION_DRAW_IMAGE;
    msg.draw_image.x = x;
    msg.draw_image.y = y;
    msg.draw_image.image = image;
    xQueueSend(ui_queue, &msg, 0);
}
void ui_fill_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    ui_message_t msg;
    msg.action = UI_ACTION_FILL_COLOR;
    msg.fill_color.x1 = x1;
    msg.fill_color.y1 = y1;
    msg.fill_color.x2 = x2;
    msg.fill_color.y2 = y2;
    msg.fill_color.color = color;
    xQueueSend(ui_queue, &msg, 0);
}
