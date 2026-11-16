#include "ct_iap.h"
#include "ct_config.h"
#include "ct_watchdog.h"
#include "stm32f10x.h"
#include "stm32f10x_can.h"
#include "stm32f10x_flash.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"
#include "misc.h"
#include <string.h>

#define IAP_SERIAL_BAUD                  CT_UART_DEFAULT_BAUD
#define IAP_SERIAL_RX_SIZE               1100u
#define IAP_SERIAL_IRQ_RX_SIZE           2048u
#define IAP_SERIAL_IRQ_TX_SIZE           256u
#define IAP_SERIAL_BLOCK_MAX             1024u
#define IAP_SERIAL_FRAME_TIMEOUT_MS      500u
#define IAP_SERIAL_RESPONSE_DELAY_MS     20u
#define IAP_SERIAL_TX_TIMEOUT_LOOPS      60000u
#define IAP_LED_PERIOD_MS                500u
#define IAP_CAN_BLOCK_BYTES              256u
#define IAP_CAN_PRESCALER_250K           4u
#define IAP_CAN_RX_TIMEOUT_MS            5000u
#define IAP_RESET_DELAY_MS               20u

#if (CT_COMM_UART_PORT == CT_COMM_UART_PORT_USART1)
#define IAP_SERIAL_USART                 USART1
#define IAP_SERIAL_IRQn                  USART1_IRQn
#define IAP_SERIAL_IRQHandler            USART1_IRQHandler
#elif (CT_COMM_UART_PORT == CT_COMM_UART_PORT_USART3)
#define IAP_SERIAL_USART                 USART3
#define IAP_SERIAL_IRQn                  USART3_IRQn
#define IAP_SERIAL_IRQHandler            USART3_IRQHandler
#else
#error "Unsupported CT_COMM_UART_PORT"
#endif

#define LEGACY_SLAVE_ADDR                0x01u
#define LEGACY_BROADCAST_ADDR            0x00u
#define LEGACY_CMD_READ_REGS             0x03u
#define LEGACY_CMD_WRITE_REGS            0x10u
#define LEGACY_FLASH_CONNECT_ADDR        0xFFFDu
#define LEGACY_FLASH_UPGRADE_ADDR        0xFFFEu
#define LEGACY_FLASH_COMPLETE_ADDR       0xFFFFu
#define LEGACY_RO_START                  0xD000u
#define LEGACY_RO_STATUS_ADDR            0xD050u
#define LEGACY_READ_WORDS_MAX            64u
#define LEGACY_ERR_ADDR_INVALID          0x01u
#define LEGACY_ERR_CRC                   0x02u
#define LEGACY_ERR_CMD_INVALID           0x04u

#define CAN_IAP_PROTOCOL_VERSION         1u
#define CAN_IAP_NODE_DEFAULT             CT_NODE_ID_DEFAULT
#define CAN_IAP_CTRL_BASE_ID             0x14F8F000u
#define CAN_IAP_ACK_BASE_ID              0x14F8F100u
#define CAN_IAP_DATA_BASE_ID             0x14000000u
#define CAN_IAP_DATA_ID_MASK             0x1F000000u
#define CAN_IAP_CMD_HELLO                0x01u
#define CAN_IAP_CMD_START                0x02u
#define CAN_IAP_CMD_COMMIT               0x03u
#define CAN_IAP_CMD_END                  0x04u
#define CAN_IAP_CMD_ABORT                0x05u
#define CAN_IAP_CMD_ACK                  0x79u
#define CAN_IAP_CMD_NACK                 0x1Fu
#define CAN_IAP_ERR_OK                   0x00u
#define CAN_IAP_ERR_BAD_CMD              0x01u
#define CAN_IAP_ERR_BAD_STATE            0x02u
#define CAN_IAP_ERR_BAD_PARAM            0x03u
#define CAN_IAP_ERR_BAD_SEQ              0x04u
#define CAN_IAP_ERR_CRC                  0x05u
#define CAN_IAP_ERR_FLASH                0x06u
#define CAN_IAP_ERR_APP_INVALID          0x07u

#define IAP_FLASH_APP_PAGE_COUNT         (CT_SELF_APP_SIZE / CT_SELF_FLASH_PAGE_SIZE)

typedef void (*pFunction)(void);

typedef struct
{
    uint32_t magic;
    uint32_t magic_inv;
    uint32_t request;
    uint32_t request_inv;
    uint32_t crc;
} IapBootMailbox;

typedef struct
{
    uint8_t owner;
    uint16_t first_page_len;
    uint8_t page_erased[IAP_FLASH_APP_PAGE_COUNT];
    uint8_t first_page[CT_SELF_FLASH_PAGE_SIZE];
} IapFlashContext;

typedef struct
{
    uint8_t state;
    uint8_t node;
    uint8_t last_cmd;
    uint8_t last_error;
    uint16_t expect_seq;
    uint16_t block_seq;
    uint16_t block_bytes;
    uint16_t fw_crc;
    uint16_t running_crc;
    uint32_t fw_size;
    uint32_t written;
    uint32_t last_rx_ms;
    uint8_t block[IAP_CAN_BLOCK_BYTES];
} IapCanContext;

static volatile uint32_t s_tick_ms;
static IapFlashContext s_flash;
static IapCanContext s_can;
static uint8_t s_serial_rx[IAP_SERIAL_RX_SIZE];
static uint16_t s_serial_index;
static uint16_t s_serial_expect;
static uint16_t s_serial_block_count;
static uint32_t s_serial_written;
static uint32_t s_serial_last_rx_ms;
static uint8_t s_serial_fault_count;
static volatile uint8_t s_serial_irq_rx[IAP_SERIAL_IRQ_RX_SIZE];
static volatile uint16_t s_serial_irq_head;
static volatile uint16_t s_serial_irq_tail;
static volatile uint8_t s_serial_irq_tx[IAP_SERIAL_IRQ_TX_SIZE];
static volatile uint16_t s_serial_tx_head;
static volatile uint16_t s_serial_tx_tail;
static uint8_t s_reset_pending;
static uint32_t s_reset_time_ms;

