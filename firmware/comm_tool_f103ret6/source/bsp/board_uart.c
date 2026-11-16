#include "board_uart.h"
#include "ct_board_port.h"
#include "ct_config.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"
#include "misc.h"

#define BOARD_UART_RX_BUF_SIZE        1024u
#define BOARD_UART_TX_BUF_SIZE        1024u
#define BOARD_BMS_UART_RX_BUF_SIZE    512u
#define BOARD_BMS_UART_TX_BUF_SIZE    1152u
#define BOARD_UART_TX_TIMEOUT_LOOPS   60000u
#define BOARD_BMS_UART_TX_TIMEOUT_MS  2000u

static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static uint8_t s_rx_buf[BOARD_UART_RX_BUF_SIZE];
static volatile uint16_t s_tx_head;
static volatile uint16_t s_tx_tail;
static uint8_t s_tx_buf[BOARD_UART_TX_BUF_SIZE];
static volatile uint16_t s_bms_rx_head;
static volatile uint16_t s_bms_rx_tail;
static uint8_t s_bms_rx_buf[BOARD_BMS_UART_RX_BUF_SIZE];
static volatile uint16_t s_bms_tx_head;
static volatile uint16_t s_bms_tx_tail;
static uint8_t s_bms_tx_buf[BOARD_BMS_UART_TX_BUF_SIZE];

#if (CT_COMM_UART_PORT == CT_COMM_UART_PORT_USART1)
#define BOARD_UART_INSTANCE            USART1
#define BOARD_UART_IRQn                USART1_IRQn
#define BOARD_UART_IRQHandler          USART1_IRQHandler
#elif (CT_COMM_UART_PORT == CT_COMM_UART_PORT_USART3)
#define BOARD_UART_INSTANCE            USART3
#define BOARD_UART_IRQn                USART3_IRQn
#define BOARD_UART_IRQHandler          USART3_IRQHandler
#else
#error "Unsupported CT_COMM_UART_PORT"
#endif

static void rx_push(uint8_t byte)
{
    uint16_t next;

    next = (uint16_t)((s_rx_head + 1u) % BOARD_UART_RX_BUF_SIZE);
    if (next != s_rx_tail)
    {
        s_rx_buf[s_rx_head] = byte;
        s_rx_head = next;
    }
}

static void bms_rx_push(uint8_t byte)
{
    uint16_t next;

    next = (uint16_t)((s_bms_rx_head + 1u) % BOARD_BMS_UART_RX_BUF_SIZE);
    if (next != s_bms_rx_tail)
    {
        s_bms_rx_buf[s_bms_rx_head] = byte;
        s_bms_rx_head = next;
    }
}

static uint16_t tx_next(uint16_t index)
{
    index++;
    if (index >= BOARD_UART_TX_BUF_SIZE)
    {
        index = 0u;
    }
    return index;
}

static uint16_t bms_tx_next(uint16_t index)
{
    index++;
    if (index >= BOARD_BMS_UART_TX_BUF_SIZE)
    {
        index = 0u;
    }
    return index;
}

static int tx_empty(void)
{
    return (s_tx_head == s_tx_tail) ? 1 : 0;
}

static int bms_tx_empty(void)
{
    return (s_bms_tx_head == s_bms_tx_tail) ? 1 : 0;
}

static int tx_full(void)
{
    return (tx_next(s_tx_head) == s_tx_tail) ? 1 : 0;
}

static int bms_tx_full(void)
{
    return (bms_tx_next(s_bms_tx_head) == s_bms_tx_tail) ? 1 : 0;
}

static void tx_start(void)
{
    USART_ITConfig(BOARD_UART_INSTANCE, USART_IT_TXE, ENABLE);
}

static void bms_tx_start(void)
{
    USART_ITConfig(USART2, USART_IT_TXE, ENABLE);
}

static int tx_push(uint8_t byte)
{
    uint32_t wait = BOARD_UART_TX_TIMEOUT_LOOPS;

    while (tx_full() != 0)
    {
        if (wait == 0u)
        {
            return 0;
        }
        wait--;
    }

    s_tx_buf[s_tx_head] = byte;
    s_tx_head = tx_next(s_tx_head);
    tx_start();
    return 1;
}

static int bms_tx_push(uint8_t byte)
{
    uint32_t wait = BOARD_UART_TX_TIMEOUT_LOOPS;

    while (bms_tx_full() != 0)
    {
        if (wait == 0u)
        {
            return 0;
        }
        wait--;
    }

    s_bms_tx_buf[s_bms_tx_head] = byte;
    s_bms_tx_head = bms_tx_next(s_bms_tx_head);
    bms_tx_start();
    return 1;
}

static void tx_irq_service(void)
{
    if (s_tx_tail == s_tx_head)
    {
        USART_ITConfig(BOARD_UART_INSTANCE, USART_IT_TXE, DISABLE);
        return;
    }

    USART_SendData(BOARD_UART_INSTANCE, s_tx_buf[s_tx_tail]);
    s_tx_tail = tx_next(s_tx_tail);
}

static void bms_tx_irq_service(void)
{
    if (s_bms_tx_tail == s_bms_tx_head)
    {
        USART_ITConfig(USART2, USART_IT_TXE, DISABLE);
        return;
    }

    USART_SendData(USART2, s_bms_tx_buf[s_bms_tx_tail]);
    s_bms_tx_tail = bms_tx_next(s_bms_tx_tail);
}

