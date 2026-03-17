#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "w25q32.h"

/*
 * DMA 分配：
 *   DMA2_Stream0 Ch3：SPI1 RX，接收数据到用户缓冲区
 *   DMA2_Stream3 Ch3：SPI1 TX，发送哑字节（0xFF）驱动 SPI 时钟
 *
 * 只有 w25q32_read() 使用 DMA，写入/擦除仍用轮询
 * （写入瓶颈在 Flash 内部操作时间，DMA 收益可忽略）
 */
#define SPI1_DMA_CHANNEL    DMA_Channel_3
#define SPI1_DMA_RX_STREAM  DMA2_Stream0   /* SPI1_RX */
#define SPI1_DMA_TX_STREAM  DMA2_Stream3   /* SPI1_TX */
#define SPI1_DMA_RX_IRQn    DMA2_Stream0_IRQn
#define SPI1_DMA_RX_FLAG_TC DMA_FLAG_TCIF0 /* Stream0 传输完成标志 */

/* 二值信号量：DMA RX 完成中断里释放，w25q32_read 里等待 */
static SemaphoreHandle_t s_dma_done = NULL;

/* TX 哑字节缓冲区：固定为 0xFF，DMA 从此地址循环读取 */
static const uint8_t s_dummy = 0xFF;

// SPI1: PA5=SCK, PA6=MISO, PA7=MOSI, PA4=CS
#define W25Q32_CS_PORT   GPIOA
#define W25Q32_CS_PIN    GPIO_Pin_4

#define CS_LOW()   GPIO_ResetBits(W25Q32_CS_PORT, W25Q32_CS_PIN)
#define CS_HIGH()  GPIO_SetBits(W25Q32_CS_PORT, W25Q32_CS_PIN)

// W25Q32 commands
#define CMD_WRITE_ENABLE    0x06
#define CMD_WRITE_DISABLE   0x04
#define CMD_READ_STATUS1    0x05
#define CMD_READ_DATA       0x03
#define CMD_PAGE_PROGRAM    0x02
#define CMD_SECTOR_ERASE    0x20
#define CMD_CHIP_ERASE      0xC7
#define CMD_JEDEC_ID        0x9F

#define STATUS_BUSY         0x01

static uint8_t spi1_transfer(uint8_t data)
{
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET);
    SPI_I2S_SendData(SPI1, data);
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET);
    return SPI_I2S_ReceiveData(SPI1);
}

static void w25q32_wait_busy(void)
{
    CS_LOW();
    spi1_transfer(CMD_READ_STATUS1);
    while (spi1_transfer(0xFF) & STATUS_BUSY);
    CS_HIGH();
}

static void w25q32_write_enable(void)
{
    CS_LOW();
    spi1_transfer(CMD_WRITE_ENABLE);
    CS_HIGH();
}

void w25q32_init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    SPI_InitTypeDef   SPI_InitStructure;
    DMA_InitTypeDef   DMA_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    /* 创建二值信号量，初始为空（taken），DMA 完成中断里 give */
    s_dma_done = xSemaphoreCreateBinary();

    /* CS 引脚 PA4：推挽输出，初始高电平（不选中） */
    GPIO_SetBits(GPIOA, W25Q32_CS_PIN);
    GPIO_InitStructure.GPIO_Pin   = W25Q32_CS_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* SPI1 复用引脚：PA5=SCK, PA6=MISO, PA7=MOSI */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource5, GPIO_AF_SPI1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource6, GPIO_AF_SPI1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_SPI1);
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* SPI1：主机，全双工，8位，模式0，21MHz */
    SPI_StructInit(&SPI_InitStructure);
    SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL              = SPI_CPOL_Low;
    SPI_InitStructure.SPI_CPHA              = SPI_CPHA_1Edge;
    SPI_InitStructure.SPI_NSS               = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4; // 84MHz/4=21MHz
    SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_Init(SPI1, &SPI_InitStructure);
    SPI_Cmd(SPI1, ENABLE);

    /* DMA 公共配置 */
    DMA_StructInit(&DMA_InitStructure);
    DMA_InitStructure.DMA_Channel            = SPI1_DMA_CHANNEL;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DR;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority           = DMA_Priority_High;
    DMA_InitStructure.DMA_FIFOMode           = DMA_FIFOMode_Disable;
    DMA_InitStructure.DMA_BufferSize         = 1; /* 占位值，每次传输前动态写 NDTR */

    /* DMA2_Stream0：SPI1 RX，外设→内存，内存地址自增 */
    DMA_InitStructure.DMA_DIR             = DMA_DIR_PeripheralToMemory;
    DMA_InitStructure.DMA_PeripheralInc   = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc       = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_Memory0BaseAddr = 0; /* 每次传输前动态写 M0AR */
    DMA_Init(SPI1_DMA_RX_STREAM, &DMA_InitStructure);
    /* DMA_Init 会清除中断使能位，必须在 DMA_Init 之后配置中断 */
    DMA_ITConfig(SPI1_DMA_RX_STREAM, DMA_IT_TC, ENABLE);

    /* DMA2_Stream3：SPI1 TX，内存→外设，内存地址不自增（固定发哑字节 0xFF） */
    DMA_InitStructure.DMA_DIR             = DMA_DIR_MemoryToPeripheral;
    DMA_InitStructure.DMA_MemoryInc       = DMA_MemoryInc_Disable;
    DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)&s_dummy;
    DMA_Init(SPI1_DMA_TX_STREAM, &DMA_InitStructure);

    /* NVIC：DMA2_Stream0 中断，优先级低于 FreeRTOS 系统调用阈值 */
    NVIC_InitStructure.NVIC_IRQChannel                   = SPI1_DMA_RX_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