static uint16_t rd_be16(const uint8_t *data)
{
    return ((uint16_t)data[0] << 8) | data[1];
}

static uint32_t rd_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static void wr_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static uint16_t crc16_update(uint16_t crc, const uint8_t *data, uint16_t length)
{
    uint16_t i;
    uint8_t bit;

    for (i = 0u; i < length; ++i)
    {
        crc ^= data[i];
        for (bit = 0u; bit < 8u; ++bit)
        {
            if ((crc & 1u) != 0u)
            {
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            }
            else
            {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return crc;
}

static uint16_t crc16_calc(const uint8_t *data, uint16_t length)
{
    return crc16_update(0xFFFFu, data, length);
}

static uint32_t boot_crc(uint32_t magic, uint32_t request)
{
    return magic ^ request ^ 0xA5A55A5Au;
}

static volatile IapBootMailbox *boot_mailbox(void)
{
    return (volatile IapBootMailbox *)CT_BOOT_MAILBOX_ADDR;
}

static int boot_consume_iap_request(void)
{
    volatile IapBootMailbox *mailbox = boot_mailbox();
    int valid;

    valid = ((mailbox->magic == CT_BOOT_MAILBOX_MAGIC) &&
             (mailbox->magic_inv == ~CT_BOOT_MAILBOX_MAGIC) &&
             (mailbox->request == CT_BOOT_MAILBOX_REQUEST) &&
             (mailbox->request_inv == ~CT_BOOT_MAILBOX_REQUEST) &&
             (mailbox->crc == boot_crc(CT_BOOT_MAILBOX_MAGIC, CT_BOOT_MAILBOX_REQUEST))) ? 1 : 0;

    mailbox->magic = 0u;
    mailbox->magic_inv = 0u;
    mailbox->request = 0u;
    mailbox->request_inv = 0u;
    mailbox->crc = 0u;
    return valid;
}

static int valid_app_vector(uint32_t app_addr, uint32_t app_limit)
{
    uint32_t msp = *(__IO uint32_t *)app_addr;
    uint32_t reset = *(__IO uint32_t *)(app_addr + 4u);

    if ((msp < 0x20000000u) || (msp >= CT_BOOT_MAILBOX_ADDR))
    {
        return 0;
    }
    if ((reset < app_addr) || (reset >= app_limit) || ((reset & 1u) == 0u))
    {
        return 0;
    }
    return 1;
}

static void jump_to_app(void)
{
    pFunction jump;
    uint32_t jump_addr;

    if (!valid_app_vector(CT_SELF_APP_BASE, CT_SELF_APP_LIMIT))
    {
        return;
    }

    jump_addr = *(__IO uint32_t *)(CT_SELF_APP_BASE + 4u);
    jump = (pFunction)jump_addr;
    __disable_irq();
    SysTick->CTRL = 0u;
    USART_Cmd(IAP_SERIAL_USART, DISABLE);
    CAN_DeInit(CAN1);
    NVIC_DisableIRQ(IAP_SERIAL_IRQn);
    NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
    NVIC_ClearPendingIRQ(IAP_SERIAL_IRQn);
    NVIC_ClearPendingIRQ(USB_LP_CAN1_RX0_IRQn);
    SCB->VTOR = CT_SELF_APP_BASE;
    __set_CONTROL(0u);
    __set_MSP(*(__IO uint32_t *)CT_SELF_APP_BASE);
    __enable_irq();
    jump();
}

static void schedule_reset(void)
{
    s_reset_pending = 1u;
    s_reset_time_ms = s_tick_ms + IAP_RESET_DELAY_MS;
}

static uint8_t flash_erase_page(uint32_t addr)
{
    uint8_t ok;

    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    CtWatchdog_Feed();
    ok = (FLASH_ErasePage(addr) == FLASH_COMPLETE) ? 1u : 0u;
    CtWatchdog_Feed();
    return ok;
}

static uint8_t flash_program_halfword(uint32_t addr, uint16_t value)
{
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    if (FLASH_ProgramHalfWord(addr, value) != FLASH_COMPLETE)
    {
        CtWatchdog_Feed();
        return 0u;
    }
    CtWatchdog_Feed();
    return (*(__IO uint16_t *)addr == value) ? 1u : 0u;
}

static uint8_t flash_program_bytes(uint32_t addr, const uint8_t *data, uint16_t length)
{
    uint16_t i;
    uint16_t halfword;

    for (i = 0u; i < length; i = (uint16_t)(i + 2u))
    {
        halfword = (uint16_t)data[i] | 0xFF00u;
        if ((uint16_t)(i + 1u) < length)
        {
            halfword = (uint16_t)data[i] | ((uint16_t)data[i + 1u] << 8);
        }
        if (flash_program_halfword(addr + i, halfword) == 0u)
        {
            return 0u;
        }
    }
    return 1u;
}

static uint8_t flash_range_valid(uint32_t offset, uint16_t length)
{
    uint32_t start = CT_SELF_APP_BASE + offset;
    uint32_t end = start + (uint32_t)length;

    if ((length == 0u) || (start < CT_SELF_APP_BASE) || (end > CT_SELF_APP_LIMIT) || (end < start))
    {
        return 0u;
    }
    return 1u;
}

static uint8_t iap_flash_ensure_page_erased(uint32_t page)
{
    uint32_t addr;

    if (page >= IAP_FLASH_APP_PAGE_COUNT)
    {
        return 0u;
    }
    if (s_flash.page_erased[page] != 0u)
    {
        return 1u;
    }

    addr = CT_SELF_APP_BASE + (page * CT_SELF_FLASH_PAGE_SIZE);
    if (flash_erase_page(addr) == 0u)
    {
        return 0u;
    }
    s_flash.page_erased[page] = 1u;
    return 1u;
}

static uint8_t iap_flash_begin(uint8_t owner)
{
    if ((owner == 0u) || ((s_flash.owner != 0u) && (s_flash.owner != owner)))
    {
        return 0u;
    }

    s_flash.owner = owner;
    s_flash.first_page_len = 0u;
    memset(s_flash.page_erased, 0, sizeof(s_flash.page_erased));
    memset(s_flash.first_page, 0xFF, sizeof(s_flash.first_page));

    FLASH_Unlock();
    if (flash_erase_page(CT_SELF_APP_BASE) == 0u)
    {
        FLASH_Lock();
        s_flash.owner = 0u;
        return 0u;
    }
    s_flash.page_erased[0] = 1u;
    FLASH_Lock();
    return 1u;
}

static uint8_t iap_flash_program_direct(uint32_t offset, const uint8_t *data, uint16_t length)
{
    uint32_t addr = CT_SELF_APP_BASE + offset;
    uint32_t page;
    uint32_t last_page;

    if ((data == 0) ||
        (flash_range_valid(offset, length) == 0u) ||
        (offset < CT_SELF_FLASH_PAGE_SIZE))
    {
        return 0u;
    }

    page = offset / CT_SELF_FLASH_PAGE_SIZE;
    last_page = (offset + (uint32_t)length - 1u) / CT_SELF_FLASH_PAGE_SIZE;

    FLASH_Unlock();
    while (page <= last_page)
    {
        if (iap_flash_ensure_page_erased(page) == 0u)
        {
            FLASH_Lock();
            return 0u;
        }
        page++;
    }
    if (flash_program_bytes(addr, data, length) == 0u)
    {
        FLASH_Lock();
        return 0u;
    }
    FLASH_Lock();
    return 1u;
}

static uint8_t iap_flash_write(uint8_t owner, uint32_t offset, const uint8_t *data, uint16_t length)
{
    uint16_t first_len;
    uint32_t first_end;

    if ((s_flash.owner != owner) || (data == 0) || (flash_range_valid(offset, length) == 0u))
    {
        return 0u;
    }

    if (offset < CT_SELF_FLASH_PAGE_SIZE)
    {
        first_len = (uint16_t)(CT_SELF_FLASH_PAGE_SIZE - offset);
        if (first_len > length)
        {
            first_len = length;
        }
        memcpy(&s_flash.first_page[offset], data, first_len);
        first_end = offset + (uint32_t)first_len;
        if (first_end > s_flash.first_page_len)
        {
            s_flash.first_page_len = (uint16_t)first_end;
        }
        if (first_len == length)
        {
            return 1u;
        }
        return iap_flash_program_direct(offset + first_len, &data[first_len], (uint16_t)(length - first_len));
    }

    return iap_flash_program_direct(offset, data, length);
}

static uint8_t vector_valid_in_buffer(uint32_t image_size)
{
    uint32_t msp;
    uint32_t reset;

    if ((image_size < 8u) || (s_flash.first_page_len < 8u))
    {
        return 0u;
    }

    msp = ((uint32_t)s_flash.first_page[0]) |
          ((uint32_t)s_flash.first_page[1] << 8) |
          ((uint32_t)s_flash.first_page[2] << 16) |
          ((uint32_t)s_flash.first_page[3] << 24);
    reset = ((uint32_t)s_flash.first_page[4]) |
            ((uint32_t)s_flash.first_page[5] << 8) |
            ((uint32_t)s_flash.first_page[6] << 16) |
            ((uint32_t)s_flash.first_page[7] << 24);

    if ((msp < 0x20000000u) || (msp >= CT_BOOT_MAILBOX_ADDR))
    {
        return 0u;
    }
    if ((reset < CT_SELF_APP_BASE) ||
        (reset >= (CT_SELF_APP_BASE + image_size)) ||
        ((reset & 1u) == 0u))
    {
        return 0u;
    }
    return 1u;
}

static uint8_t iap_flash_finish(uint8_t owner, uint32_t image_size)
{
    uint16_t length;
    uint16_t tail_len;

    if ((s_flash.owner != owner) ||
        (image_size == 0u) ||
        (image_size > CT_SELF_APP_SIZE))
    {
        return 0u;
    }

    length = (image_size > CT_SELF_FLASH_PAGE_SIZE) ? (uint16_t)CT_SELF_FLASH_PAGE_SIZE : (uint16_t)image_size;
    if ((length < 8u) || (s_flash.first_page_len < length) || (vector_valid_in_buffer(image_size) == 0u))
    {
        return 0u;
    }

    FLASH_Unlock();
    if (flash_erase_page(CT_SELF_APP_BASE) == 0u)
    {
        FLASH_Lock();
        return 0u;
    }

    tail_len = (uint16_t)(length - 8u);
    if ((tail_len > 0u) &&
        (flash_program_bytes(CT_SELF_APP_BASE + 8u, &s_flash.first_page[8], tail_len) == 0u))
    {
        FLASH_Lock();
        return 0u;
    }
    if ((flash_program_bytes(CT_SELF_APP_BASE + 4u, &s_flash.first_page[4], 4u) == 0u) ||
        (flash_program_bytes(CT_SELF_APP_BASE, &s_flash.first_page[0], 4u) == 0u))
    {
        FLASH_Lock();
        return 0u;
    }
    FLASH_Lock();

    if (!valid_app_vector(CT_SELF_APP_BASE, CT_SELF_APP_BASE + image_size))
    {
        return 0u;
    }
    s_flash.owner = 0u;
    s_flash.first_page_len = 0u;
    memset(s_flash.page_erased, 0, sizeof(s_flash.page_erased));
    return 1u;
}

static void iap_flash_abort(uint8_t owner)
{
    if (s_flash.owner == owner)
    {
        s_flash.owner = 0u;
        s_flash.first_page_len = 0u;
        memset(s_flash.page_erased, 0, sizeof(s_flash.page_erased));
    }
}

static void serial_record_fault(void)
{
    if (s_serial_fault_count < 0xFFu)
    {
        s_serial_fault_count++;
    }
}

static uint16_t serial_tx_next(uint16_t index)
{
    index++;
    if (index >= IAP_SERIAL_IRQ_TX_SIZE)
    {
        index = 0u;
    }
    return index;
}

static void serial_irq_tx_reset(void)
{
    s_serial_tx_head = 0u;
    s_serial_tx_tail = 0u;
}

static uint8_t serial_irq_tx_empty(void)
{
    return (s_serial_tx_head == s_serial_tx_tail) ? 1u : 0u;
}

static uint8_t serial_irq_tx_full(void)
{
    return (serial_tx_next(s_serial_tx_head) == s_serial_tx_tail) ? 1u : 0u;
}

static void serial_irq_tx_start(void)
{
    USART_ITConfig(IAP_SERIAL_USART, USART_IT_TXE, ENABLE);
}

static uint8_t serial_irq_tx_push(uint8_t byte)
{
    uint32_t wait = IAP_SERIAL_TX_TIMEOUT_LOOPS;

    while (serial_irq_tx_full() != 0u)
    {
        CtWatchdog_Feed();
        if (wait == 0u)
        {
            serial_record_fault();
            return 0u;
        }
        wait--;
    }

    s_serial_irq_tx[s_serial_tx_head] = byte;
    s_serial_tx_head = serial_tx_next(s_serial_tx_head);
    serial_irq_tx_start();
    return 1u;
}

static void serial_irq_tx_service(void)
{
    if (s_serial_tx_tail == s_serial_tx_head)
    {
        USART_ITConfig(IAP_SERIAL_USART, USART_IT_TXE, DISABLE);
        return;
    }

    USART_SendData(IAP_SERIAL_USART, s_serial_irq_tx[s_serial_tx_tail]);
    s_serial_tx_tail = serial_tx_next(s_serial_tx_tail);
}

static uint8_t serial_wait_tx_done(void)
{
    uint32_t wait = IAP_SERIAL_TX_TIMEOUT_LOOPS;

    while ((wait > 0u) &&
           ((serial_irq_tx_empty() == 0u) ||
            (USART_GetFlagStatus(IAP_SERIAL_USART, USART_FLAG_TC) == RESET)))
    {
        CtWatchdog_Feed();
        wait--;
    }
    if (wait == 0u)
    {
        serial_record_fault();
        return 0u;
    }
    return 1u;
}

static void serial_delay_ms(uint32_t delay_ms)
{
    uint32_t start = s_tick_ms;

    while ((uint32_t)(s_tick_ms - start) < delay_ms)
    {
        CtWatchdog_Feed();
    }
}

static uint8_t serial_write(const uint8_t *data, uint16_t length)
{
    uint16_t i;

    for (i = 0u; i < length; ++i)
    {
        if (serial_irq_tx_push(data[i]) == 0u)
        {
            return 0u;
        }
    }
    return serial_wait_tx_done();
}

static void serial_send_ack(const uint8_t *request)
{
    uint8_t ack[8];
    uint16_t crc;

    memcpy(ack, request, 6u);
    crc = crc16_calc(ack, 6u);
    ack[6] = (uint8_t)crc;
    ack[7] = (uint8_t)(crc >> 8);
    serial_delay_ms(IAP_SERIAL_RESPONSE_DELAY_MS);
    (void)serial_write(ack, (uint16_t)sizeof(ack));
}

static void serial_send_error(const uint8_t *request, uint8_t error);

static uint16_t serial_status_word(uint16_t addr)
{
    uint16_t offset;

    if (addr >= LEGACY_RO_STATUS_ADDR)
    {
        offset = (uint16_t)(addr - LEGACY_RO_STATUS_ADDR);
    }
    else if (addr >= LEGACY_RO_START)
    {
        offset = (uint16_t)(addr - LEGACY_RO_START);
    }
    else
    {
        return 0u;
    }

    switch (offset)
    {
    case 0u:
        return s_serial_block_count;
    case 1u:
        return (uint16_t)(s_serial_written & 0xFFFFu);
    case 2u:
        return (uint16_t)(s_serial_written >> 16);
    case 3u:
        return (uint16_t)s_serial_fault_count;
    case 4u:
        return (uint16_t)s_flash.owner;
    default:
        return 0u;
    }
}

static void serial_send_read_regs(const uint8_t *request)
{
    uint8_t ack[3u + (LEGACY_READ_WORDS_MAX * 2u) + 2u];
    uint16_t addr;
    uint16_t count;
    uint16_t value;
    uint16_t crc;
    uint16_t i;
    uint16_t pos;

    addr = rd_be16(&request[2]);
    count = rd_be16(&request[4]);
    if ((count == 0u) || (count > LEGACY_READ_WORDS_MAX) || (addr < LEGACY_RO_START))
    {
        serial_send_error(request, LEGACY_ERR_ADDR_INVALID);
        return;
    }

    ack[0] = request[0];
    ack[1] = LEGACY_CMD_READ_REGS;
    ack[2] = (uint8_t)(count * 2u);
    pos = 3u;
    for (i = 0u; i < count; ++i)
    {
        value = serial_status_word((uint16_t)(addr + i));
        wr_be16(&ack[pos], value);
        pos = (uint16_t)(pos + 2u);
    }

    crc = crc16_calc(ack, pos);
    ack[pos++] = (uint8_t)crc;
    ack[pos++] = (uint8_t)(crc >> 8);
    serial_delay_ms(IAP_SERIAL_RESPONSE_DELAY_MS);
    (void)serial_write(ack, pos);
}

static void serial_send_error(const uint8_t *request, uint8_t error)
{
    uint8_t ack[5];
    uint16_t crc;

    if (s_serial_fault_count < 0xFFu)
    {
        s_serial_fault_count++;
    }
    ack[0] = request[0];
    ack[1] = request[1] | 0x80u;
    ack[2] = error;
    crc = crc16_calc(ack, 3u);
    ack[3] = (uint8_t)crc;
    ack[4] = (uint8_t)(crc >> 8);
    serial_delay_ms(IAP_SERIAL_RESPONSE_DELAY_MS);
    (void)serial_write(ack, (uint16_t)sizeof(ack));
}

static uint8_t serial_connect(uint16_t count)
{
    if (count != 1u)
    {
        return 0u;
    }
    s_serial_block_count = 0u;
    s_serial_written = 0u;
    return iap_flash_begin(1u);
}

static uint8_t serial_write_block(uint16_t declared_length, uint8_t byte_count, const uint8_t *payload)
{
    uint16_t length;
    uint32_t offset;

    length = (byte_count != 0u) ? (uint16_t)byte_count : declared_length;
    if ((length == 0u) || (length > IAP_SERIAL_BLOCK_MAX) || (payload == 0))
    {
        return 0u;
    }
    if (s_flash.owner == 0u)
    {
        if (!serial_connect(1u))
        {
            return 0u;
        }
    }
    offset = (uint32_t)s_serial_block_count * (uint32_t)IAP_SERIAL_BLOCK_MAX;
    if (!iap_flash_write(1u, offset, payload, length))
    {
        return 0u;
    }
    s_serial_written = offset + (uint32_t)length;
    s_serial_block_count++;
    return 1u;
}

static uint8_t serial_complete(uint16_t count)
{
    if ((count != 1u) || (s_serial_written == 0u))
    {
        return 0u;
    }
    return iap_flash_finish(1u, s_serial_written);
}

static void serial_handle_frame(void)
{
    uint16_t expect_crc;
    uint16_t actual_crc;
    uint16_t addr;
    uint16_t count;
    uint8_t ok = 0u;

    expect_crc = (uint16_t)s_serial_rx[s_serial_expect - 2u] |
                 ((uint16_t)s_serial_rx[s_serial_expect - 1u] << 8);
    actual_crc = crc16_calc(s_serial_rx, (uint16_t)(s_serial_expect - 2u));
    if (expect_crc != actual_crc)
    {
        serial_send_error(s_serial_rx, LEGACY_ERR_CRC);
        return;
    }

    if (s_serial_rx[1] == LEGACY_CMD_READ_REGS)
    {
        serial_send_read_regs(s_serial_rx);
        return;
    }
    if (s_serial_rx[1] != LEGACY_CMD_WRITE_REGS)
    {
        serial_send_error(s_serial_rx, LEGACY_ERR_CMD_INVALID);
        return;
    }

    addr = rd_be16(&s_serial_rx[2]);
    count = rd_be16(&s_serial_rx[4]);
    switch (addr)
    {
    case LEGACY_FLASH_CONNECT_ADDR:
        ok = serial_connect(count);
        break;
    case LEGACY_FLASH_UPGRADE_ADDR:
        ok = serial_write_block(count, s_serial_rx[6], &s_serial_rx[7]);
        break;
    case LEGACY_FLASH_COMPLETE_ADDR:
        ok = serial_complete(count);
        if (ok != 0u)
        {
            schedule_reset();
        }
        break;
    default:
        ok = 0u;
        break;
    }

    if (ok != 0u)
    {
        serial_send_ack(s_serial_rx);
    }
    else
    {
        serial_send_error(s_serial_rx, LEGACY_ERR_CMD_INVALID);
    }
}

static void serial_reset_parser(void)
{
    s_serial_index = 0u;
    s_serial_expect = 0u;
}

static void serial_irq_rx_reset(void)
{
    s_serial_irq_head = 0u;
    s_serial_irq_tail = 0u;
}

static void serial_irq_rx_push(uint8_t byte)
{
    uint16_t next = (uint16_t)(s_serial_irq_head + 1u);

    if (next >= IAP_SERIAL_IRQ_RX_SIZE)
    {
        next = 0u;
    }
    if (next == s_serial_irq_tail)
    {
        serial_record_fault();
        return;
    }

    s_serial_irq_rx[s_serial_irq_head] = byte;
    s_serial_irq_head = next;
}

static uint8_t serial_irq_rx_pop(uint8_t *byte)
{
    uint16_t next;

    if ((byte == 0) || (s_serial_irq_tail == s_serial_irq_head))
    {
        return 0u;
    }

    *byte = s_serial_irq_rx[s_serial_irq_tail];
    next = (uint16_t)(s_serial_irq_tail + 1u);
    if (next >= IAP_SERIAL_IRQ_RX_SIZE)
    {
        next = 0u;
    }
    s_serial_irq_tail = next;
    return 1u;
}

static void serial_clear_overrun(void)
{
    if (USART_GetFlagStatus(IAP_SERIAL_USART, USART_FLAG_ORE) != RESET)
    {
        (void)IAP_SERIAL_USART->SR;
        (void)IAP_SERIAL_USART->DR;
        serial_record_fault();
    }
}

static void serial_check_frame_timeout(void)
{
    if ((s_serial_index != 0u) &&
        ((uint32_t)(s_tick_ms - s_serial_last_rx_ms) >= IAP_SERIAL_FRAME_TIMEOUT_MS))
    {
        serial_reset_parser();
    }
}

static void serial_feed(uint8_t byte)
{
    uint16_t payload_len;

    serial_check_frame_timeout();
    if (s_serial_index == 0u)
    {
        if ((byte != LEGACY_SLAVE_ADDR) && (byte != LEGACY_BROADCAST_ADDR))
        {
            return;
        }
    }
    else if (s_serial_index == 1u)
    {
        if ((byte != LEGACY_CMD_WRITE_REGS) && (byte != LEGACY_CMD_READ_REGS))
        {
            serial_reset_parser();
            return;
        }
    }

    if (s_serial_index >= IAP_SERIAL_RX_SIZE)
    {
        serial_reset_parser();
        return;
    }
    s_serial_rx[s_serial_index++] = byte;
    s_serial_last_rx_ms = s_tick_ms;

    if (s_serial_index == 6u)
    {
        if (s_serial_rx[1] == LEGACY_CMD_READ_REGS)
        {
            s_serial_expect = 8u;
        }
    }
    else if (s_serial_index == 7u)
    {
        if (s_serial_rx[1] == LEGACY_CMD_WRITE_REGS)
        {
            payload_len = (s_serial_rx[6] != 0u) ? s_serial_rx[6] : rd_be16(&s_serial_rx[4]);
            s_serial_expect = (uint16_t)(9u + payload_len);
            if ((s_serial_expect > IAP_SERIAL_RX_SIZE) || (s_serial_expect < 9u))
            {
                serial_reset_parser();
                return;
            }
        }
    }

    if ((s_serial_expect != 0u) && (s_serial_index >= s_serial_expect))
    {
        serial_handle_frame();
        serial_reset_parser();
    }
}

static uint32_t can_ctrl_id(uint8_t node)
{
    return CAN_IAP_CTRL_BASE_ID | (uint32_t)node;
}

static uint32_t can_ack_id(uint8_t node)
{
    return CAN_IAP_ACK_BASE_ID | (uint32_t)node;
}

static void can_build_ack(uint8_t cmd, uint8_t status, uint8_t code, uint8_t data[8])
{
    data[0] = CAN_IAP_CMD_ACK;
    data[1] = cmd;
    data[2] = status;
    wr_be16(&data[3], s_can.expect_seq);
    data[5] = code;
    data[6] = 0xFFu;
    data[7] = 0xFFu;
}

static void can_build_nack(uint8_t cmd, uint8_t code, uint8_t data[8])
{
    data[0] = CAN_IAP_CMD_NACK;
    data[1] = cmd;
    data[2] = code;
    wr_be16(&data[3], s_can.expect_seq);
    data[5] = code;
    data[6] = 0xFFu;
    data[7] = 0xFFu;
}

static uint8_t can_transmit(CanTxMsg *tx)
{
    uint32_t wait = 60000u;
    uint8_t mailbox;

    mailbox = CAN_Transmit(CAN1, tx);
    if (mailbox >= 3u)
    {
        return 0u;
    }
    while ((wait > 0u) && (CAN_TransmitStatus(CAN1, mailbox) == CAN_TxStatus_Pending))
    {
        CtWatchdog_Feed();
        --wait;
    }
    if (CAN_TransmitStatus(CAN1, mailbox) == CAN_TxStatus_Ok)
    {
        return 1u;
    }
    CAN_CancelTransmit(CAN1, mailbox);
    return 0u;
}

static void can_send_ack(uint8_t cmd, uint8_t status, uint8_t code)
{
    CanTxMsg tx;

    s_can.last_error = code;
    memset(&tx, 0, sizeof(tx));
    tx.ExtId = can_ack_id(s_can.node);
    tx.IDE = CAN_ID_EXT;
    tx.RTR = CAN_RTR_DATA;
    tx.DLC = 8u;
    can_build_ack(cmd, status, code, tx.Data);
    (void)can_transmit(&tx);
}

static void can_send_nack(uint8_t cmd, uint8_t code)
{
    CanTxMsg tx;

    s_can.last_error = code;
    memset(&tx, 0, sizeof(tx));
    tx.ExtId = can_ack_id(s_can.node);
    tx.IDE = CAN_ID_EXT;
    tx.RTR = CAN_RTR_DATA;
    tx.DLC = 8u;
    can_build_nack(cmd, code, tx.Data);
    (void)can_transmit(&tx);
}

static void can_reset_runtime(uint8_t state)
{
    memset(&s_can, 0, sizeof(s_can));
    s_can.state = state;
    s_can.node = CAN_IAP_NODE_DEFAULT;
    s_can.running_crc = 0xFFFFu;
}

static void can_handle_hello(const CanRxMsg *rx)
{
    if ((rx->DLC != 8u) || (rx->Data[1] != CAN_IAP_PROTOCOL_VERSION))
    {
        can_send_nack(CAN_IAP_CMD_HELLO, CAN_IAP_ERR_BAD_PARAM);
        return;
    }
    s_can.node = rx->Data[2];
    if ((s_can.node == 0u) || (s_can.node > 0x7Fu))
    {
        s_can.node = CAN_IAP_NODE_DEFAULT;
    }
    can_send_ack(CAN_IAP_CMD_HELLO, s_can.state, CAN_IAP_ERR_OK);
}

static void can_handle_start(const CanRxMsg *rx)
{
    uint32_t size;
    uint16_t crc;
    uint8_t node;

    if ((rx->DLC != 8u) || (rx->Data[1] != CAN_IAP_PROTOCOL_VERSION))
    {
        can_send_nack(CAN_IAP_CMD_START, CAN_IAP_ERR_BAD_PARAM);
        return;
    }
    size = rd_be32(&rx->Data[2]);
    crc = rd_be16(&rx->Data[6]);
    if ((size == 0u) || (size > CT_SELF_APP_SIZE))
    {
        can_send_nack(CAN_IAP_CMD_START, CAN_IAP_ERR_BAD_PARAM);
        return;
    }
    node = (uint8_t)(rx->ExtId & 0xFFu);
    if ((node == 0u) || (node > 0x7Fu))
    {
        node = CAN_IAP_NODE_DEFAULT;
    }
    if (!iap_flash_begin(2u))
    {
        can_send_nack(CAN_IAP_CMD_START, CAN_IAP_ERR_FLASH);
        return;
    }

    can_reset_runtime(1u);
    s_can.node = node;
    s_can.fw_size = size;
    s_can.fw_crc = crc;
    s_can.last_rx_ms = s_tick_ms;
    can_send_ack(CAN_IAP_CMD_START, s_can.state, CAN_IAP_ERR_OK);
}

static void can_handle_data(const CanRxMsg *rx)
{
    uint16_t seq;

    if (s_can.state != 1u)
    {
        can_send_nack(0u, CAN_IAP_ERR_BAD_STATE);
        return;
    }
    seq = (uint16_t)((rx->ExtId >> 8) & 0xFFFFu);
    if ((rx->DLC != 8u) ||
        (seq != s_can.expect_seq) ||
        ((uint16_t)(s_can.block_bytes + 8u) > IAP_CAN_BLOCK_BYTES))
    {
        can_send_nack(0u, CAN_IAP_ERR_BAD_SEQ);
        return;
    }
    memcpy(&s_can.block[s_can.block_bytes], rx->Data, 8u);
    s_can.block_bytes = (uint16_t)(s_can.block_bytes + 8u);
    s_can.expect_seq++;
    s_can.last_rx_ms = s_tick_ms;
}

static void can_handle_commit(const CanRxMsg *rx)
{
    uint16_t block_seq;
    uint16_t block_len;
    uint16_t block_crc;
    uint32_t next_written;

    if ((s_can.state != 1u) || (rx->DLC != 8u))
    {
        can_send_nack(CAN_IAP_CMD_COMMIT, CAN_IAP_ERR_BAD_STATE);
        return;
    }
    block_seq = rd_be16(&rx->Data[1]);
    block_len = rd_be16(&rx->Data[3]);
    block_crc = rd_be16(&rx->Data[5]);
    next_written = s_can.written + (uint32_t)block_len;
    if ((block_seq != s_can.block_seq) ||
        (block_len == 0u) ||
        (block_len > s_can.block_bytes) ||
        (next_written > s_can.fw_size))
    {
        can_send_nack(CAN_IAP_CMD_COMMIT, CAN_IAP_ERR_BAD_PARAM);
        return;
    }
    if (crc16_calc(s_can.block, block_len) != block_crc)
    {
        can_send_nack(CAN_IAP_CMD_COMMIT, CAN_IAP_ERR_CRC);
        return;
    }
    if (!iap_flash_write(2u, s_can.written, s_can.block, block_len))
    {
        s_can.state = 3u;
        can_send_nack(CAN_IAP_CMD_COMMIT, CAN_IAP_ERR_FLASH);
        return;
    }
    s_can.running_crc = crc16_update(s_can.running_crc, s_can.block, block_len);
    s_can.written = next_written;
    s_can.block_seq++;
    s_can.block_bytes = 0u;
    s_can.last_rx_ms = s_tick_ms;
    can_send_ack(CAN_IAP_CMD_COMMIT, s_can.state, CAN_IAP_ERR_OK);
}

static void can_handle_end(const CanRxMsg *rx)
{
    uint16_t frame_count;
    uint16_t crc;

    if ((s_can.state != 1u) || (rx->DLC != 8u))
    {
        can_send_nack(CAN_IAP_CMD_END, CAN_IAP_ERR_BAD_STATE);
        return;
    }
    frame_count = rd_be16(&rx->Data[1]);
    crc = rd_be16(&rx->Data[3]);
    if ((frame_count != s_can.expect_seq) ||
        (s_can.written != s_can.fw_size) ||
        (crc != s_can.fw_crc) ||
        (s_can.running_crc != s_can.fw_crc))
    {
        can_send_nack(CAN_IAP_CMD_END, CAN_IAP_ERR_CRC);
        return;
    }
    if (!iap_flash_finish(2u, s_can.fw_size))
    {
        can_send_nack(CAN_IAP_CMD_END, CAN_IAP_ERR_APP_INVALID);
        return;
    }
    s_can.state = 2u;
    can_send_ack(CAN_IAP_CMD_END, s_can.state, CAN_IAP_ERR_OK);
    schedule_reset();
}

static void can_handle_abort(void)
{
    iap_flash_abort(2u);
    can_reset_runtime(0u);
    can_send_ack(CAN_IAP_CMD_ABORT, s_can.state, CAN_IAP_ERR_OK);
}

static void can_handle_ctrl(const CanRxMsg *rx)
{
    uint8_t cmd;

    if (rx->DLC == 0u)
    {
        return;
    }
    cmd = rx->Data[0];
    s_can.last_cmd = cmd;
    switch (cmd)
    {
    case CAN_IAP_CMD_HELLO:
        can_handle_hello(rx);
        break;
    case CAN_IAP_CMD_START:
        can_handle_start(rx);
        break;
    case CAN_IAP_CMD_COMMIT:
        can_handle_commit(rx);
        break;
    case CAN_IAP_CMD_END:
        can_handle_end(rx);
        break;
    case CAN_IAP_CMD_ABORT:
        can_handle_abort();
        break;
    default:
        can_send_nack(cmd, CAN_IAP_ERR_BAD_CMD);
        break;
    }
}

static void can_handle_rx(const CanRxMsg *rx)
{
    uint8_t node;

    if ((rx->IDE != CAN_ID_EXT) || (rx->RTR != CAN_RTR_DATA))
    {
        return;
    }
    node = (uint8_t)(rx->ExtId & 0xFFu);
    if ((node != s_can.node) && (node != CAN_IAP_NODE_DEFAULT))
    {
        return;
    }
    if (rx->ExtId == can_ctrl_id(node))
    {
        s_can.node = node;
        can_handle_ctrl(rx);
    }
    else if ((rx->ExtId & CAN_IAP_DATA_ID_MASK) == CAN_IAP_DATA_BASE_ID)
    {
        s_can.node = node;
        can_handle_data(rx);
    }
}

static void can_poll(void)
{
    CanRxMsg rx;

    while (CAN_MessagePending(CAN1, CAN_FIFO0) > 0u)
    {
        CAN_Receive(CAN1, CAN_FIFO0, &rx);
        can_handle_rx(&rx);
    }
}

static void iap_gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO |
                           RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_GPIOC |
                           RCC_APB2Periph_GPIOD,
                           ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;

    gpio.GPIO_Pin = GPIO_Pin_15;
    GPIO_Init(GPIOB, &gpio);
    GPIO_ResetBits(GPIOB, GPIO_Pin_15);

    gpio.GPIO_Pin = GPIO_Pin_12;
    GPIO_Init(GPIOC, &gpio);
    GPIO_ResetBits(GPIOC, GPIO_Pin_12);

    gpio.GPIO_Pin = GPIO_Pin_13;
    GPIO_Init(GPIOC, &gpio);
    GPIO_ResetBits(GPIOC, GPIO_Pin_13);

    gpio.GPIO_Pin = GPIO_Pin_2;
    GPIO_Init(GPIOD, &gpio);
    GPIO_SetBits(GPIOD, GPIO_Pin_2);
}

static void iap_uart_init(void)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef nvic;

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

    USART_StructInit(&usart);
    usart.USART_BaudRate = IAP_SERIAL_BAUD;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(IAP_SERIAL_USART, &usart);
    serial_irq_rx_reset();
    serial_irq_tx_reset();
    NVIC_ClearPendingIRQ(IAP_SERIAL_IRQn);
    nvic.NVIC_IRQChannel = IAP_SERIAL_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1u;
    nvic.NVIC_IRQChannelSubPriority = 0u;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);
    USART_ITConfig(IAP_SERIAL_USART, USART_IT_RXNE, ENABLE);
    USART_Cmd(IAP_SERIAL_USART, ENABLE);
}

