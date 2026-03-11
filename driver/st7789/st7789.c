#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "stm32f4xx.h"
#include "delay.h"
#include "st7789.h"
#include "font.h"
#include "image.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
// CS —— PE2
// RESET —— PE3
// DC —— PE4
// BL —— PE5
// SCK —— PB13
// MOSI —— PC3
// MISO —— PC2
static QueueHandle_t write_gram_semaphore;
#define CS_PORT GPIOE
#define CS_PIN GPIO_Pin_2
#define RESET_PORT GPIOE
#define RESET_PIN GPIO_Pin_3
#define DC_PORT GPIOE
#define DC_PIN GPIO_Pin_4
#define BL_PORT GPIOE
#define BL_PIN GPIO_Pin_5

#define delay_us(x) time_delay_us(x)
#define delay_ms(x) time_delay_ms(x)
static void st7789_io_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_StructInit(&GPIO_InitStructure);
    GPIO_SetBits(GPIOE, GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOE, &GPIO_InitStructure);

    GPIO_PinAFConfig(GPIOB, GPIO_PinSource13, GPIO_AF_SPI2);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource2, GPIO_AF_SPI2);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource3, GPIO_AF_SPI2);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_Init(GPIOC, &GPIO_InitStructure);
}
static void st7789_spi_init(void)
{
    SPI_InitTypeDef SPI_InitStructure;
    SPI_StructInit(&SPI_InitStructure);
    SPI_InitStructure.SPI_Direction = SPI_Direction_1Line_Tx;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_Init(SPI2, &SPI_InitStructure);
	SPI_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, ENABLE);
    SPI_Cmd(SPI2, ENABLE);
}
static void st7789_dma_init(void)
{
    DMA_InitTypeDef DMA_InitStructure;
    DMA_StructInit(&DMA_InitStructure);
    DMA_InitStructure.DMA_Channel = DMA_Channel_0;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&SPI2->DR;
    DMA_InitStructure.DMA_DIR = DMA_DIR_MemoryToPeripheral;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Enable;
    DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_INC8;
    DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_ITConfig(DMA1_Stream4, DMA_IT_TC, ENABLE);
    DMA_Init(DMA1_Stream4, &DMA_InitStructure);
}
static void st7789_int_init(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Stream4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

}

void st7789_init(void)
{
    write_gram_semaphore = xSemaphoreCreateBinary();
    configASSERT(write_gram_semaphore != NULL);//失败会调用vAssertCalled函数，进入死循环
    st7789_spi_init();
    st7789_io_init();
    st7789_dma_init();
    st7789_int_init();
    st7789_init_display();
}

static void st7789_write_register(uint8_t reg, uint8_t data[], uint16_t length)
{
    SPI_DataSizeConfig(SPI2, SPI_DataSize_8b);
	GPIO_ResetBits(CS_PORT, CS_PIN);
    GPIO_ResetBits(DC_PORT, DC_PIN);
    SPI_I2S_SendData(SPI2, reg);
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET);
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) != RESET);

    GPIO_SetBits(DC_PORT, DC_PIN); // Data mode
    for (uint16_t i = 0; i < length; i++)
    {
        SPI_I2S_SendData(SPI2, data[i]);
        while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET);
    }
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) != RESET);
    GPIO_SetBits(CS_PORT, CS_PIN);
}
static void st7789_write_gram(uint8_t data[], uint32_t length, bool singlecolor)
{
    SPI_DataSizeConfig(SPI2, SPI_DataSize_16b);
    #define DMA_MAX_SIZE 65535 // DMA最大传输单位数

    GPIO_ResetBits(CS_PORT, CS_PIN);
    GPIO_SetBits(DC_PORT, DC_PIN);

    length >>= 1;

    do
    {
        uint32_t transfer_size = length < DMA_MAX_SIZE ? length : DMA_MAX_SIZE;
        DMA1_Stream4->NDTR = transfer_size;
        DMA1_Stream4->M0AR = (uint32_t)data;
        if (singlecolor)
        DMA1_Stream4->CR &= ~DMA_SxCR_MINC;
        else
        DMA1_Stream4->CR |= DMA_SxCR_MINC;
        DMA_Cmd(DMA1_Stream4, ENABLE);
        xSemaphoreTake(write_gram_semaphore, portMAX_DELAY);
        DMA_ClearFlag(DMA1_Stream4, DMA_FLAG_TCIF4);
        length -= transfer_size;
        if(!singlecolor)
        data += transfer_size << 1;
    } while (length > 0);

    // 等待SPI传输完成
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) != RESET);

    GPIO_SetBits(CS_PORT, CS_PIN);

#undef DMA_MAX_SIZE
}

static void st7789_reset(void)
{
    GPIO_ResetBits(RESET_PORT, RESET_PIN);
    delay_us(20);
    GPIO_SetBits(RESET_PORT, RESET_PIN);
    delay_ms(120);
}