void w25q32_read_id(uint8_t *manufacturer, uint8_t *mem_type, uint8_t *capacity)
{
    CS_LOW();
    spi1_transfer(CMD_JEDEC_ID);
    *manufacturer = spi1_transfer(0xFF); // 0xEF = Winbond
    *mem_type     = spi1_transfer(0xFF); // 0x40 = W25Q series
    *capacity     = spi1_transfer(0xFF); // 0x17 = 64Mbit (W25Q64)
    CS_HIGH();
}

void w25q32_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    CS_LOW();
    /* 轮询发送命令字节和 24 位地址（共 4 字节） */
    spi1_transfer(CMD_READ_DATA);
    spi1_transfer((addr >> 16) & 0xFF);
    spi1_transfer((addr >>  8) & 0xFF);
    spi1_transfer( addr        & 0xFF);
    /* 此时 SPI RX FIFO 已被上面 4 次收发清空（spi1_transfer 每次都读了 DR），无残留 */

    /* 确保两个 Stream 处于关闭状态再配置（直接检查 EN 位） */
    DMA_Cmd(SPI1_DMA_RX_STREAM, DISABLE);
    DMA_Cmd(SPI1_DMA_TX_STREAM, DISABLE);
    while (SPI1_DMA_RX_STREAM->CR & DMA_SxCR_EN);
    while (SPI1_DMA_TX_STREAM->CR & DMA_SxCR_EN);

    /* 清除上次传输残留的标志位 */
    DMA_ClearFlag(SPI1_DMA_RX_STREAM, DMA_FLAG_TCIF0 | DMA_FLAG_HTIF0 |
                                      DMA_FLAG_TEIF0 | DMA_FLAG_DMEIF0 | DMA_FLAG_FEIF0);
    DMA_ClearFlag(SPI1_DMA_TX_STREAM, DMA_FLAG_TCIF3 | DMA_FLAG_HTIF3 |
                                      DMA_FLAG_TEIF3 | DMA_FLAG_DMEIF3 | DMA_FLAG_FEIF3);

    /* 配置 RX Stream：目标地址为用户缓冲区，传输长度为 len */
    SPI1_DMA_RX_STREAM->M0AR = (uint32_t)buf;
    SPI1_DMA_RX_STREAM->NDTR = len;

    /* 配置 TX Stream：源地址固定为哑字节，传输长度为 len */
    SPI1_DMA_TX_STREAM->NDTR = len;

    /* 先使能 SPI DMA 请求，再启动 Stream，避免 Stream 启动后错过第一个请求 */
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx | SPI_I2S_DMAReq_Tx, ENABLE);

    /* 先启动 RX，再启动 TX，避免 TX 先跑导致 RX 丢数据 */
    DMA_Cmd(SPI1_DMA_RX_STREAM, ENABLE);
    DMA_Cmd(SPI1_DMA_TX_STREAM, ENABLE);

    /* 阻塞等待 RX DMA 完成（中断里释放信号量），超时 100ms */
    xSemaphoreTake(s_dma_done, pdMS_TO_TICKS(100));

    /* 关闭 SPI DMA 请求，为下次传输做准备 */
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx | SPI_I2S_DMAReq_Tx, DISABLE);

    CS_HIGH();
}

/*
 * DMA2_Stream0_IRQHandler - SPI1 RX DMA 传输完成中断
 * RX 完成即代表整次读取结束，释放信号量唤醒等待的任务
 *
 */