static void iap_can_init(void)
{
    GPIO_InitTypeDef gpio;
    CAN_InitTypeDef can;
    CAN_FilterInitTypeDef filter;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_11;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_12;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &gpio);

    CAN_DeInit(CAN1);
    CAN_StructInit(&can);
    can.CAN_TTCM = DISABLE;
    can.CAN_ABOM = ENABLE;
    can.CAN_AWUM = DISABLE;
    can.CAN_NART = DISABLE;
    can.CAN_RFLM = DISABLE;
    can.CAN_TXFP = DISABLE;
    can.CAN_Mode = CAN_Mode_Normal;
    can.CAN_SJW = CAN_SJW_1tq;
    can.CAN_BS1 = CAN_BS1_5tq;
    can.CAN_BS2 = CAN_BS2_2tq;
    can.CAN_Prescaler = IAP_CAN_PRESCALER_250K;
    (void)CAN_Init(CAN1, &can);

    filter.CAN_FilterNumber = 0u;
    filter.CAN_FilterMode = CAN_FilterMode_IdMask;
    filter.CAN_FilterScale = CAN_FilterScale_32bit;
    filter.CAN_FilterIdHigh = 0u;
    filter.CAN_FilterIdLow = 0u;
    filter.CAN_FilterMaskIdHigh = 0u;
    filter.CAN_FilterMaskIdLow = 0u;
    filter.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;
    filter.CAN_FilterActivation = ENABLE;
    CAN_FilterInit(&filter);
}

