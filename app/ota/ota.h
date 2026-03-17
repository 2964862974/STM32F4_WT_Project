#ifndef __OTA_H__
#define __OTA_H__

#include <stdint.h>
#include <stdbool.h>

// Place this struct at APP flash offset +0x200 to store version info
// (avoids conflicting with the vector table at the start of APP)
typedef struct {
    uint32_t magic;    // APP_HEADER_MAGIC
    uint32_t version;  // e.g. 1 = v1, 2 = v2
    uint32_t size;     // firmware size in bytes
    uint32_t crc32;    // CRC32 of firmware
} app_header_t;

#define APP_HEADER_MAGIC  0xA55A1234

// Fill in your Aliyun OSS URLs here
#define OTA_VERSION_URL   "http://your-bucket.oss-cn-hangzhou.aliyuncs.com/firmware/version.json"
#define OTA_FIRMWARE_URL  "http://your-bucket.oss-cn-hangzhou.aliyuncs.com/firmware/app.bin"

// Check server for new firmware; download to W25Q32 and reboot if found.
// Returns true if update was triggered (device will reboot, never returns).
bool ota_check_and_update(void);

// Read current firmware version from APP header in flash. Returns 0 if no header.
uint32_t ota_get_current_version(void);

#endif
