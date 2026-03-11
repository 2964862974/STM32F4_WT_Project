#include <stdint.h>
#include <string.h>
#include "stm32f4xx.h"
#include "console.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
static console_received_func_t received_func;
static QueueHandle_t console_tx_semaphore;
static void console_io_init(void)
{
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_StructInit(&GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}
static void console_usart_init(void)
{

    USART_InitTypeDef USART_InitStructure;
    USART_StructInit(&USART_InitStructure);
    USART_InitStructure.USART_BaudRate = 115200u;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_Init(USART1, &USART_InitStructure);
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    USART_DMACmd(USART1, USART_DMAReq_Tx, ENABLE);
    USART_Cmd(USART1, ENABLE);
}
static void console_dma_init(void)
{
    DMA_InitTypeDef DMA_InitStructure;
    DMA_StructInit(&DMA_InitStructure);
    DMA_InitStructure.DMA_Channel = DMA_Channel_4;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR;
    DMA_InitStructure.DMA_DIR = DMA_DIR_MemoryToPeripheral;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_Low;
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Enable;
    DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_INC8;
    DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_ITConfig(DMA2_Stream7, DMA_IT_TC, ENABLE);
    DMA_Init(DMA2_Stream7, &DMA_InitStructure);
}
static void console_int_init(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn; /* USART1 global Interrupt */
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 6;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    NVIC_InitStructure.NVIC_IRQChannel = DMA2_Stream7_IRQn;
    NVIC_Init(&NVIC_InitStructure);
}
void console_init(void)
{
    console_tx_semaphore = xSemaphoreCreateBinary();
    console_usart_init();
    console_dma_init();
    console_int_init();
    console_io_init();
}

void console_write(const char str[])
{
    int len = strlen(str);
    do
    {
        uint32_t chunk_size = len < 65535 ? len : 65535; // DMA最大传输单位数
        DMA2_Stream7->NDTR = chunk_size;
        DMA2_Stream7->M0AR = (uint32_t)str;

        DMA_Cmd(DMA2_Stream7, ENABLE);
        xSemaphoreTake(console_tx_semaphore, portMAX_DELAY);
        len -= chunk_size;
        str += chunk_size;
    } while (len > 0);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
    USART_ClearFlag(USART1, USART_FLAG_TC);
}

void console_received_register(console_received_func_t func)
{
    received_func = func;
}

void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        if (received_func != NULL)
        {
            uint8_t data = USART_ReceiveData(USART1);
            received_func(data);
        }

        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}
void DMA2_Stream7_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (DMA_GetITStatus(DMA2_Stream7, DMA_IT_TCIF7) != RESET)
    {
        xSemaphoreGiveFromISR(console_tx_semaphore, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        DMA_ClearITPendingBit(DMA2_Stream7, DMA_FLAG_TCIF7);
    }

}
