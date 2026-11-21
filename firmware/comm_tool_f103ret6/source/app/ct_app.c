#include "ct_app.h"
#include "ct_board_port.h"
#include "ct_can_gateway.h"
#include "ct_config.h"
#include "ct_debug_log.h"
#include "ct_flash_store.h"
#include "ct_self_iap.h"
#include "ct_status.h"
#include "ct_upgrade_manager.h"
#include "ct_owc.h"
#include "board_owc.h"
#include <string.h>

static uint8_t s_tx[10u + CT_UART_MAX_PAYLOAD + 2u];
static uint32_t s_can_bitrate = CT_CAN_DEFAULT_BITRATE;
static uint32_t s_bms_uart_baud = CT_BMS_UART_BAUD;
static uint8_t s_node_id = CT_NODE_ID_DEFAULT;
static uint8_t s_app_can_addr = 0u;

#define CT_BMS_MAX_REG_WORDS 120u
#define CT_OFFLINE_BUTTON_DEBOUNCE_MS 60u

static uint8_t s_offline_button_last_raw;
static uint8_t s_offline_button_stable;
static uint8_t s_offline_button_latched;
static uint32_t s_offline_button_change_ms;

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void wr32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void respond(const CtFrame *req, uint8_t status, const uint8_t *payload, uint16_t length)
{
    uint16_t frame_len;

    frame_len = CtProtocol_Encode(req->seq,
                                  req->cmd,
                                  status,
                                  payload,
                                  length,
                                  CT_UART_FLAG_ACK,
                                  s_tx,
                                  (uint16_t)sizeof(s_tx));
    if (frame_len != 0u)
    {
        CtDebugLog_Record(CT_LOG_MOD_UART,
                          CT_LOG_EVT_CMD_TX,
                          (uint16_t)(((uint16_t)req->cmd << 8) | status),
                          length);
        CtBoard_UartWrite(s_tx, frame_len);
    }
}

static uint8_t command_allowed_during_upgrade(uint8_t cmd)
{
    switch (cmd)
    {
    case CT_CMD_GET_INFO:
    case CT_CMD_OWC_INFO:
    case CT_CMD_FW_INFO:
    case CT_CMD_UPGRADE_STATUS:
    case CT_CMD_UPGRADE_ABORT:
    case CT_CMD_CAN_DIAG:
    case CT_CMD_DEBUG_LOG:
        return 1u;
    default:
        return 0u;
    }
}

static void start_offline_upgrade_from_cache(void)
{
    const CtFirmwareInfo *info;
    const CtUpgradeStatus *status;

    status = CtUpgrade_GetStatus();
    if (status->state == CT_UPGRADE_STATE_RUNNING)
    {
        return;
    }

    info = CtFlash_GetInfo();
    if ((info->valid == 0u) ||
        (info->size == 0u) ||
        (info->app_addr != CT_BMS_APP_BASE_ADDR && info->app_addr != CT_C073_APP_BASE))
    {
        CtDebugLog_Record(CT_LOG_MOD_UPGRADE,
                          CT_LOG_EVT_UPGRADE_ERROR,
                          0x30u,
                          0u);
        return;
    }

    (void)CtUpgrade_StartWithAppAddress(s_node_id, s_app_can_addr);
}

static void poll_offline_upgrade_button(void)
{
    uint8_t raw;
    uint32_t now;

    now = CtBoard_GetTickMs();
    raw = CtBoard_OfflineUpgradeButtonActive() ? 1u : 0u;
    if (raw != s_offline_button_last_raw)
    {
        s_offline_button_last_raw = raw;
        s_offline_button_change_ms = now;
        return;
    }

    if ((uint32_t)(now - s_offline_button_change_ms) < CT_OFFLINE_BUTTON_DEBOUNCE_MS)
    {
        return;
    }

    if (raw != s_offline_button_stable)
    {
        s_offline_button_stable = raw;
        if (s_offline_button_stable == 0u)
        {
            s_offline_button_latched = 0u;
        }
    }

    if ((s_offline_button_stable != 0u) && (s_offline_button_latched == 0u))
    {
        s_offline_button_latched = 1u;
        start_offline_upgrade_from_cache();
    }
}

