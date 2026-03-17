#ifndef __W25Q32_H__
#define __W25Q32_H__

#include <stdint.h>

// W25Q32: 4MB, 64 blocks, 1024 sectors, each sector 4KB
#define W25Q32_SECTOR_SIZE      4096
#define W25Q32_PAGE_SIZE        256
#define W25Q32_TOTAL_SIZE       (4 * 1024 * 1024)

// 下载区: 0x000000, 384KB
#define W25Q32_OTA_FW_ADDR      0x000000
#define W25Q32_OTA_FW_MAX_SIZE  (384 * 1024)

// 备份区: 0x060000, 384KB (固件数据)
#define W25Q32_BACKUP_ADDR      0x060000
#define W25Q32_BACKUP_MAX_SIZE  (384 * 1024)

// 备份 flag: 0x0BF000 (备份区末尾 sector)
#define W25Q32_BACKUP_FLAG_ADDR  0x0BF000
#define W25Q32_BACKUP_FLAG_MAGIC 0xBACCBACE

// boot confirm flag: 0x1FE000
// APP 启动时写 PENDING，稳定后写 CONFIRMED
// Bootloader 检测到 PENDING 说明上次 APP 跑飞，自动回滚
#define W25Q32_BOOT_CONFIRM_ADDR            0x1FE000
#define W25Q32_BOOT_CONFIRM_MAGIC           0xB007C0DE
#define W25Q32_BOOT_CONFIRM_STATE_PENDING   0x00000001
#define W25Q32_BOOT_CONFIRM_STATE_CONFIRMED 0x00000002

// OTA flag: 0x1FF000
#define W25Q32_OTA_FLAG_ADDR    0x1FF000
#define W25Q32_OTA_FLAG_MAGIC   0xDEADBEEF

typedef struct {
    uint32_t magic;     // W25Q32_OTA_FLAG_MAGIC
    uint32_t fw_size;   // firmware size in bytes
    uint32_t fw_crc32;  // CRC32 of firmware
    uint32_t version;   // new firmware version
} w25q32_ota_flag_t;

// 备份 flag: 记录备份区中固件的大小
typedef struct {
    uint32_t magic;     // W25Q32_BACKUP_FLAG_MAGIC
    uint32_t bak_size;  // backup firmware size in bytes
} w25q32_backup_flag_t;

// boot confirm flag 结构体
typedef struct {
    uint32_t magic;  // W25Q32_BOOT_CONFIRM_MAGIC
    uint32_t state;  // PENDING 或 CONFIRMED
} w25q32_boot_confirm_t;

void     w25q32_init(void);
void     w25q32_read_id(uint8_t *manufacturer, uint8_t *mem_type, uint8_t *capacity);
void     w25q32_read(uint32_t addr, uint8_t *buf, uint32_t len);
void     w25q32_write(uint32_t addr, const uint8_t *buf, uint32_t len);
void     w25q32_erase_sector(uint32_t addr);
void     w25q32_erase_chip(void);

// OTA helpers
void     w25q32_ota_write_flag(const w25q32_ota_flag_t *flag);
void     w25q32_ota_read_flag(w25q32_ota_flag_t *flag);
void     w25q32_ota_clear_flag(void);

// Backup helpers
void     w25q32_backup_read_flag(w25q32_backup_flag_t *flag);
void     w25q32_backup_write_flag(const w25q32_backup_flag_t *flag);
void     w25q32_backup_clear_flag(void);

// Boot confirm helpers
void     w25q32_boot_confirm_read(w25q32_boot_confirm_t *bc);
void     w25q32_boot_confirm_write(uint32_t state);
void     w25q32_boot_confirm_clear(void);

#endif
