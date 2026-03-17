#include <string.h>
#include "stm32f4xx.h"
#include "ota.h"

uint32_t ota_get_current_version(void)
{
    const app_header_t *hdr = (const app_header_t *)0x08010200;
    if (hdr->magic != APP_HEADER_MAGIC)
        return 0;
    return hdr->version;
}