void CtApp_Init(void)
{
    CtFlash_Init();
    CtUpgrade_Init();
    CtSelfIap_Init();
    s_offline_button_last_raw = CtBoard_OfflineUpgradeButtonActive() ? 1u : 0u;
    s_offline_button_stable = s_offline_button_last_raw;
    s_offline_button_latched = s_offline_button_stable;
    s_offline_button_change_ms = CtBoard_GetTickMs();
    CtDebugLog_Record(CT_LOG_MOD_APP,
                      CT_LOG_EVT_BOOT,
                      (uint16_t)(((uint16_t)CT_FW_VERSION_MAJOR << 8) | CT_FW_VERSION_MINOR),
                      (uint16_t)(((uint16_t)CT_FW_VERSION_PATCH << 8) | CT_DEBUG_LOG_ENABLE));
}

void CtApp_Poll(void)
{
    const CtUpgradeStatus *status;

    poll_offline_upgrade_button();
    CtUpgrade_Task();
    status = CtUpgrade_GetStatus();
    if (status->state != 1u)
    {
        CtSelfIap_PollCan();
    }
    CtSelfIap_Task();
}

static void handle_info(const CtFrame *req)
{
    uint8_t payload[28];

    memset(payload, 0, sizeof(payload));
    payload[0] = CT_PROTOCOL_VERSION;
    payload[1] = CT_FW_VERSION_MAJOR;
    payload[2] = CT_FW_VERSION_MINOR;
    payload[3] = CT_FW_VERSION_PATCH;
    wr32(&payload[4], s_can_bitrate);
    wr32(&payload[8], CT_FW_CACHE_BASE);
    wr32(&payload[12], CT_FW_CACHE_SIZE);
    wr32(&payload[16], CtDebugLog_IsEnabled() ? 1u : 0u);
    payload[20] = s_node_id;
    payload[21] = s_app_can_addr;
    wr32(&payload[24], s_bms_uart_baud);
    respond(req, CT_STATUS_OK, payload, (uint16_t)sizeof(payload));
}

static void handle_set_bms_uart(const CtFrame *req)
{
    uint32_t baudrate;
    uint8_t payload[4];

    if (req->length < 4u)
    {
        respond(req, CT_STATUS_BAD_PARAM, 0, 0u);
        return;
    }

    baudrate = rd32(&req->payload[0]);
    if (!CtBoard_SetBmsUartBaud(baudrate))
    {
        respond(req, CT_STATUS_BAD_PARAM, 0, 0u);
        return;
    }

    s_bms_uart_baud = baudrate;
    wr32(payload, s_bms_uart_baud);
    respond(req, CT_STATUS_OK, payload, (uint16_t)sizeof(payload));
}

static void handle_set_can(const CtFrame *req)
{
    if (req->length < 8u)
    {
        respond(req, CT_STATUS_BAD_PARAM, 0, 0u);
        return;
    }
    s_can_bitrate = rd32(&req->payload[0]);
    s_node_id = req->payload[4];
    s_app_can_addr = req->payload[5] & 0x0Fu;
    if ((s_node_id == 0u) || (s_node_id > 0x7Fu))
    {
        respond(req, CT_STATUS_BAD_PARAM, 0, 0u);
        return;
    }
    if (!CtBoard_SetCanBitrate(s_can_bitrate))
    {
        respond(req, CT_STATUS_BAD_PARAM, 0, 0u);
        return;
    }
    CtDebugLog_Record(CT_LOG_MOD_CAN,
                      CT_LOG_EVT_CAN_SET,
                      (uint16_t)(s_can_bitrate / 1000u),
                      (uint16_t)(((uint16_t)s_node_id << 8) | s_app_can_addr));
    respond(req, CT_STATUS_OK, 0, 0u);
}