static void iap_task_1ms(void)
{
    static uint32_t last_led_ms;

    if ((uint32_t)(s_tick_ms - last_led_ms) >= IAP_LED_PERIOD_MS)
    {
        last_led_ms = s_tick_ms;
        if (GPIO_ReadOutputDataBit(GPIOB, GPIO_Pin_15) != Bit_RESET)
        {
            GPIO_ResetBits(GPIOB, GPIO_Pin_15);
        }
        else
        {
            GPIO_SetBits(GPIOB, GPIO_Pin_15);
        }
    }

    serial_check_frame_timeout();

    if ((s_can.state == 1u) && ((uint32_t)(s_tick_ms - s_can.last_rx_ms) >= IAP_CAN_RX_TIMEOUT_MS))
    {
        s_can.state = 3u;
        s_can.last_error = CAN_IAP_ERR_BAD_STATE;
        iap_flash_abort(2u);
    }

    if ((s_reset_pending != 0u) && ((int32_t)(s_tick_ms - s_reset_time_ms) >= 0))
    {
        s_reset_pending = 0u;
        NVIC_SystemReset();
    }
}

static void iap_init(void)
{
    SystemCoreClockUpdate();
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    iap_gpio_init();
    iap_uart_init();
    iap_can_init();
    can_reset_runtime(0u);
    (void)SysTick_Config(SystemCoreClock / 1000u);
    CtWatchdog_Init();
}

void CtIap_Run(void)
{
    uint8_t byte;

    SystemInit();
    if ((boot_consume_iap_request() == 0) && valid_app_vector(CT_SELF_APP_BASE, CT_SELF_APP_LIMIT))
    {
        jump_to_app();
    }

    iap_init();
    while (1)
    {
        while (serial_irq_rx_pop(&byte) != 0u)
        {
            serial_feed(byte);
        }
        serial_clear_overrun();
        can_poll();
        iap_task_1ms();
        CtWatchdog_Feed();
    }
}

void SysTick_Handler(void)
{
    s_tick_ms++;
}

void IAP_SERIAL_IRQHandler(void)
{
    if (USART_GetITStatus(IAP_SERIAL_USART, USART_IT_RXNE) != RESET)
    {
        serial_irq_rx_push((uint8_t)USART_ReceiveData(IAP_SERIAL_USART));
    }
    if (USART_GetITStatus(IAP_SERIAL_USART, USART_IT_TXE) != RESET)
    {
        serial_irq_tx_service();
    }
    serial_clear_overrun();
}

void USB_LP_CAN1_RX0_IRQHandler(void)
{
}
