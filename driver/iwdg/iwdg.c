#include "stm32f4xx.h"
#include "iwdg.h"

/*
 * iwdg_init - 启动独立看门狗
 * LSI 约 32KHz，prescaler=256，reload=4000
 * 超时时间 = (256 / 32000) * 4000 = 32 秒
 * 一旦启动无法关闭，复位后重新计时
 */
void iwdg_init(void)
{
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(IWDG_PRESCALER);
    IWDG_SetReload(IWDG_RELOAD);
    IWDG_ReloadCounter();   /* 先喂一次，防止立即超时 */
    IWDG_Enable();
}

/*
 * iwdg_feed - 喂狗，重置计数器
 * 必须在超时时间内周期性调用，否则触发系统复位
 */
void iwdg_feed(void)
{
    IWDG_ReloadCounter();
}