static void handle_fw_begin(const CtFrame *req)
{
    uint32_t app_addr;
    uint32_t size;
    uint16_t crc16;
    uint32_t crc32;

    if (req->length < 14u)
    {
        respond(req, CT_STATUS_BAD_PARAM, 0, 0u);
        return;
    }
    app_addr = rd32(&req->payload[0]);
    size = rd32(&req->payload[4]);
    crc16 = rd16(&req->payload[8]);
    crc32 = rd32(&req->payload[10]);
    if (!CtFlash_Begin(app_addr, size, crc16, crc32))
    {
        respond(req, CT_STATUS_FLASH_ERROR, 0, 0u);
        return;
    }
    CtDebugLog_Record(CT_LOG_MOD_FLASH,
                      CT_LOG_EVT_FW_BEGIN,
                      (uint16_t)(app_addr >> 16),
                      (uint16_t)(size & 0xFFFFu));
    respond(req, CT_STATUS_OK, 0, 0u);
}

static void handle_fw_data(const CtFrame *req)
{
    uint32_t offset;

    if (req->length < 5u)
    {
        respond(req, CT_STATUS_BAD_PARAM, 0, 0u);
        return;
    }
    offset = rd32(&req->payload[0]);
    if (!CtFlash_Write(offset, &req->payload[4], (uint16_t)(req->length - 4u)))
    {
        respond(req, CT_STATUS_FLASH_ERROR, 0, 0u);
        return;
    }
    respond(req, CT_STATUS_OK, 0, 0u);
}

static void handle_fw_end(const CtFrame *req)
{
    uint32_t size;
    uint16_t crc16;
    uint32_t crc32;

    if (req->length < 10u)
    {
        respond(req, CT_STATUS_BAD_PARAM, 0, 0u);
        return;
    }
    size = rd32(&req->payload[0]);
    crc16 = rd16(&req->payload[4]);
    crc32 = rd32(&req->payload[6]);
    if (!CtFlash_End(size, crc16, crc32))
    {
        respond(req, CT_STATUS_FLASH_ERROR, 0, 0u);
        return;
    }
    CtDebugLog_Record(CT_LOG_MOD_FLASH,
                      CT_LOG_EVT_FW_END,
                      (uint16_t)(size & 0xFFFFu),
                      crc16);
    respond(req, CT_STATUS_OK, 0, 0u);
}

static void handle_fw_info(const CtFrame *req)
{
    const CtFirmwareInfo *info;
    uint8_t payload[15];

    info = CtFlash_GetInfo();
    wr32(&payload[0], info->app_addr);
    wr32(&payload[4], info->size);
    wr16(&payload[8], info->crc16);
    wr32(&payload[10], info->crc32);
    payload[14] = (uint8_t)(info->valid != 0u);
    respond(req, CT_STATUS_OK, payload, (uint16_t)sizeof(payload));
}

static void handle_bms_read(const CtFrame *req)
{
    uint16_t words[CT_BMS_MAX_REG_WORDS];
    uint8_t payload[CT_BMS_MAX_REG_WORDS * 2u];
    uint16_t addr;
    uint16_t count;
    uint16_t i;

    if (req->length < 4u)
    {
        respond(req, CT_STATUS_BAD_PARAM, 0, 0u);
        return;
    }
    addr = rd16(&req->payload[0]);
    count = rd16(&req->payload[2]);
    if ((count == 0u) ||
        (count > CT_BMS_MAX_REG_WORDS) ||
        (((uint32_t)addr + (uint32_t)count - 1u) > 0xFFFFu))
    {
        respond(req, CT_STATUS_BAD_PARAM, 0, 0u);
        return;
    }
    if (!CtCan_AppReadRegs(s_app_can_addr, addr, count, words))
    {
        respond(req,
                (CtCan_GetLastGatewayError() == CT_CAN_GATEWAY_ERR_TIMEOUT) ?
                    CT_STATUS_CAN_TIMEOUT : CT_STATUS_BMS_ERROR,
                0,
                0u);
        return;
    }

    for (i = 0u; i < count; ++i)
    {
        wr16(&payload[i << 1], words[i]);
    }
    respond(req, CT_STATUS_OK, payload, (uint16_t)(count << 1));
}