void st7789_init_display(void)
{
    delay_ms(10);
    st7789_reset();

    st7789_write_register(0x11, NULL, 0); // Sleep out
    delay_ms(5);

    st7789_write_register(0x36, (uint8_t[]){0x00}, 1);                                                                                // Memory data access control
    st7789_write_register(0x3A, (uint8_t[]){0x55}, 1);                                                                                // 16-bit color mode
    st7789_write_register(0xB2, (uint8_t[]){0x0C, 0x0C, 0x00, 0x33, 0x33}, 5);                                                        // Porch setting
    st7789_write_register(0xB7, (uint8_t[]){0x46}, 1);                                                                                // Gate control
    st7789_write_register(0xBB, (uint8_t[]){0x1B}, 1);                                                                                // VCOM setting
    st7789_write_register(0xC0, (uint8_t[]){0x2C}, 1);                                                                                // LCM control
    st7789_write_register(0xC2, (uint8_t[]){0x01}, 1);                                                                                // VDV and VRH command enable
    st7789_write_register(0xC3, (uint8_t[]){0x0F}, 1);                                                                                // VRH set
    st7789_write_register(0xC4, (uint8_t[]){0x20}, 1);                                                                                // VDV set
    st7789_write_register(0xC6, (uint8_t[]){0x0F}, 1);                                                                                // Frame rate control
    st7789_write_register(0xD0, (uint8_t[]){0xA4, 0xA1}, 2);                                                                          // Power control 1
    st7789_write_register(0xD6, (uint8_t[]){0xA1}, 1);                                                                                // Display function control
    st7789_write_register(0xE0, (uint8_t[]){0xF0, 0x00, 0x06, 0x04, 0x05, 0x05, 0x31, 0x44, 0x48, 0x36, 0x12, 0x12, 0x2B, 0x34}, 14); // Gamma setting
    st7789_write_register(0xE1, (uint8_t[]){0xF0, 0x0B, 0x0F, 0x0F, 0x0D, 0x26, 0x31, 0x43, 0x47, 0x38, 0x14, 0x14, 0x2C, 0x32}, 14); // Positive gamma correction
    st7789_write_register(0x21, NULL, 0);                                                                                             // Inversion on
    st7789_write_register(0x29, NULL, 0);                                                                                             // Display on

    st7789_fill_color(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1, 0x0000); // Clear screen with black color
    st7789_set_backlight(true);                                           // Turn on backlight
}

void st7789_set_backlight(bool on)
{
    GPIO_WriteBit(BL_PORT, BL_PIN, on ? Bit_SET : Bit_RESET);
}

void st7789_output_lock(void)
{
    st7789_write_register(0x28, NULL, 0); // Display off
}

void st7789_output_unlock(void)
{
    st7789_write_register(0x29, NULL, 0); // Display on
}

void st7789_fill_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    if (x1 >= ST7789_WIDTH || y1 >= ST7789_HEIGHT || x2 >= ST7789_WIDTH || y2 >= ST7789_HEIGHT || x1 > x2 || y1 > y2)
    {
        return; // Out of bounds
    }

    uint32_t pixels = (x2 - x1 + 1) * (y2 - y1 + 1);
    st7789_write_register(0x2A, (uint8_t[]){(x1 >> 8) & 0xff, x1 & 0xff, (x2 >> 8) & 0xff, x2 & 0xff}, 4); // Column address set
    st7789_write_register(0x2B, (uint8_t[]){(y1 >> 8) & 0xff, y1 & 0xff, (y2 >> 8) & 0xff, y2 & 0xff}, 4); // Row address set
    st7789_write_register(0x2C, NULL, 0);                                                                  // Memory write
    st7789_write_gram((uint8_t *)&color, pixels * 2, true);
}

void st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT)
    {
        return; // Out of bounds
    }

    st7789_write_register(0x2A, (uint8_t[]){(x >> 8) & 0xff, x & 0xff, (x >> 8) & 0xff, x & 0xff}, 4); // Column address set
    st7789_write_register(0x2B, (uint8_t[]){(y >> 8) & 0xff, y & 0xff, (y >> 8) & 0xff, y & 0xff}, 4); // Row address set
    st7789_write_register(0x2C, NULL, 0);                                                              // Memory write

    GPIO_ResetBits(CS_PORT, CS_PIN);
    GPIO_SetBits(DC_PORT, DC_PIN);

    SPI_I2S_SendData(SPI2, (color >> 8) & 0xff);
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET)
        ;
    SPI_I2S_SendData(SPI2, color & 0xff);
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET)
        ;

    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_BSY) != RESET)
        ;
    GPIO_SetBits(CS_PORT, CS_PIN);
}

void st7789_draw_image(uint16_t x, uint16_t y, const image_t *image)
{
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT || x + image->width > ST7789_WIDTH || y + image->height > ST7789_HEIGHT)
    {
        return;
    }

    st7789_write_register(0x2A, (uint8_t[]){(x >> 8) & 0xff, x & 0xff, ((x + image->width - 1) >> 8) & 0xff, (x + image->width - 1) & 0xff}, 4);
    st7789_write_register(0x2B, (uint8_t[]){(y >> 8) & 0xff, y & 0xff, ((y + image->height - 1) >> 8) & 0xff, (y + image->height - 1) & 0xff}, 4);
    st7789_write_register(0x2C, NULL, 0);

    st7789_write_gram((uint8_t *)image->data, image->width * image->height * 2, false);
}

