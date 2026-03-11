#ifndef __DELAY_H__
#define __DELAY_H__

#include <stdint.h>

void time_delay_init(void);
void time_delay_us(uint32_t us);
void time_delay_ms(uint32_t ms);



#endif /* __DELAY_H__ */