static void handle_bms_write(const CtFrame *req)
{
    uint16_t words[CT_BMS_MAX_REG_WORDS];
    uint16_t addr;
    uint16_t count;
    uint16_t i;

    if (req->length < 4u)
    {
        respond(req, CT_STATUS_BAD_PARAM, 0, 0u);
        return;
    }
    addr = rd16(&req->payload[0]);
    count = rd16(&req->payload[2]);
    if ((count == 0u) ||
        (count > CT_BMS_MAX_REG_WORDS) ||
        (((uint32_t)addr + (uint32_t)count - 1u) > 0xFFFFu) ||
        (req->length != (uint16_t)(4u + (count << 1))))
    {
        respond(req, CT_STATUS_BAD_PARAM, 0, 0u);
        return;
    }

    for (i = 0u; i < count; ++i)
    {
        words[i] = rd16(&req->payload[4u + (i << 1)]);
    }

    if (!(req->cmd == CT_CMD_BMS_WRITE_GROUP ?
          CtCan_AppWriteGroup(s_app_can_addr, addr, count, words) :
          CtCan_AppWriteRegs(s_app_can_addr, addr, count, words)))
    {
        respond(req, CT_STATUS_BMS_ERROR, 0, 0u);
        return;
    }

    respond(req, CT_STATUS_OK, 0, 0u);
}

static void handle_bms_aging_ctrl(const CtFrame *req)
{
    uint8_t payload[2];
    uint8_t action;

    if (req->length < 1u)
    {
        respond(req, CT_STATUS_BAD_PARAM, 0, 0u);
        return;
    }

    action = req->payload[0];
    if (!CtCan_AppAgingControl(s_app_can_addr, action, &payload[0], &payload[1]))
    {
        respond(req, CT_STATUS_BMS_ERROR, 0, 0u);
        return;
    }

	respond(req, CT_STATUS_OK, payload, (uint16_t)sizeof(payload));
}

static void handle_bms_aging_set_hours(const CtFrame *req)
{
    uint8_t payload[4];
    uint16_t hours;

    if (req->length < 2u)
    {
        respond(req, CT_STATUS_BAD_PARAM, 0, 0u);
        return;
    }

    hours = rd16(req->payload);
    if ((hours == 0u) || (hours > 168u))
    {
        respond(req, CT_STATUS_BAD_PARAM, 0, 0u);
        return;
    }

    if (!CtCan_AppSetAgingHours(s_app_can_addr, hours, &payload[0], &payload[1]))
    {
        respond(req, CT_STATUS_BMS_ERROR, 0, 0u);
        return;
    }

    wr16(&payload[2], hours);
    respond(req, CT_STATUS_OK, payload, (uint16_t)sizeof(payload));
}

static void handle_debug_log(const CtFrame *req)
{
    uint8_t payload[CT_UART_MAX_PAYLOAD];
    uint8_t max_entries = 0u;
    uint8_t clear_after_read = 0u;
    uint16_t length;

    if (req->length >= 1u)
    {
        max_entries = req->payload[0];
    }
    if (req->length >= 2u)
    {
        clear_after_read = req->payload[1];
    }

    length = CtDebugLog_EncodeLatest(max_entries,
                                     clear_after_read,
                                     payload,
                                     (uint16_t)sizeof(payload));
    respond(req, CT_STATUS_OK, payload, length);
}

static void handle_bms_aging_status(const CtFrame *req)
{
    uint8_t payload[3];
    uint8_t state;
    uint16_t remaining_minutes;

    if (!CtCan_ReadFactoryAgingBroadcast(&state, &remaining_minutes, 6500u))
    {
        respond(req, CT_STATUS_CAN_TIMEOUT, 0, 0u);
        return;
    }

    payload[0] = state;
    wr16(&payload[1], remaining_minutes);
    respond(req, CT_STATUS_OK, payload, (uint16_t)sizeof(payload));
}

static void handle_enter_iap(const CtFrame *req)
{
    if (!CtCan_AppEnterIap(s_app_can_addr))
    {
        respond(req, CT_STATUS_CAN_TIMEOUT, 0, 0u);
        return;
    }
    respond(req, CT_STATUS_OK, 0, 0u);
}

