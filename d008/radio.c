#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "vendor/common/ble_flash.h"
#include "vendor/common/flash_prot.h"
#include "registers.h"
#include "link.h"

uint8_t device_address[6];
/* Classic 自动 OTA 在 MTU >= 71 时使用 Extend64。当前 SDK 对该客户端首包返回
 * SEQ_ERR。65 可承载普通参数组，DVC 整组使用现有 0x42 分片事务。 */
enum { D008_ATT_MTU=65 };
static uint16_t lock_command;
static bool ble_updating, protection_ready, retry_soc_after_disconnect;
static uint32_t retry_soc_ms;
static RtuSession session;
static uint8_t request[256], response[256];
static unsigned request_size,response_size,sent;
static u8 name[]=D008_BLE_NAME;
static u8 ccc[2],ota_ccc[2],dummy[20];
static const u16 primary=GATT_UUID_PRIMARY_SERVICE, characteristic=GATT_UUID_CHARACTER;
static const u16 gap=SERVICE_UUID_GENERIC_ACCESS, name_id=GATT_UUID_DEVICE_NAME, ccc_id=GATT_UUID_CLIENT_CHAR_CFG;
static const u8 spp[]={TELINK_SPP_UUID_SERVICE}, rx_id[]={TELINK_SPP_DATA_SERVER2CLIENT},
    tx_id[]={TELINK_SPP_DATA_CLIENT2SERVER}, ota_service[]={TELINK_OTA_UUID_SERVICE},
    ota_id[]={TELINK_SPP_DATA_OTA};
static const u8 name_decl[]={CHAR_PROP_READ,3,0,0,0x2a};
static const u8 rx_decl[]={CHAR_PROP_WRITE|CHAR_PROP_WRITE_WITHOUT_RSP,6,0,TELINK_SPP_DATA_SERVER2CLIENT};
static const u8 tx_decl[]={CHAR_PROP_READ|CHAR_PROP_NOTIFY,8,0,TELINK_SPP_DATA_CLIENT2SERVER};
static const u8 ota_decl[]={CHAR_PROP_WRITE_WITHOUT_RSP|CHAR_PROP_WRITE|CHAR_PROP_NOTIFY|CHAR_PROP_READ,12,0,TELINK_SPP_DATA_OTA};

bool update_active(void) {return ble_updating;}
static bool update_layout(void)
{
    if (!protection_ready || blc_flash_capacity!=FLASH_SIZE_512K ||
        ota_program_bootAddr!=0x20000 || (ota_program_offset && ota_program_offset!=0x20000) ||
        ota_firmware_max_size!=124*1024) return false;
    u32 mark=0;
    flash_read_page((ota_program_offset?0:0x20000)+8,4,(u8 *)&mark);
    return mark==0x544c4e4b;
}
static void update_lock(bool lock)
{
    if (!protection_ready) return;
    if (lock) flash_lock(lock_command);
    else flash_unlock();
}
static void stack_flash(u8 event,u32 start,u32 end)
{
    (void)start; (void)end;
    if (event==FLASH_OP_EVT_STACK_OTA_WRITE_NEW_FW_BEGIN || event==FLASH_OP_EVT_STACK_OTA_CLEAR_OLD_FW_BEGIN)
        update_lock(false);
    else if (event==FLASH_OP_EVT_STACK_OTA_WRITE_NEW_FW_END || event==FLASH_OP_EVT_STACK_OTA_CLEAR_OLD_FW_END)
        update_lock(true);
}

