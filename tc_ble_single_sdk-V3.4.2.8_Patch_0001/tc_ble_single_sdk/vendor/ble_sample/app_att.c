/********************************************************************************************************
 * @file    app_att.c
 * @brief   BMS BLE attribute table: GAP/GATT/device-info/battery/SPP/OTA.
 *******************************************************************************************************/
#include "tl_common.h"
#include "stack/ble/ble.h"

#include "app_att.h"
#include "btname_modbus.h"
#include "modbus_rtu.h"

#define TELINK_NOTIFY_PAYLOAD 20u
#define BLE_MODBUS_RSP_MAX    512u

/* Telink SPP UUIDs. */
static const u8 s_spp_service_uuid[16] = WRAPPING_BRACES(TELINK_SPP_UUID_SERVICE);
static const u8 s_spp_server_to_client_uuid[16] = WRAPPING_BRACES(TELINK_SPP_DATA_SERVER2CLIENT);
static const u8 s_spp_client_to_server_uuid[16] = WRAPPING_BRACES(TELINK_SPP_DATA_CLIENT2SERVER);

static u8 s_spp_ccc[2];
static u8 s_spp_write_value[1];
static u8 s_spp_notify_value[1];

static const u8 s_spp_write_desc[] = "Telink SPP: Module->Phone";
static const u8 s_spp_notify_desc[] = "Telink SPP: Phone->Module";

/*
 * The historical characteristic direction names are retained in the handle
 * enum for protocol compatibility. The active properties are:
 * - SERVER_TO_CLIENT handle: writable request path used by module_on_receive.
 * - CLIENT_TO_SERVER handle: notify response path.
 */
static const u8 s_spp_write_char[19] = {
    CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP | CHAR_PROP_WRITE,
    U16_LO(SPP_SERVER_TO_CLIENT_DP_H), U16_HI(SPP_SERVER_TO_CLIENT_DP_H),
    TELINK_SPP_DATA_SERVER2CLIENT,
};

static const u8 s_spp_notify_char[19] = {
    CHAR_PROP_READ | CHAR_PROP_NOTIFY,
    U16_LO(SPP_CLIENT_TO_SERVER_DP_H), U16_HI(SPP_CLIENT_TO_SERVER_DP_H),
    TELINK_SPP_DATA_CLIENT2SERVER,
};

/* Standard GATT UUIDs used by the active table. */
static const u16 s_client_cfg_uuid = GATT_UUID_CLIENT_CHAR_CFG;
static const u16 s_user_desc_uuid = GATT_UUID_CHAR_USER_DESC;
static const u16 s_service_change_uuid = GATT_UUID_SERVICE_CHANGE;
static const u16 s_primary_service_uuid = GATT_UUID_PRIMARY_SERVICE;
static const u16 s_character_uuid = GATT_UUID_CHARACTER;
static const u16 s_device_info_service_uuid = SERVICE_UUID_DEVICE_INFORMATION;
static const u16 s_pnp_uuid = CHARACTERISTIC_UUID_PNP_ID;
static const u16 s_device_name_uuid = GATT_UUID_DEVICE_NAME;
static const u16 s_gap_service_uuid = SERVICE_UUID_GENERIC_ACCESS;
static const u16 s_appearance_uuid = GATT_UUID_APPEARANCE;
static const u16 s_conn_param_uuid = GATT_UUID_PERI_CONN_PARAM;
static const u16 s_appearance = GAP_APPEARE_UNKNOWN;
static const u16 s_gatt_service_uuid = SERVICE_UUID_GENERIC_ATTRIBUTE;
static const u16 s_conn_params[4] = {20, 40, 0, 1000};

_attribute_data_retention_ static u16 s_service_change_value[2];
_attribute_data_retention_ static u8 s_service_change_ccc[2];

/* Updated at runtime by btname_modbus.c. */
u8 my_devName[BTNAME_TOTAL_MAX_LEN] = "BT_default";

static const u8 s_pnp_id[] = {0x02, 0x8a, 0x24, 0x66, 0x82, 0x01, 0x00};

/* Battery service is kept to preserve the deployed GATT handle layout. */
static const u16 s_battery_service_uuid = SERVICE_UUID_BATTERY;
static const u16 s_battery_char_uuid = CHARACTERISTIC_UUID_BATTERY_LEVEL;
_attribute_data_retention_ static u8 s_battery_ccc[2];
_attribute_data_retention_ static u8 s_battery_value[1] = {99};