static void handle_upgrade(const CtFrame *req)
{
    if (!CtUpgrade_StartWithAppAddress(s_node_id, s_app_can_addr))
    {
        respond(req, CT_STATUS_BMS_ERROR, 0, 0u);
        return;
    }
    respond(req, CT_STATUS_OK, 0, 0u);
}

static void handle_upgrade_status(const CtFrame *req)
{
    const CtUpgradeStatus *status;
    uint8_t payload[13];

    status = CtUpgrade_GetStatus();
    payload[0] = status->state;
    payload[1] = status->percent;
    payload[2] = status->last_error;
    wr32(&payload[3], status->written);
    wr32(&payload[7], status->total);
    wr16(&payload[11], status->expect_seq);
    respond(req, CT_STATUS_OK, payload, (uint16_t)sizeof(payload));
}

static void handle_can_diag(const CtFrame *req)
{
    CtCanDiag diag;
    uint8_t payload[64];

    if ((req->length >= 1u) && (req->payload[0] != 0u))
    {
        CtBoard_CanClearDiag();
    }

    memset(payload, 0, sizeof(payload));
    CtBoard_CanGetDiag(&diag);
    wr32(&payload[0], diag.tx_count);
    wr32(&payload[4], diag.tx_ok);
    wr32(&payload[8], diag.tx_fail);
    wr32(&payload[12], diag.tx_timeout);
    wr32(&payload[16], diag.rx_count);
    wr32(&payload[20], diag.rx_drop);
    wr32(&payload[24], diag.last_esr);
    wr32(&payload[28], diag.last_tsr);
    wr32(&payload[32], diag.last_msr);
    wr32(&payload[36], diag.last_rf0r);
    wr32(&payload[40], diag.last_tx_id);
    wr32(&payload[44], diag.last_rx_id);
    payload[48] = diag.last_tx_ide;
    payload[49] = diag.last_tx_dlc;
    payload[50] = diag.last_tx_status;
    payload[51] = diag.last_rx_ide;
    payload[52] = diag.last_rx_dlc;
    memcpy(&payload[53], diag.last_rx_data, 8u);
    payload[61] = s_node_id;
    payload[62] = s_app_can_addr;
    respond(req, CT_STATUS_OK, payload, (uint16_t)sizeof(payload));
}

