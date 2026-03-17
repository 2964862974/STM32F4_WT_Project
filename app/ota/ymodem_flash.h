#ifndef __YMODEM_FLASH_H__
#define __YMODEM_FLASH_H__

#include <stdint.h>

// Receive a file via YMODEM on USART1 and write it to W25Q32 starting at addr.
// Prints progress to console. Returns number of bytes written, 0 on failure.
uint32_t ymodem_flash_receive(uint32_t flash_addr);

#endif
