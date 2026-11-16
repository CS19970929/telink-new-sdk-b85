#include "ct_upgrade_manager.h"
#include "ct_board_port.h"
#include "ct_can_gateway.h"
#include "ct_config.h"
#include "ct_crc16.h"
#include "ct_debug_log.h"
#include "ct_flash_store.h"
#include <string.h>

#define CT_UPGRADE_FAST_HELLO_TIMEOUT_MS       700u
#define CT_UPGRADE_BOOT_DELAY_MS               1200u
#define CT_UPGRADE_IAP_HELLO_TIMEOUT_MS        8000u
#define CT_UPGRADE_HELLO_RETRY_INTERVAL_MS     250u

enum
{
    CT_UPGRADE_IDLE = CT_UPGRADE_STATE_IDLE,
    CT_UPGRADE_RUNNING = CT_UPGRADE_STATE_RUNNING,
    CT_UPGRADE_DONE = CT_UPGRADE_STATE_DONE,
    CT_UPGRADE_ERROR = CT_UPGRADE_STATE_ERROR,
    CT_UPGRADE_ABORTED = CT_UPGRADE_STATE_ABORTED
};

enum
{
    CT_UPGRADE_PHASE_IDLE = 0u,
    CT_UPGRADE_PHASE_HELLO_FAST_SEND,
    CT_UPGRADE_PHASE_HELLO_FAST_WAIT,
    CT_UPGRADE_PHASE_ENTER_APP_IAP,
    CT_UPGRADE_PHASE_BOOT_DELAY,
    CT_UPGRADE_PHASE_HELLO_IAP_SEND,
    CT_UPGRADE_PHASE_HELLO_IAP_WAIT,
    CT_UPGRADE_PHASE_START_SEND,
    CT_UPGRADE_PHASE_START_WAIT,
    CT_UPGRADE_PHASE_LOAD_BLOCK,
    CT_UPGRADE_PHASE_SEND_DATA,
    CT_UPGRADE_PHASE_SEND_COMMIT,
    CT_UPGRADE_PHASE_COMMIT_WAIT,
    CT_UPGRADE_PHASE_SEND_END,
    CT_UPGRADE_PHASE_END_WAIT
};

typedef struct
{
    uint8_t phase;
    uint8_t node;
    uint8_t app_can_addr;
    uint8_t frame_index;
    uint8_t frames_this_block;
    uint32_t phase_start_ms;
    uint32_t offset;
    uint16_t seq;
    uint16_t block_seq;
    uint16_t chunk_len;
    uint16_t block_crc;
    uint32_t last_tx_ms;
    uint8_t block[CT_IAP_BLOCK_BYTES];
} CtUpgradeContext;

static CtUpgradeStatus s_status;
static CtUpgradeContext s_ctx;
static uint8_t s_abort;

static int timeout_expired(uint32_t start, uint32_t timeout_ms)
{
    return ((uint32_t)(CtBoard_GetTickMs() - start) >= timeout_ms) ? 1 : 0;
}

static void set_error(uint8_t err)
{
    s_status.state = CT_UPGRADE_ERROR;
    s_status.last_error = err;
    s_ctx.phase = CT_UPGRADE_PHASE_IDLE;
    CtDebugLog_Record(CT_LOG_MOD_UPGRADE,
                      CT_LOG_EVT_UPGRADE_ERROR,
                      err,
                      s_status.percent);
}

static void set_phase(uint8_t phase)
{
    s_ctx.phase = phase;
    s_ctx.phase_start_ms = CtBoard_GetTickMs();
    CtDebugLog_Record(CT_LOG_MOD_UPGRADE,
                      CT_LOG_EVT_UPGRADE_PHASE,
                      phase,
                      s_status.percent);
}

static void reset_context(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.phase = CT_UPGRADE_PHASE_IDLE;
}

void CtUpgrade_Init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = CT_UPGRADE_IDLE;
    reset_context();
    s_abort = 0u;
}

const CtUpgradeStatus *CtUpgrade_GetStatus(void)
{
    return &s_status;
}

