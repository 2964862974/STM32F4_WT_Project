#ifndef __CPU_DELAY_H__
#define __CPU_DELAY_H__

#include <stdint.h>

typedef void (*cpu_periodic_callback_t)(void);

void cpu_tick_init(void);
uint64_t cpu_now(void);
uint64_t cpu_get_us(void);
uint64_t cpu_get_ms(void);
void cpu_delay_us(uint32_t us);
void cpu_delay_ms(uint32_t ms);
void cpu_register_periodic_callback(cpu_periodic_callback_t callback);

#endif /* __CPU_DELAY_H__ */