void DMA2_Stream0_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (DMA_GetITStatus(SPI1_DMA_RX_STREAM, DMA_IT_TCIF0)) {
        DMA_ClearITPendingBit(SPI1_DMA_RX_STREAM, DMA_IT_TCIF0);
        /* 从中断里释放信号量，若有更高优先级任务被唤醒则请求任务切换 */
        xSemaphoreGiveFromISR(s_dma_done, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

// Write one page (max 256 bytes, must not cross page boundary)
static void w25q32_write_page(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    w25q32_write_enable();
    CS_LOW();
    spi1_transfer(CMD_PAGE_PROGRAM);
    spi1_transfer((addr >> 16) & 0xFF);
    spi1_transfer((addr >>  8) & 0xFF);
    spi1_transfer( addr        & 0xFF);
    for (uint32_t i = 0; i < len; i++)
        spi1_transfer(buf[i]);
    CS_HIGH();
    w25q32_wait_busy();
}

// Write arbitrary length, handles page boundaries automatically
void w25q32_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t offset = addr % W25Q32_PAGE_SIZE;
    uint32_t chunk  = (offset + len > W25Q32_PAGE_SIZE) ? (W25Q32_PAGE_SIZE - offset) : len;

    while (len > 0) {
        w25q32_write_page(addr, buf, chunk);
        addr += chunk;
        buf  += chunk;
        len  -= chunk;
        chunk = (len > W25Q32_PAGE_SIZE) ? W25Q32_PAGE_SIZE : len;
    }
}

void w25q32_erase_sector(uint32_t addr)
{
    addr &= ~(W25Q32_SECTOR_SIZE - 1); // align to sector
    w25q32_write_enable();
    CS_LOW();
    spi1_transfer(CMD_SECTOR_ERASE);
    spi1_transfer((addr >> 16) & 0xFF);
    spi1_transfer((addr >>  8) & 0xFF);
    spi1_transfer( addr        & 0xFF);
    CS_HIGH();
    w25q32_wait_busy();
}

void w25q32_erase_chip(void)
{
    w25q32_write_enable();
    CS_LOW();
    spi1_transfer(CMD_CHIP_ERASE);
    CS_HIGH();
    w25q32_wait_busy();
}

// --- OTA flag helpers ---
/* OTA flag 存储在 0x1FF000，由 APP 写入以触发 Bootloader 升级 */

/* 写入 OTA flag：先擦除 sector 再写入，确保数据干净 */
void w25q32_ota_write_flag(const w25q32_ota_flag_t *flag)
{
    w25q32_erase_sector(W25Q32_OTA_FLAG_ADDR);
    w25q32_write(W25Q32_OTA_FLAG_ADDR, (const uint8_t *)flag, sizeof(w25q32_ota_flag_t));
}

/* 读取 OTA flag */
void w25q32_ota_read_flag(w25q32_ota_flag_t *flag)
{
    w25q32_read(W25Q32_OTA_FLAG_ADDR, (uint8_t *)flag, sizeof(w25q32_ota_flag_t));
}

/* 清除 OTA flag：擦除整个 sector，magic 变为 0xFFFFFFFF 即视为无效 */
void w25q32_ota_clear_flag(void)
{
    w25q32_erase_sector(W25Q32_OTA_FLAG_ADDR);
}

// --- Backup flag helpers ---
/* 备份 flag 存储在 0x0BF000，由 Bootloader 在备份完成后写入
 * 回滚时读取此 flag 获得备份固件的大小 */

/* 读取备份 flag */
void w25q32_backup_read_flag(w25q32_backup_flag_t *flag)
{
    w25q32_read(W25Q32_BACKUP_FLAG_ADDR, (uint8_t *)flag, sizeof(w25q32_backup_flag_t));
}

/* 写入备份 flag：先擦除 sector 再写入 */
void w25q32_backup_write_flag(const w25q32_backup_flag_t *flag)
{
    w25q32_erase_sector(W25Q32_BACKUP_FLAG_ADDR);
    w25q32_write(W25Q32_BACKUP_FLAG_ADDR, (const uint8_t *)flag, sizeof(w25q32_backup_flag_t));
}

/* 清除备份 flag：擦除整个 sector */
void w25q32_backup_clear_flag(void)
{
    w25q32_erase_sector(W25Q32_BACKUP_FLAG_ADDR);
}

// --- Boot confirm flag helpers ---
/* boot confirm flag 存储在 0x1FE000
 * APP 启动时写 PENDING，稳定运行后写 CONFIRMED
 * Bootloader 检测到 PENDING 说明上次 APP 跑飞，自动回滚 */

void w25q32_boot_confirm_read(w25q32_boot_confirm_t *bc)
{
    w25q32_read(W25Q32_BOOT_CONFIRM_ADDR, (uint8_t *)bc, sizeof(w25q32_boot_confirm_t));
}

void w25q32_boot_confirm_write(uint32_t state)
{
    w25q32_boot_confirm_t bc;
    bc.magic = W25Q32_BOOT_CONFIRM_MAGIC;
    bc.state = state;
    w25q32_erase_sector(W25Q32_BOOT_CONFIRM_ADDR);
    w25q32_write(W25Q32_BOOT_CONFIRM_ADDR, (const uint8_t *)&bc, sizeof(bc));
}

void w25q32_boot_confirm_clear(void)
{
    w25q32_erase_sector(W25Q32_BOOT_CONFIRM_ADDR);
}
