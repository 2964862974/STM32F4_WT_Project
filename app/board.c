#include <stdio.h>
#include "stm32f4xx.h"
#include "cpu_tick.h"
#include "console.h"
#include "rtc.h"
#include "aht20.h"
#include "st7789.h"
#include "delay.h"
#include "w25q32.h"
#include "ota.h"

/* APP 版本 header，固定放在 0x08010200（向量表之后，不冲突）
 * 每次升级时修改 version 字段，阿里云 OTA 通过此字段判断版本 */
const app_header_t app_header __attribute__((at(0x08010200))) = {
    .magic   = APP_HEADER_MAGIC,
    .version = 4,   /* 当前版本，每次发布新版本时递增 */
    .size    = 0,   /* 可选，填 0 也能工作 */
    .crc32   = 0,   /* 可选，填 0 也能工作 */
};

void board_lowlevel_init(void)
{
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM5, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RCC_RTCCLKConfig(RCC_RTCCLKSource_HSE_Div25);
}

void board_init(void)
{
    console_init();
    printf("[SYS] Build Date: %s %s\n", __DATE__, __TIME__);
    time_delay_init();
    rtc_init();
    aht20_init();
    w25q32_init();
}

int fputc(int ch, FILE *f)
{
    USART_SendData(USART1, (uint8_t)ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    return ch;
}