#if (BLE_OTA_SERVER_ENABLE)
static const u8 s_ota_service_uuid[16] = WRAPPING_BRACES(TELINK_OTA_UUID_SERVICE);
static const u8 s_ota_uuid[16] = WRAPPING_BRACES(TELINK_SPP_DATA_OTA);
_attribute_data_retention_ static u8 s_ota_data;
_attribute_data_retention_ static u8 s_ota_ccc[2];
static const u8 s_ota_name[] = {'O', 'T', 'A'};
#endif

static const u8 s_device_name_char[5] = {
    CHAR_PROP_READ | CHAR_PROP_NOTIFY,
    U16_LO(GenericAccess_DeviceName_DP_H), U16_HI(GenericAccess_DeviceName_DP_H),
    U16_LO(GATT_UUID_DEVICE_NAME), U16_HI(GATT_UUID_DEVICE_NAME),
};

static const u8 s_appearance_char[5] = {
    CHAR_PROP_READ,
    U16_LO(GenericAccess_Appearance_DP_H), U16_HI(GenericAccess_Appearance_DP_H),
    U16_LO(GATT_UUID_APPEARANCE), U16_HI(GATT_UUID_APPEARANCE),
};

static const u8 s_conn_param_char[5] = {
    CHAR_PROP_READ,
    U16_LO(CONN_PARAM_DP_H), U16_HI(CONN_PARAM_DP_H),
    U16_LO(GATT_UUID_PERI_CONN_PARAM), U16_HI(GATT_UUID_PERI_CONN_PARAM),
};

static const u8 s_service_change_char[5] = {
    CHAR_PROP_INDICATE,
    U16_LO(GenericAttribute_ServiceChanged_DP_H), U16_HI(GenericAttribute_ServiceChanged_DP_H),
    U16_LO(GATT_UUID_SERVICE_CHANGE), U16_HI(GATT_UUID_SERVICE_CHANGE),
};

static const u8 s_pnp_char[5] = {
    CHAR_PROP_READ,
    U16_LO(DeviceInformation_pnpID_DP_H), U16_HI(DeviceInformation_pnpID_DP_H),
    U16_LO(CHARACTERISTIC_UUID_PNP_ID), U16_HI(CHARACTERISTIC_UUID_PNP_ID),
};

static const u8 s_battery_char[5] = {
    CHAR_PROP_READ | CHAR_PROP_NOTIFY,
    U16_LO(BATT_LEVEL_INPUT_DP_H), U16_HI(BATT_LEVEL_INPUT_DP_H),
    U16_LO(CHARACTERISTIC_UUID_BATTERY_LEVEL), U16_HI(CHARACTERISTIC_UUID_BATTERY_LEVEL),
};

#if (BLE_OTA_SERVER_ENABLE)
static const u8 s_ota_char[19] = {
    CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP | CHAR_PROP_NOTIFY | CHAR_PROP_WRITE,
    U16_LO(OTA_CMD_OUT_DP_H), U16_HI(OTA_CMD_OUT_DP_H),
    TELINK_SPP_DATA_OTA,
};
#endif

static u8 s_ble_rsp_buf[BLE_MODBUS_RSP_MAX];

static ble_sts_t notify_big_packet(u16 conn, u16 handle, const u8 *data, u16 len)
{
    u16 offset = 0u;

    while (offset < len)
    {
        u8 chunk = (u8)(((len - offset) > TELINK_NOTIFY_PAYLOAD)
                            ? TELINK_NOTIFY_PAYLOAD
                            : (len - offset));
        ble_sts_t status = blc_gatt_pushHandleValueNotify(
            conn, handle, (u8 *)(data + offset), chunk);

        if (status != BLE_SUCCESS)
        {
            return status;
        }
        offset = (u16)(offset + chunk);
    }

    return BLE_SUCCESS;
}

static int module_on_receive_data(void *para)
{
    rf_packet_att_write_t *packet = (rf_packet_att_write_t *)para;
    u8 len = (u8)(packet->l2capLen - 3u);
    const u8 *data = (const u8 *)&packet->value;
    u32 rsp_len = 0u;

    if ((len != 0u) &&
        modbus_on_frame(data, len, s_ble_rsp_buf, &rsp_len) &&
        (rsp_len != 0u))
    {
        (void)notify_big_packet(BLS_CONN_HANDLE,
                                SPP_CLIENT_TO_SERVER_DP_H,
                                s_ble_rsp_buf,
                                (u16)rsp_len);
    }

    return 0;
}

/*
 * Do not reorder active rows: deployed clients and OTA tooling depend on the
 * current handle numbering even though the old HID sample has been removed.
 */
