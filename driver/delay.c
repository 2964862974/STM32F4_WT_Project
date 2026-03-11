#include <stdint.h>
#include <string.h>
#include "stm32f4xx.h"
#include "delay.h"

void time_delay_init(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_TimeBaseStructure.TIM_Period = 0xFFFFFFFF;
    TIM_TimeBaseStructure.TIM_Prescaler = 84-1; // 84MHz / 84 = 1MHz 1微秒
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM5, &TIM_TimeBaseStructure);
    TIM_ClearFlag(TIM5, TIM_FLAG_Update);
    TIM_Cmd(TIM5, ENABLE);
}
void time_delay_us(uint32_t us)
{
    uint32_t start = TIM_GetCounter(TIM5);
    while (TIM_GetCounter(TIM5) - start < us)
    {
        ;
    }
}
void time_delay_ms(uint32_t ms)
{
    time_delay_us(ms * 1000);
}
