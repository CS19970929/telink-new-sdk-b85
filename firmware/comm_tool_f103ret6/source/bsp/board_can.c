#include "board_can.h"
#include "board.h"
#include "ct_debug_log.h"
#include "stm32f10x_can.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "misc.h"
#include <string.h>

#define BOARD_CAN_RX_QUEUE_SIZE        32u

static volatile uint8_t s_rx_head;
static volatile uint8_t s_rx_tail;
static CtCanFrame s_rx_queue[BOARD_CAN_RX_QUEUE_SIZE];
static CtCanDiag s_diag;

static void can_diag_latch_regs(void)
{
    s_diag.last_esr = CAN1->ESR;
    s_diag.last_tsr = CAN1->TSR;
    s_diag.last_msr = CAN1->MSR;
    s_diag.last_rf0r = CAN1->RF0R;
}

static uint16_t bitrate_to_prescaler(uint32_t bitrate)
{
    switch (bitrate)
    {
    case 125000u:
        return 8u;
    case 250000u:
        return 4u;
    case 500000u:
        return 2u;
    default:
        return 0u;
    }
}

static void can_rx_push(const CanRxMsg *msg)
{
    uint8_t next;
    uint8_t i;
    CtCanFrame *slot;

    next = (uint8_t)((s_rx_head + 1u) % BOARD_CAN_RX_QUEUE_SIZE);
    if (next == s_rx_tail)
    {
        s_diag.rx_drop++;
        can_diag_latch_regs();
        return;
    }

    slot = &s_rx_queue[s_rx_head];
    memset(slot, 0, sizeof(*slot));
    slot->ide = (msg->IDE == CAN_ID_EXT) ? 1u : 0u;
    slot->id = (slot->ide != 0u) ? msg->ExtId : msg->StdId;
    slot->dlc = msg->DLC;
    for (i = 0u; (i < msg->DLC) && (i < 8u); ++i)
    {
        slot->data[i] = msg->Data[i];
    }
    s_rx_head = next;
    s_diag.rx_count++;
    s_diag.last_rx_id = slot->id;
    s_diag.last_rx_ide = slot->ide;
    s_diag.last_rx_dlc = slot->dlc;
    memcpy(s_diag.last_rx_data, slot->data, sizeof(s_diag.last_rx_data));
    can_diag_latch_regs();
}

static int can_rx_pop(CtCanFrame *frame)
{
    if ((frame == 0) || (s_rx_head == s_rx_tail))
    {
        return 0;
    }

    *frame = s_rx_queue[s_rx_tail];
    s_rx_tail = (uint8_t)((s_rx_tail + 1u) % BOARD_CAN_RX_QUEUE_SIZE);
    return 1;
}

static void can_gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA, ENABLE);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_11;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin = GPIO_Pin_12;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &gpio);
}

static int can_hw_init(uint32_t bitrate)
{
    CAN_InitTypeDef can;
    CAN_FilterInitTypeDef filter;
    NVIC_InitTypeDef nvic;
    uint16_t prescaler;

    prescaler = bitrate_to_prescaler(bitrate);
    if (prescaler == 0u)
    {
        return 0;
    }

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE);
    CAN_DeInit(CAN1);
    s_rx_head = 0u;
    s_rx_tail = 0u;
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
    can.CAN_Prescaler = prescaler;
    if (CAN_Init(CAN1, &can) != CAN_InitStatus_Success)
    {
        return 0;
    }

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

    nvic.NVIC_IRQChannel = USB_LP_CAN1_RX0_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1u;
    nvic.NVIC_IRQChannelSubPriority = 1u;
    nvic.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic);

    CAN_ITConfig(CAN1, CAN_IT_FMP0, ENABLE);
    return 1;
}

void BoardCan_Init(uint32_t bitrate)
{
    can_gpio_init();
    (void)can_hw_init(bitrate);
}

int CtBoard_SetCanBitrate(uint32_t bitrate)
{
    int result;

    result = can_hw_init(bitrate);
    if (result == 0)
    {
        CtDebugLog_Record(CT_LOG_MOD_CAN,
                          CT_LOG_EVT_CAN_TX_FAIL,
                          (uint16_t)(bitrate / 1000u),
                          0u);
    }
    return result;
}