static const attribute_t s_attributes[] = {
    {ATT_END_H - 1, 0, 0, 0, 0, 0, 0, 0},

    /* GAP: handles 0x0001..0x0007 */
    {7, ATT_PERMISSIONS_READ, 2, 2, (u8 *)&s_primary_service_uuid, (u8 *)&s_gap_service_uuid, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_device_name_char), (u8 *)&s_character_uuid, (u8 *)s_device_name_char, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(my_devName), (u8 *)&s_device_name_uuid, my_devName, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_appearance_char), (u8 *)&s_character_uuid, (u8 *)s_appearance_char, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_appearance), (u8 *)&s_appearance_uuid, (u8 *)&s_appearance, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_conn_param_char), (u8 *)&s_character_uuid, (u8 *)s_conn_param_char, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_conn_params), (u8 *)&s_conn_param_uuid, (u8 *)s_conn_params, 0, 0},

    /* GATT: handles 0x0008..0x000B */
    {4, ATT_PERMISSIONS_READ, 2, 2, (u8 *)&s_primary_service_uuid, (u8 *)&s_gatt_service_uuid, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_service_change_char), (u8 *)&s_character_uuid, (u8 *)s_service_change_char, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_service_change_value), (u8 *)&s_service_change_uuid, (u8 *)s_service_change_value, 0, 0},
    {0, ATT_PERMISSIONS_RDWR, 2, sizeof(s_service_change_ccc), (u8 *)&s_client_cfg_uuid, s_service_change_ccc, 0, 0},

    /* Device information: handles 0x000C..0x000E */
    {3, ATT_PERMISSIONS_READ, 2, 2, (u8 *)&s_primary_service_uuid, (u8 *)&s_device_info_service_uuid, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_pnp_char), (u8 *)&s_character_uuid, (u8 *)s_pnp_char, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_pnp_id), (u8 *)&s_pnp_uuid, (u8 *)s_pnp_id, 0, 0},

    /* Battery: handles 0x000F..0x0012 */
    {4, ATT_PERMISSIONS_READ, 2, 2, (u8 *)&s_primary_service_uuid, (u8 *)&s_battery_service_uuid, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_battery_char), (u8 *)&s_character_uuid, (u8 *)s_battery_char, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_battery_value), (u8 *)&s_battery_char_uuid, s_battery_value, 0, 0},
    {0, ATT_PERMISSIONS_RDWR, 2, sizeof(s_battery_ccc), (u8 *)&s_client_cfg_uuid, s_battery_ccc, 0, 0},

    /* SPP: handles 0x0013..0x001A */
    {8, ATT_PERMISSIONS_READ, 2, 16, (u8 *)&s_primary_service_uuid, (u8 *)s_spp_service_uuid, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_spp_write_char), (u8 *)&s_character_uuid, (u8 *)s_spp_write_char, 0},
    {0, ATT_PERMISSIONS_RDWR, 16, sizeof(s_spp_write_value), (u8 *)s_spp_server_to_client_uuid, s_spp_write_value, (att_readwrite_callback_t)&module_on_receive_data},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_spp_write_desc), (u8 *)&s_user_desc_uuid, (u8 *)s_spp_write_desc},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_spp_notify_char), (u8 *)&s_character_uuid, (u8 *)s_spp_notify_char, 0},
    {0, ATT_PERMISSIONS_RDWR, 16, sizeof(s_spp_notify_value), (u8 *)s_spp_client_to_server_uuid, s_spp_notify_value, 0},
    {0, ATT_PERMISSIONS_RDWR, 2, 2, (u8 *)&s_client_cfg_uuid, s_spp_ccc},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_spp_notify_desc), (u8 *)&s_user_desc_uuid, (u8 *)s_spp_notify_desc},

#if (BLE_OTA_SERVER_ENABLE)
    /* OTA: handles 0x001B..0x001F */
    {5, ATT_PERMISSIONS_READ, 2, 16, (u8 *)&s_primary_service_uuid, (u8 *)s_ota_service_uuid, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_ota_char), (u8 *)&s_character_uuid, (u8 *)s_ota_char, 0, 0},
    {0, ATT_PERMISSIONS_RDWR, 16, sizeof(s_ota_data), (u8 *)s_ota_uuid, &s_ota_data, &otaWrite, NULL},
    {0, ATT_PERMISSIONS_RDWR, 2, sizeof(s_ota_ccc), (u8 *)&s_client_cfg_uuid, s_ota_ccc, 0, 0},
    {0, ATT_PERMISSIONS_READ, 2, sizeof(s_ota_name), (u8 *)&s_user_desc_uuid, (u8 *)s_ota_name, 0, 0},
#endif
};

void my_att_init(void)
{
    bls_att_setAttributeTable((u8 *)s_attributes);
}