void CtUpgrade_Abort(void)
{
    s_abort = 1u;
    if (s_status.state == CT_UPGRADE_RUNNING)
    {
        s_status.state = CT_UPGRADE_ABORTED;
        s_ctx.phase = CT_UPGRADE_PHASE_IDLE;
        CtDebugLog_Record(CT_LOG_MOD_UPGRADE,
                          CT_LOG_EVT_UPGRADE_ABORT,
                          s_status.percent,
                          s_status.last_error);
    }
}

int CtUpgrade_StartWithAppAddress(uint8_t node, uint8_t app_can_addr)
{
    const CtFirmwareInfo *info;

    if (s_status.state == CT_UPGRADE_RUNNING)
    {
        return 0;
    }

    info = CtFlash_GetInfo();
    if ((info->valid == 0u) || (info->size == 0u) ||
        ((info->app_addr != CT_BMS_APP_BASE_ADDR) && (info->app_addr != CT_SELF_APP_BASE)))
    {
        memset(&s_status, 0, sizeof(s_status));
        set_error(1u);
        return 0;
    }
    if ((info->app_addr == CT_SELF_APP_BASE) && ((app_can_addr & 0x0Fu) != CT_SELF_CAN_APP_ADDR))
    {
        memset(&s_status, 0, sizeof(s_status));
        set_error(1u);
        return 0;
    }

    memset(&s_status, 0, sizeof(s_status));
    reset_context();
    s_status.state = CT_UPGRADE_RUNNING;
    s_status.total = info->size;
    s_status.percent = 0u;
    s_ctx.node = node;
    if (s_ctx.node == 0u)
    {
        s_ctx.node = CT_NODE_ID_DEFAULT;
    }
    s_ctx.app_can_addr = app_can_addr & 0x0Fu;
    s_abort = 0u;
    CtDebugLog_Record(CT_LOG_MOD_UPGRADE,
                      CT_LOG_EVT_UPGRADE_START,
                      (uint16_t)(((uint16_t)s_ctx.node << 8) | s_ctx.app_can_addr),
                      (uint16_t)(s_status.total / 1024u));
    set_phase(CT_UPGRADE_PHASE_HELLO_FAST_SEND);
    return 1;
}

int CtUpgrade_Start(uint8_t node)
{
    return CtUpgrade_StartWithAppAddress(node, 0u);
}

static uint8_t handle_ack_wait(uint8_t cmd, uint32_t timeout_ms, uint8_t ok_phase, uint8_t timeout_error)
{
    uint8_t code;
    uint8_t match;

    match = CtCan_IapPollAck(s_ctx.node, cmd, &s_status.expect_seq, &code);
    if (match == CT_CAN_IAP_ACK_MATCH_OK)
    {
        set_phase(ok_phase);
        return 1u;
    }
    if (match == CT_CAN_IAP_ACK_MATCH_BAD)
    {
        set_error(code != 0u ? code : timeout_error);
        return 2u;
    }
    if (timeout_expired(s_ctx.phase_start_ms, timeout_ms))
    {
        set_error(timeout_error);
        return 2u;
    }
    return 0u;
}

static uint8_t send_hello_and_mark(void)
{
    if (!CtCan_IapSendHello(s_ctx.node))
    {
        return 0u;
    }
    s_ctx.last_tx_ms = CtBoard_GetTickMs();
    return 1u;
}

static void handle_fast_hello_wait(void)
{
    uint8_t code;
    uint8_t match;
    uint32_t now;

    match = CtCan_IapPollAck(s_ctx.node, CT_CAN_IAP_HELLO, &s_status.expect_seq, &code);
    if (match == CT_CAN_IAP_ACK_MATCH_OK)
    {
        set_phase(CT_UPGRADE_PHASE_START_SEND);
        return;
    }
    now = CtBoard_GetTickMs();
    if ((match == CT_CAN_IAP_ACK_MATCH_BAD) ||
        ((uint32_t)(now - s_ctx.phase_start_ms) >= CT_UPGRADE_FAST_HELLO_TIMEOUT_MS))
    {
        (void)code;
        set_phase(CT_UPGRADE_PHASE_ENTER_APP_IAP);
        return;
    }
    if ((uint32_t)(now - s_ctx.last_tx_ms) >= CT_UPGRADE_HELLO_RETRY_INTERVAL_MS)
    {
        (void)send_hello_and_mark();
    }
}