static bool is_gb2312(char ch)
{
    return ((unsigned char)ch >= 0xA1 && (unsigned char)ch <= 0xF7);
}

// static int utf8_char_length(const char *str)
//{
//     if ((*str & 0x80) == 0) return 1; // 1 byte
//     if ((*str & 0xE0) == 0xC0) return 2; // 2 bytes
//     if ((*str & 0xF0) == 0xE0) return 3; // 3 bytes
//     if ((*str & 0xF8) == 0xF0) return 4; // 4 bytes
//     return -1; // Invalid UTF-8
// }

static void st7789_draw_font(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *model, uint16_t color, uint16_t bg_color)
{
    uint16_t bytes_per_row = (width + 7) / 8;
    static uint8_t buff[72 * 72 * 2]; // Max font size is 72x72, each pixel takes 2 bytes
    uint8_t *pbuff = buff;
    st7789_write_register(0x2A, (uint8_t[]){(x >> 8) & 0xff, x & 0xff, ((x + width - 1) >> 8) & 0xff, (x + width - 1) & 0xff}, 4);
    st7789_write_register(0x2B, (uint8_t[]){(y >> 8) & 0xff, y & 0xff, ((y + height - 1) >> 8) & 0xff, (y + height - 1) & 0xff}, 4);
    st7789_write_register(0x2C, NULL, 0);
    for (uint16_t row = 0; row < height; row++)
    {
        const uint8_t *row_data = model + row * bytes_per_row;
        for (uint16_t col = 0; col < width; col++)
        {
            uint8_t pixel = row_data[col / 8] & (1 << (7 - col % 8));
            uint16_t pixel_color = pixel ? color : bg_color;
            *pbuff++ = pixel_color & 0xff;
            *pbuff++ = (pixel_color >> 8) & 0xff;
        }
    }
    st7789_write_gram(buff, pbuff - buff, false);
}

static const uint8_t *ascii_get_fmodel(const char ch, const font_t *font)
{
    uint16_t bytes_per_row = (font->size / 2 + 7) / 8;
    uint16_t bytes_per_char = font->size * bytes_per_row;
    if (font->ascii_map)
    {
        const char *map = font->ascii_map;
        do
        {
            if (*map == ch)
            {
                return font->ascii_model + (map - font->ascii_map) * bytes_per_char;
            }
        } while (*(++map) != '\0');
    }
    else
    {
        return font->ascii_model + (ch - ' ') * bytes_per_char;
    }
    return NULL;
}

static void st7789_write_ascii(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t bg_color, const font_t *font)
{
    uint16_t fwidth = font->size / 2;
    uint16_t fheight = font->size;
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT || x + fwidth > ST7789_WIDTH || y + fheight > ST7789_HEIGHT)
        return;

    const uint8_t *fmodel = ascii_get_fmodel(ch, font);
    if (fmodel != NULL)
        st7789_draw_font(x, y, fwidth, fheight, fmodel, color, bg_color);
}

static void st7789_write_chinese(uint16_t x, uint16_t y, const char *ch, uint16_t color, uint16_t bg_color, const font_t *font)
{
    if (font == NULL)
        return;

    const font_chinese_t *c = font->chinese;
    for (; c->name != NULL; c++)
    {
        if (strcmp(c->name, ch) == 0)
            break;
    }
    if (c->name == NULL)
        return;

    uint16_t fwidth = font->size;
    uint16_t fheight = font->size;
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT || x + fwidth > ST7789_WIDTH || y + fheight > ST7789_HEIGHT)
    {
        return;
    }

    const uint8_t *fmodel = c->model;
    st7789_draw_font(x, y, fwidth, fheight, fmodel, color, bg_color);
}

void st7789_write_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color, const font_t *font)
{
    while (*str)
    {
        // int len = utf8_char_length(str);
        int len = is_gb2312(*str) ? 2 : 1;
        if (len <= 0)
        {
            str++;
            continue;
        }
        else if (len == 1)
        {
            st7789_write_ascii(x, y, *str, color, bg_color, font);
            str++;
            x += font->size / 2;
        }
        else
        {
            char ch[5] = {0};
            strncpy(ch, str, len);
            st7789_write_chinese(x, y, ch, color, bg_color, font);
            str += len;
            x += font->size;
        }
    }
}
void DMA1_Stream4_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (DMA_GetITStatus(DMA1_Stream4, DMA_IT_TCIF4) != RESET)
    {
        xSemaphoreGiveFromISR(write_gram_semaphore, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        DMA_ClearITPendingBit(DMA1_Stream4, DMA_FLAG_TCIF4);
    }

}