int CtBoard_CanSend(const CtCanFrame *frame, uint32_t timeout_ms)
{
    CanTxMsg msg;
    uint8_t mailbox;
    uint8_t i;
    uint32_t start;
    uint8_t tx_status;

    if ((frame == 0) || (frame->dlc > 8u))
    {
        return 0;
    }

    s_diag.tx_count++;
    s_diag.last_tx_id = frame->id;
    s_diag.last_tx_ide = frame->ide;
    s_diag.last_tx_dlc = frame->dlc;
    s_diag.last_tx_status = 0u;
    can_diag_latch_regs();

    memset(&msg, 0, sizeof(msg));
    msg.IDE = (frame->ide != 0u) ? CAN_ID_EXT : CAN_ID_STD;
    msg.RTR = CAN_RTR_DATA;
    msg.DLC = frame->dlc;
    if (msg.IDE == CAN_ID_EXT)
    {
        msg.ExtId = frame->id & 0x1FFFFFFFu;
    }
    else
    {
        msg.StdId = (uint16_t)(frame->id & 0x7FFu);
    }
    for (i = 0u; i < frame->dlc; ++i)
    {
        msg.Data[i] = frame->data[i];
    }

    mailbox = CAN_Transmit(CAN1, &msg);
    if (mailbox == CAN_TxStatus_NoMailBox)
    {
        s_diag.tx_fail++;
        s_diag.last_tx_status = CAN_TxStatus_NoMailBox;
        can_diag_latch_regs();
        CtDebugLog_Record(CT_LOG_MOD_CAN,
                          CT_LOG_EVT_CAN_TX_FAIL,
                          (uint16_t)(frame->id & 0xFFFFu),
                          CAN_TxStatus_NoMailBox);
        return 0;
    }

    start = CtBoard_GetTickMs();
    while (CAN_TransmitStatus(CAN1, mailbox) == CAN_TxStatus_Pending)
    {
        if ((uint32_t)(CtBoard_GetTickMs() - start) >= timeout_ms)
        {
            CAN_CancelTransmit(CAN1, mailbox);
            s_diag.tx_timeout++;
            s_diag.last_tx_status = CAN_TxStatus_Pending;
            can_diag_latch_regs();
            CtDebugLog_Record(CT_LOG_MOD_CAN,
                              CT_LOG_EVT_CAN_TX_TIMEOUT,
                              (uint16_t)(frame->id & 0xFFFFu),
                              mailbox);
            return 0;
        }
    }

    tx_status = CAN_TransmitStatus(CAN1, mailbox);
    s_diag.last_tx_status = tx_status;
    can_diag_latch_regs();
    if (tx_status == CAN_TxStatus_Ok)
    {
        s_diag.tx_ok++;
        return 1;
    }

    s_diag.tx_fail++;
    CAN_CancelTransmit(CAN1, mailbox);
    CtDebugLog_Record(CT_LOG_MOD_CAN,
                      CT_LOG_EVT_CAN_TX_FAIL,
                      (uint16_t)(frame->id & 0xFFFFu),
                      tx_status);
    return 0;
}

int CtBoard_CanRecv(CtCanFrame *frame, uint32_t timeout_ms)
{
    uint32_t start;

    start = CtBoard_GetTickMs();
    do
    {
        if (can_rx_pop(frame))
        {
            return 1;
        }
    } while ((uint32_t)(CtBoard_GetTickMs() - start) < timeout_ms);

    return 0;
}

void USB_LP_CAN1_RX0_IRQHandler(void)
{
    CanRxMsg msg;

    while (CAN_MessagePending(CAN1, CAN_FIFO0) != 0u)
    {
        CAN_Receive(CAN1, CAN_FIFO0, &msg);
        can_rx_push(&msg);
    }
}

void CtBoard_CanGetDiag(CtCanDiag *diag)
{
    if (diag == 0)
    {
        return;
    }

    can_diag_latch_regs();
    *diag = s_diag;
}

void CtBoard_CanClearDiag(void)
{
    memset(&s_diag, 0, sizeof(s_diag));
    can_diag_latch_regs();
}