static void handle_iap_hello_wait(void)
{
    uint8_t code;
    uint8_t match;
    uint32_t now;

    match = CtCan_IapPollAck(s_ctx.node, CT_CAN_IAP_HELLO, &s_status.expect_seq, &code);
    if (match == CT_CAN_IAP_ACK_MATCH_OK)
    {
        set_phase(CT_UPGRADE_PHASE_START_SEND);
        return;
    }
    if (match == CT_CAN_IAP_ACK_MATCH_BAD)
    {
        set_error(code != 0u ? code : 2u);
        return;
    }

    now = CtBoard_GetTickMs();
    if ((uint32_t)(now - s_ctx.phase_start_ms) >= CT_UPGRADE_IAP_HELLO_TIMEOUT_MS)
    {
        set_error(2u);
        return;
    }
    if ((uint32_t)(now - s_ctx.last_tx_ms) >= CT_UPGRADE_HELLO_RETRY_INTERVAL_MS)
    {
        if (!send_hello_and_mark())
        {
            set_error(2u);
        }
    }
}

static void load_next_block(void)
{
    const CtFirmwareInfo *info;

    info = CtFlash_GetInfo();
    s_ctx.chunk_len = (uint16_t)((info->size - s_ctx.offset) > CT_IAP_BLOCK_BYTES ?
                                 CT_IAP_BLOCK_BYTES :
                                 (info->size - s_ctx.offset));
    memset(s_ctx.block, 0xFF, sizeof(s_ctx.block));
    if (!CtFlash_Read(s_ctx.offset, s_ctx.block, s_ctx.chunk_len))
    {
        set_error(4u);
        return;
    }

    s_ctx.frames_this_block = (uint8_t)((s_ctx.chunk_len + 7u) / 8u);
    s_ctx.frame_index = 0u;
    s_ctx.block_crc = CtCrc16_Calc(s_ctx.block, s_ctx.chunk_len);
    set_phase(CT_UPGRADE_PHASE_SEND_DATA);
}

static void send_data_frame(void)
{
    uint8_t frame[8];
    uint16_t discard_seq;
    uint8_t discard_code;

    /* Drain one unrelated RX frame between data frames so other bus traffic cannot fill FIFO0. */
    (void)CtCan_IapPollAck(s_ctx.node, 0xFFu, &discard_seq, &discard_code);
    memcpy(frame, &s_ctx.block[(uint16_t)s_ctx.frame_index * 8u], 8u);
    if (!CtCan_IapSendData(s_ctx.node, s_ctx.seq, frame))
    {
        set_error(5u);
        return;
    }

    s_ctx.seq++;
    s_ctx.frame_index++;
    if (s_ctx.frame_index >= s_ctx.frames_this_block)
    {
        set_phase(CT_UPGRADE_PHASE_SEND_COMMIT);
    }
}

static void finish_committed_block(void)
{
    s_ctx.offset += s_ctx.chunk_len;
    s_status.written = s_ctx.offset;
    if (s_status.total != 0u)
    {
        s_status.percent = (uint8_t)((s_status.written * 100u) / s_status.total);
    }
    s_ctx.block_seq++;

    if (s_status.written >= s_status.total)
    {
        set_phase(CT_UPGRADE_PHASE_SEND_END);
    }
    else
    {
        set_phase(CT_UPGRADE_PHASE_LOAD_BLOCK);
    }
}