void CtApp_HandleFrame(const CtFrame *frame)
{
    if (frame == 0)
    {
        return;
    }

    CtDebugLog_Record(CT_LOG_MOD_UART,
                      CT_LOG_EVT_CMD_RX,
                      frame->cmd,
                      frame->length);

    if ((CtUpgrade_GetStatus()->state == CT_UPGRADE_STATE_RUNNING) &&
        (command_allowed_during_upgrade(frame->cmd) == 0u))
    {
        respond(frame, CT_STATUS_BAD_STATE, 0, 0u);
        return;
    }

    switch (frame->cmd)
    {
    case CT_CMD_GET_INFO:
        handle_info(frame);
        break;
    case CT_CMD_SET_CAN:
        handle_set_can(frame);
        break;
    case CT_CMD_FEIDAO_SNAPSHOT: {
        uint8_t snapshot[72];
        if (frame->length != 0u) { respond(frame, CT_STATUS_BAD_PARAM, 0, 0u); break; }
        CtBoard_FeidaoSnapshot(snapshot);
        respond(frame, CT_STATUS_OK, snapshot, sizeof(snapshot));
        break;
    }
    case CT_CMD_FW_BEGIN:
        handle_fw_begin(frame);
        break;
    case CT_CMD_FW_DATA:
        handle_fw_data(frame);
        break;
    case CT_CMD_FW_END:
        handle_fw_end(frame);
        break;
    case CT_CMD_FW_INFO:
        handle_fw_info(frame);
        break;
    case CT_CMD_BMS_READ:
        handle_bms_read(frame);
        break;
    case CT_CMD_BMS_WRITE_GROUP:
    case CT_CMD_BMS_WRITE:
        handle_bms_write(frame);
        break;
    case CT_CMD_BMS_AGING_CTRL:
        handle_bms_aging_ctrl(frame);
        break;
    case CT_CMD_BMS_AGING_STATUS:
        handle_bms_aging_status(frame);
        break;
    case CT_CMD_BMS_AGING_SET_HOURS:
        handle_bms_aging_set_hours(frame);
        break;
    case CT_CMD_SET_BMS_UART:
        handle_set_bms_uart(frame);
        break;
    case CT_CMD_ENTER_IAP:
        handle_enter_iap(frame);
        break;
    case CT_CMD_UPGRADE:
        handle_upgrade(frame);
        break;
    case CT_CMD_UPGRADE_STATUS:
        handle_upgrade_status(frame);
        break;
    case CT_CMD_UPGRADE_ABORT:
        CtUpgrade_Abort();
        respond(frame, CT_STATUS_OK, 0, 0u);
        break;
    case CT_CMD_CAN_DIAG:
        handle_can_diag(frame);
        break;
    case CT_CMD_DEBUG_LOG:
        handle_debug_log(frame);
        break;
    case CT_CMD_OWC_CONTROL:
        if ((frame->length != 1u) || (frame->payload[0] > 1u)) {
            respond(frame, CT_STATUS_BAD_PARAM, 0, 0u);
        } else {
            respond(frame, BoardOwc_Enable(frame->payload[0]) ? CT_STATUS_OK :
                    CT_STATUS_UNSUPPORTED, 0, 0u);
        }
        break;
    case CT_CMD_OWC_SNAPSHOT:
        {
            uint8_t payload[CT_OWC_HEADER_BYTES + CT_OWC_MAX_BYTES];
            uint16_t length;
            if (frame->length != 1u) {
                respond(frame, CT_STATUS_BAD_PARAM, 0, 0u);
                break;
            }
            BoardOwc_Poll();
            length = CtOwc_EncodeSnapshot(frame->payload[0], BoardOwc_IsEnabled(),
                                         CtBoard_GetTickMs(), payload, sizeof(payload));
            if (length) BoardOwc_KeepAlive();
            respond(frame, length ? CT_STATUS_OK : CT_STATUS_BAD_PARAM, payload, length);
        }
        break;
    case CT_CMD_OWC_INFO:
        {
            uint8_t info[8] = {1u, 7u, 3u, 0u, CT_OWC_V3_HEADER_BYTES, 0u,
                              (uint8_t)CT_OWC_MAX_BYTES, (uint8_t)(CT_OWC_MAX_BYTES >> 8)};
#if (CT_COMM_UART_PORT == CT_COMM_UART_PORT_USART3)
            info[1] = 0u;
#endif
            respond(frame, frame->length == 0u ? CT_STATUS_OK : CT_STATUS_BAD_PARAM,
                    info, frame->length == 0u ? sizeof(info) : 0u);
        }
        break;
    case CT_CMD_OWC_CONFIG:
        if (frame->length != 2u || frame->payload[0] > 1u || frame->payload[1] > CT_OWC_MODE_LISHEN)
            respond(frame, CT_STATUS_BAD_PARAM, 0, 0u);
        else
            respond(frame, BoardOwc_EnableMode(frame->payload[0], frame->payload[1]) ?
                    CT_STATUS_OK : CT_STATUS_UNSUPPORTED, 0, 0u);
        break;
    case CT_CMD_OWC_FRAME:
        {
            uint8_t payload[CT_OWC_V3_HEADER_BYTES + CT_OWC_MAX_BYTES];
            uint16_t length;
            if (frame->length != 1u) {
                respond(frame, CT_STATUS_BAD_PARAM, 0, 0u);
                break;
            }
            BoardOwc_Poll();
            length = CtOwc_EncodeSnapshotV3(frame->payload[0], BoardOwc_IsEnabled(),
                        CtBoard_GetTickMs(), payload, sizeof(payload));
            if (length) BoardOwc_KeepAlive();
            respond(frame, length ? CT_STATUS_OK : CT_STATUS_BAD_PARAM, payload, length);
        }
        break;
    default:
        respond(frame, CT_STATUS_UNSUPPORTED, 0, 0u);
        break;
    }
}