static void board_uart_gpio_init(void)
{
    GPIO_InitTypeDef gpio;

#if (CT_COMM_UART_PORT == CT_COMM_UART_PORT_USART1)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO |
                           RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_USART1,
                           ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_USART1, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_6;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_7;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &gpio);

#elif (CT_COMM_UART_PORT == CT_COMM_UART_PORT_USART3)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOC, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    GPIO_PinRemapConfig(GPIO_PartialRemap_USART3, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_11;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &gpio);
#endif
}

static void board_bms_uart_gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_2;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_3;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);
}

void BoardUart_Init(uint32_t baudrate)
{
    USART_InitTypeDef usart;
    NVIC_InitTypeDef nvic;

    s_rx_head = 0u;
    s_rx_tail = 0u;
    s_tx_head = 0u;
    s_tx_tail = 0u;

    board_uart_gpio_init();

    USART_StructInit(&usart);
    usart.USART_BaudRate = baudrate;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(BOARD_UART_INSTANCE, &usart);

    nvic.NVIC_IRQChannel = BOARD_UART_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1u;
    nvic.NVIC_IRQChannelSubPriority = 0u;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    USART_ITConfig(BOARD_UART_INSTANCE, USART_IT_RXNE, ENABLE);
    USART_Cmd(BOARD_UART_INSTANCE, ENABLE);
}

void BoardBmsUart_Init(uint32_t baudrate)
{
    USART_InitTypeDef usart;
    NVIC_InitTypeDef nvic;

    board_bms_uart_gpio_init();
    USART_Cmd(USART2, DISABLE);
    USART_ITConfig(USART2, USART_IT_RXNE, DISABLE);
    USART_ITConfig(USART2, USART_IT_TXE, DISABLE);
    USART_DeInit(USART2);

    s_bms_rx_head = 0u;
    s_bms_rx_tail = 0u;
    s_bms_tx_head = 0u;
    s_bms_tx_tail = 0u;

    USART_StructInit(&usart);
    usart.USART_BaudRate = baudrate;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &usart);

    nvic.NVIC_IRQChannel = USART2_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1u;
    nvic.NVIC_IRQChannelSubPriority = 2u;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART2, ENABLE);
}

int BoardUart_ReadByte(uint8_t *byte)
{
    if ((byte == 0) || (s_rx_head == s_rx_tail))
    {
        return 0;
    }

    *byte = s_rx_buf[s_rx_tail];
    s_rx_tail = (uint16_t)((s_rx_tail + 1u) % BOARD_UART_RX_BUF_SIZE);
    return 1;
}

int BoardBmsUart_ReadByte(uint8_t *byte)
{
    if ((byte == 0) || (s_bms_rx_head == s_bms_rx_tail))
    {
        return 0;
    }

    *byte = s_bms_rx_buf[s_bms_rx_tail];
    s_bms_rx_tail = (uint16_t)((s_bms_rx_tail + 1u) % BOARD_BMS_UART_RX_BUF_SIZE);
    return 1;
}

int CtBoard_UartWrite(const uint8_t *data, uint16_t length)
{
    uint16_t i;
    uint32_t wait;

    if ((data == 0) || (length == 0u))
    {
        return 0;
    }

    for (i = 0u; i < length; ++i)
    {
        if (tx_push(data[i]) == 0)
        {
            return 0;
        }
    }
    wait = BOARD_UART_TX_TIMEOUT_LOOPS;
    while ((wait > 0u) &&
           ((tx_empty() == 0) ||
            (USART_GetFlagStatus(BOARD_UART_INSTANCE, USART_FLAG_TC) == RESET)))
    {
        wait--;
    }
    if (wait == 0u)
    {
        return 0;
    }
    return 1;
}

int BoardBmsUart_Write(const uint8_t *data, uint16_t length)
{
    uint16_t i;
    uint32_t start;

    if ((data == 0) || (length == 0u))
    {
        return 0;
    }

    for (i = 0u; i < length; ++i)
    {
        if (bms_tx_push(data[i]) == 0)
        {
            return 0;
        }
    }
    start = CtBoard_GetTickMs();
    while ((bms_tx_empty() == 0) ||
           (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET))
    {
        if ((uint32_t)(CtBoard_GetTickMs() - start) >= BOARD_BMS_UART_TX_TIMEOUT_MS)
        {
            return 0;
        }
    }
    return 1;
}

void BOARD_UART_IRQHandler(void)
{
    if (USART_GetITStatus(BOARD_UART_INSTANCE, USART_IT_RXNE) != RESET)
    {
        rx_push((uint8_t)USART_ReceiveData(BOARD_UART_INSTANCE));
        USART_ClearITPendingBit(BOARD_UART_INSTANCE, USART_IT_RXNE);
    }
    if (USART_GetITStatus(BOARD_UART_INSTANCE, USART_IT_TXE) != RESET)
    {
        tx_irq_service();
    }
    if (USART_GetFlagStatus(BOARD_UART_INSTANCE, USART_FLAG_ORE) != RESET)
    {
        (void)BOARD_UART_INSTANCE->SR;
        (void)BOARD_UART_INSTANCE->DR;
    }
}

void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
    {
        bms_rx_push((uint8_t)USART_ReceiveData(USART2));
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }
    if (USART_GetITStatus(USART2, USART_IT_TXE) != RESET)
    {
        bms_tx_irq_service();
    }
    if (USART_GetFlagStatus(USART2, USART_FLAG_ORE) != RESET)
    {
        (void)USART2->SR;
        (void)USART2->DR;
    }
}
