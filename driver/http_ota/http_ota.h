#ifndef __HTTP_OTA_H__
#define __HTTP_OTA_H__

#include <stdint.h>
#include <stdbool.h>

// Download entire firmware from url, write directly to W25Q32 starting at flash_addr.
// Calls progress_cb(bytes_received, total) periodically if not NULL.
// Returns total bytes written, 0 on failure.
typedef void (*http_ota_progress_cb_t)(uint32_t received, uint32_t total);
uint32_t http_ota_download(const char *url, uint32_t fw_size,
                           uint32_t flash_addr, http_ota_progress_cb_t progress_cb);

// Called from esp_at's USART2_IRQHandler for every received byte.
// Returns true if consumed (OTA active), false to let esp_at handle normally.
bool http_ota_isr_hook(uint8_t byte);

#endif