void CtUpgrade_Task(void)
{
    const CtFirmwareInfo *info;
    uint16_t frame_count;

    if (s_status.state != CT_UPGRADE_RUNNING)
    {
        return;
    }
    if (s_abort != 0u)
    {
        s_status.state = CT_UPGRADE_ABORTED;
        s_ctx.phase = CT_UPGRADE_PHASE_IDLE;
        CtDebugLog_Record(CT_LOG_MOD_UPGRADE,
                          CT_LOG_EVT_UPGRADE_ABORT,
                          s_status.percent,
                          s_status.last_error);
        return;
    }

    info = CtFlash_GetInfo();
    switch (s_ctx.phase)
    {
    case CT_UPGRADE_PHASE_HELLO_FAST_SEND:
        if (!send_hello_and_mark())
        {
            set_phase(CT_UPGRADE_PHASE_ENTER_APP_IAP);
            break;
        }
        set_phase(CT_UPGRADE_PHASE_HELLO_FAST_WAIT);
        break;

    case CT_UPGRADE_PHASE_HELLO_FAST_WAIT:
        handle_fast_hello_wait();
        break;

    case CT_UPGRADE_PHASE_ENTER_APP_IAP:
        if (!CtCan_AppEnterIap(s_ctx.app_can_addr))
        {
            set_error(0x21u);
            break;
        }
        set_phase(CT_UPGRADE_PHASE_BOOT_DELAY);
        break;

    case CT_UPGRADE_PHASE_BOOT_DELAY:
        if (timeout_expired(s_ctx.phase_start_ms, CT_UPGRADE_BOOT_DELAY_MS))
        {
            set_phase(CT_UPGRADE_PHASE_HELLO_IAP_SEND);
        }
        break;

    case CT_UPGRADE_PHASE_HELLO_IAP_SEND:
        if (!send_hello_and_mark())
        {
            set_error(2u);
            break;
        }
        set_phase(CT_UPGRADE_PHASE_HELLO_IAP_WAIT);
        break;

    case CT_UPGRADE_PHASE_HELLO_IAP_WAIT:
        handle_iap_hello_wait();
        break;

    case CT_UPGRADE_PHASE_START_SEND:
        if (!CtCan_IapSendStart(s_ctx.node, info->size, info->crc16))
        {
            set_error(3u);
            break;
        }
        set_phase(CT_UPGRADE_PHASE_START_WAIT);
        break;

    case CT_UPGRADE_PHASE_START_WAIT:
        handle_ack_wait(CT_CAN_IAP_START, 2000u, CT_UPGRADE_PHASE_LOAD_BLOCK, 3u);
        break;

    case CT_UPGRADE_PHASE_LOAD_BLOCK:
        load_next_block();
        break;

    case CT_UPGRADE_PHASE_SEND_DATA:
        send_data_frame();
        break;

    case CT_UPGRADE_PHASE_SEND_COMMIT:
        if (!CtCan_IapSendCommit(s_ctx.node, s_ctx.block_seq, s_ctx.chunk_len, s_ctx.block_crc))
        {
            set_error(6u);
            break;
        }
        set_phase(CT_UPGRADE_PHASE_COMMIT_WAIT);
        break;

    case CT_UPGRADE_PHASE_COMMIT_WAIT:
        if (handle_ack_wait(CT_CAN_IAP_COMMIT, 2000u, CT_UPGRADE_PHASE_COMMIT_WAIT, 6u) == 1u)
        {
            finish_committed_block();
        }
        break;

    case CT_UPGRADE_PHASE_SEND_END:
        frame_count = s_ctx.seq;
        if (!CtCan_IapSendEnd(s_ctx.node, frame_count, info->crc16))
        {
            set_error(7u);
            break;
        }
        set_phase(CT_UPGRADE_PHASE_END_WAIT);
        break;

    case CT_UPGRADE_PHASE_END_WAIT:
        handle_ack_wait(CT_CAN_IAP_END, 5000u, CT_UPGRADE_PHASE_IDLE, 7u);
        if ((s_status.state == CT_UPGRADE_RUNNING) && (s_ctx.phase == CT_UPGRADE_PHASE_IDLE))
        {
            s_status.percent = 100u;
            s_status.state = CT_UPGRADE_DONE;
        }
        break;

    default:
        set_error(0x22u);
        break;
    }
}
