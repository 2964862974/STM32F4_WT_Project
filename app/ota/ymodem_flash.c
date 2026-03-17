#include <stdio.h>

#include <string.h>

#include "stm32f4xx.h"
#include "console.h"
#include "FreeRTOS.h"
#include "w25q32.h"
#include "task.h"
#include "semphr.h"
#define SOH       0x01
#define STX       0x02
#define EOT       0x04
#define ACK       0x06
#define NAK       0x15
#define CAN       0x18
#define CRCREQ    0x43  // 'C'

#define PKT_128   128
#define PKT_1K    1024
#define PKT_MAX   (1 + 2 + PKT_1K + 2)  // head + seq + seqcomp + data + crc

// Ring buffer for ISR -> task byte passing
#define RING_SIZE 2048
static uint8_t  s_ring[RING_SIZE];
static volatile uint16_t s_head = 0;
static volatile uint16_t s_tail = 0;
static QueueHandle_t s_rx_sem;

static void rx_callback(uint8_t byte)
{
    BaseType_t woken = pdFALSE;
    uint16_t next = (s_head + 1) % RING_SIZE;
    if (next != s_tail) {
        s_ring[s_head] = byte;
        s_head = next;
    }
    xSemaphoreGiveFromISR(s_rx_sem, &woken);
    portYIELD_FROM_ISR(woken);
}

static void send_byte(uint8_t b)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, b);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
}

// Wait up to timeout_ms for at least `need` bytes in ring buffer
static uint8_t wait_bytes(uint32_t need, uint32_t timeout_ms)
{
    uint32_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (1) {
        uint32_t avail = (s_head - s_tail + RING_SIZE) % RING_SIZE;
        if (avail >= need) return 1;
        if (xTaskGetTickCount() >= deadline) return 0;
        xSemaphoreTake(s_rx_sem, pdMS_TO_TICKS(10));
    }
}

static uint8_t read_byte(void)
{
    uint8_t b = s_ring[s_tail];
    s_tail = (s_tail + 1) % RING_SIZE;
    return b;
}

static void flush_rx(void)
{
    s_head = s_tail = 0;
}

static uint16_t crc16(const uint8_t *buf, uint32_t len)
{
    uint16_t crc = 0;
    while (len--) {
        crc ^= (*buf++ << 8);
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

uint32_t ymodem_flash_receive(uint32_t flash_addr)
{
    static uint8_t pkt[PKT_MAX];
    uint32_t bytes_written = 0;
    uint32_t file_size     = 0;
    uint8_t  first_packet  = 1;
    uint32_t write_addr    = flash_addr;

    if (s_rx_sem == NULL)
        s_rx_sem = xSemaphoreCreateBinary();

    flush_rx();
    console_received_register(rx_callback);

    // Erase W25Q32 sectors for the OTA area
    printf("[YMODEM] Erasing W25Q32 OTA area...\r\n");
    uint32_t sectors = W25Q32_OTA_FW_MAX_SIZE / W25Q32_SECTOR_SIZE;
    for (uint32_t i = 0; i < sectors; i++)
        w25q32_erase_sector(flash_addr + i * W25Q32_SECTOR_SIZE);
    printf("[YMODEM] Erase done. Waiting for file (send via YMODEM)...\r\n");

    // Send 'C' to request CRC mode
    while (1) {
        flush_rx();
        send_byte(CRCREQ);
        if (wait_bytes(1, 1000)) {
            uint8_t h = read_byte();
            if (h == SOH || h == STX) {
                // Got first packet header, put it back by adjusting tail
                s_tail = (s_tail - 1 + RING_SIZE) % RING_SIZE;
                break;
            }
        }
    }

    while (1) {
        // Wait for packet header
        if (!wait_bytes(1, 5000)) {
            printf("[YMODEM] Timeout waiting for packet header\r\n");
            send_byte(CAN); send_byte(CAN);
            goto done;
        }

        uint8_t head = read_byte();
        uint32_t pkt_data_len;

        if (head == EOT) {
            send_byte(NAK);
            flush_rx();
            if (!wait_bytes(1, 3000) || read_byte() != EOT) goto done;
            send_byte(ACK);
            send_byte(CRCREQ);
            flush_rx();
            // Wait for final empty packet (filename packet with empty name)
            if (wait_bytes(1, 3000)) {
                uint8_t h2 = read_byte();
                if (h2 == SOH) {
                    if (wait_bytes(2 + PKT_128 + 2, 3000))
                        send_byte(ACK);
                }
            }
            printf("[YMODEM] Transfer complete! %lu bytes written.\r\n",
                   (unsigned long)bytes_written);
            goto done;
        }

        if (head == CAN) {
            printf("[YMODEM] Transfer cancelled by sender.\r\n");
            goto done;
        }

        if (head == SOH) pkt_data_len = PKT_128;
        else if (head == STX) pkt_data_len = PKT_1K;
        else { flush_rx(); continue; }

        uint32_t total = 2 + pkt_data_len + 2; // seq + seqcomp + data + crc
        if (!wait_bytes(total, 5000)) {
            printf("[YMODEM] Timeout waiting for packet body\r\n");
            send_byte(NAK);
            flush_rx();
            continue;
        }

        pkt[0] = head;
        for (uint32_t i = 0; i < total; i++)
            pkt[1 + i] = read_byte();

        // Verify sequence number
        if (pkt[1] != (uint8_t)(~pkt[2])) {
            printf("[YMODEM] Seq error\r\n");
            send_byte(NAK);
            flush_rx();
            continue;
        }

        // Verify CRC
        uint16_t rx_crc   = ((uint16_t)pkt[3 + pkt_data_len] << 8) | pkt[3 + pkt_data_len + 1];
        uint16_t calc_crc = crc16(&pkt[3], pkt_data_len);
        if (rx_crc != calc_crc) {
            printf("[YMODEM] CRC error\r\n");
            send_byte(NAK);
            flush_rx();
            continue;
        }

        if (first_packet) {
            first_packet = 0;
            // Parse filename packet: "name\0size\0"
            uint8_t *p = &pkt[3];
            while (*p && p < &pkt[3 + pkt_data_len]) p++;
            p++;
            file_size = 0;
            while (*p >= '0' && *p <= '9') file_size = file_size * 10 + (*p++ - '0');
            printf("[YMODEM] File size: %lu bytes\r\n", (unsigned long)file_size);
            send_byte(ACK);
            send_byte(CRCREQ);
        } else {
            uint32_t to_write = pkt_data_len;
            if (file_size > 0 && bytes_written + pkt_data_len > file_size)
                to_write = file_size - bytes_written;

            w25q32_write(write_addr, &pkt[3], to_write);
            write_addr    += to_write;
            bytes_written += to_write;

            printf("[YMODEM] %lu / %lu bytes\r\n",
                   (unsigned long)bytes_written,
                   (unsigned long)file_size);
            send_byte(ACK);
        }
    }

done:
    console_received_register(NULL);
    return bytes_written;
}