static int receive(void *packet)
{
    const rf_packet_att_write_t *p=packet;
    if (p->l2capLen>3 && p->l2capLen<=sizeof(request)+3 && !request_size && !response_size) {
        unsigned n=p->l2capLen-3;
        memcpy(request,&p->value,n); request_size=n;
    }
    return 0;
}
static int ota_receive(void *packet)
{
    const rf_packet_att_write_t *p=packet;
    if (!update_layout() || p->l2capLen<5) return 0;
    const u8 *v=(const u8 *)&p->value;
    unsigned cmd=v[0]|((unsigned)v[1]<<8);
    if (cmd==CMD_OTA_START || cmd==CMD_OTA_START_EXT) {
        unsigned expected=cmd==CMD_OTA_START?sizeof(ota_start_t):sizeof(ota_startExt_t);
        if ((unsigned)p->l2capLen!=expected+3 || !device.valid) return 0;
        wired_pause_frame();
        if (!ble_updating && !device_save_soc()) {
            /* 连接中禁止长擦除；断开后准备 SOC 槽，使下一次升级可以正常开始。 */
            retry_soc_after_disconnect=true; retry_soc_ms=0;
            return 0;
        }
        retry_soc_after_disconnect=false;
    }
    return otaWrite(packet);
}
#define A(n,perm,id,val) {n,perm,sizeof(id),sizeof(val),(u8 *)&id,(u8 *)&val}
static const attribute_t attrs[]={
    {13}, A(3,ATT_PERMISSIONS_READ,primary,gap), A(0,ATT_PERMISSIONS_READ,characteristic,name_decl),
    {0,ATT_PERMISSIONS_READ,2,sizeof(name)-1,(u8 *)&name_id,name},
    A(6,ATT_PERMISSIONS_READ,primary,spp), A(0,ATT_PERMISSIONS_READ,characteristic,rx_decl),
    {0,ATT_PERMISSIONS_WRITE,16,sizeof(dummy),(u8 *)rx_id,dummy,receive},
    A(0,ATT_PERMISSIONS_READ,characteristic,tx_decl), A(0,ATT_PERMISSIONS_READ,tx_id,dummy),
    A(0,ATT_PERMISSIONS_RDWR,ccc_id,ccc),
    A(4,ATT_PERMISSIONS_READ,primary,ota_service), A(0,ATT_PERMISSIONS_READ,characteristic,ota_decl),
    {0,ATT_PERMISSIONS_RDWR,16,sizeof(dummy),(u8 *)ota_id,dummy,ota_receive},
    A(0,ATT_PERMISSIONS_RDWR,ccc_id,ota_ccc)
};
static void ota_start(void)
{
    ble_updating=true;
    bls_pm_setManualLatency(0);
    bls_l2cap_requestConnParamUpdate(8,12,0,400);
}
static void ota_result(int result)
{
    if (result!=OTA_SUCCESS) {ble_updating=false; update_lock(true);}
}
static void disconnected(u8 event,u8 *data,int n)
{
    (void)event; (void)data; (void)n;
    memset(&session,0,sizeof(session)); request_size=response_size=sent=0;
    memset(ccc,0,2); memset(ota_ccc,0,2);
}
void product_radio_init(void)
{
    random_generator_init();
    blc_readFlashSize_autoConfigCustomFlashSector();
    blc_app_loadCustomizedParameters_normal();
    flash_read_page(flash_sector_mac_address,6,device_address);
    u8 blank[6]={255,255,255,255,255,255}, zero[6]={0};
    bool random=!memcmp(device_address,blank,6) || !memcmp(device_address,zero,6);
    if (random) {
        u8 uid[16]; unsigned int mid;
        if (flash_read_mid_uid_with_check(&mid,uid)) memcpy(device_address,uid,6);
        else generateRandomNum(6,device_address);
        device_address[5]|=0xc0;
    }
    flash_protection_init();
    lock_command=flash_change_app_lock_block_to_flash_lock_block(FLASH_LOCK_FW_LOW_256K);
    protection_ready=!blc_flashProt.init_err && lock_command && blc_flash_capacity==FLASH_SIZE_512K;
    update_lock(true);
    blc_appRegisterStackFlashOperationCallback(stack_flash);
    blc_ll_initBasicMCU();
    blc_ll_initStandby_module(device_address); blc_ll_initAdvertising_module(device_address);
    blc_ll_initConnection_module(); blc_ll_initSlaveRole_module();
    blc_gap_peripheral_init(); blc_l2cap_register_handler(blc_l2cap_packet_receive);
    bls_att_setAttributeTable((u8 *)attrs); blc_att_setRxMtuSize(D008_ATT_MTU);
    blc_smp_setSecurityLevel(No_Security);
    if (update_layout()) {
        blc_ota_initOtaServer_module(); blc_ota_setOtaProcessTimeout(600); blc_ota_setOtaDataPacketTimeout(15);
        blc_ota_registerOtaStartCmdCb(ota_start); blc_ota_registerOtaResultIndicationCb(ota_result);
    }
    bls_app_registerEventCallback(BLT_EV_FLAG_TERMINATE,disconnected);
    if (random) blc_ll_setRandomAddr(device_address);
    const u8 adv[]={2,DT_FLAGS,6,17,0x07,TELINK_SPP_UUID_SERVICE};
    u8 scan[31]={sizeof(name),DT_COMPLETE_LOCAL_NAME};
    memcpy(scan+2,name,sizeof(name)-1);
    bls_ll_setAdvData((u8 *)adv,sizeof(adv)); bls_ll_setScanRspData(scan,sizeof(name)+1);
    bls_ll_setAdvParam(ADV_INTERVAL_500MS,ADV_INTERVAL_500MS,ADV_TYPE_CONNECTABLE_UNDIRECTED,
        random?OWN_ADDRESS_RANDOM:OWN_ADDRESS_PUBLIC,0,0,BLT_ENABLE_ADV_ALL,ADV_FP_NONE);
    rf_set_power_level_index(RF_POWER_P0dBm); bls_ll_setAdvEnable(BLC_ADV_ENABLE);
}
void product_radio_poll(uint32_t ms)
{
    if (retry_soc_after_disconnect && !ble_updating &&
        blc_ll_getCurrentState()!=BLS_LINK_STATE_CONN && (!retry_soc_ms || ms-retry_soc_ms>=5000)) {
        retry_soc_ms=ms;
        if (device_save_soc()) retry_soc_after_disconnect=false;
    }
    if (request_size) {
        response_size=rtu_channel(&session,request,request_size,response,ms);
        sent=0; request_size=0;
    }
    if (response_size && !(ccc[0]&1)) response_size=0;
    if (response_size) {
        unsigned n=response_size-sent;
        if (n>20) n=20;
        if (blc_gatt_pushHandleValueNotify(BLS_CONN_HANDLE,8,response+sent,n)==BLE_SUCCESS)
            if ((sent+=n)==response_size) response_size=0;
    }
}
