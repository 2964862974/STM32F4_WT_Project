#ifndef __IWDG_H__
#define __IWDG_H__

#include <stdint.h>

/*
 * IWDG 独立看门狗驱动
 *
 * STM32F4 IWDG 时钟源：LSI（约 32KHz）
 * 超时时间 = (prescaler / 32000) * reload
 *
 * 本配置：prescaler=256, reload=4000
 * 超时 = (256 / 32000) * 4000 = 32 秒
 * 喂狗间隔建议 ≤ 10 秒
 */
#define IWDG_PRESCALER  IWDG_Prescaler_256
#define IWDG_RELOAD     4000   /* 超时约 32 秒 */

void iwdg_init(void);
void iwdg_feed(void);

#endif
