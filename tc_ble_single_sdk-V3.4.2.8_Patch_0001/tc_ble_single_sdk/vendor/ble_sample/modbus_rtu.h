#pragma once
#include "tl_common.h"
#include "conf.h"
//#include "build_version_auto.h"

u16 mb_crc16(const u8 *buf, u32 len);

// 处理一帧：
// - in: req/req_len
// - out: rsp/rsp_len（返回1表示要发送；0表示不回复）
int modbus_on_frame(const u8 *req, u32 req_len, u8 *rsp, u32 *rsp_len);

/*
 * DVC1124 transport-neutral configuration window.
 *
 * UART/Modbus and BLE SPP both enter the same modbus_on_frame(), therefore this
 * single map is the shared AFE configuration interface for serial and BLE.
 *
 * 0x2800..0x285F: semantic/user-facing DVC configuration and requested/effective
 *                  protection values.
 * 0x2900..0x2990: raw DVC register mirror (0x00..0x90).
 *                  Reads are direct. Writes are restricted to documented,
 *                  stable configuration fields and preserve reserved bits.
 */
#define DVC1124_COMM_SCHEMA_VERSION             0x0001u
#define DVC1124_COMM_REG_BASE                   0x2800u
#define DVC1124_COMM_REG_COUNT                  0x0060u

#define DVC1124_COMM_SCHEMA                     (DVC1124_COMM_REG_BASE + 0x00u)
#define DVC1124_COMM_MODEL                      (DVC1124_COMM_REG_BASE + 0x01u)
#define DVC1124_COMM_CHIP_VERSION               (DVC1124_COMM_REG_BASE + 0x02u)
#define DVC1124_COMM_WRITE_ADDR                 (DVC1124_COMM_REG_BASE + 0x03u)
#define DVC1124_COMM_CELL_COUNT                 (DVC1124_COMM_REG_BASE + 0x04u)
#define DVC1124_COMM_SHUNT_UOHM_LO              (DVC1124_COMM_REG_BASE + 0x05u)
#define DVC1124_COMM_SHUNT_UOHM_HI              (DVC1124_COMM_REG_BASE + 0x06u)

/* Operating configuration. */
#define DVC1124_COMM_HS_FET_MASK                (DVC1124_COMM_REG_BASE + 0x10u)
#define DVC1124_COMM_CADC_WORK_ENABLE           (DVC1124_COMM_REG_BASE + 0x11u)
#define DVC1124_COMM_CURRENT_WAKE_ENABLE        (DVC1124_COMM_REG_BASE + 0x12u)
#define DVC1124_COMM_CC1_WORK_TIME              (DVC1124_COMM_REG_BASE + 0x13u) /* enum code 0..3 */
#define DVC1124_COMM_CC1_SLEEP_WAKE_TIME        (DVC1124_COMM_REG_BASE + 0x14u) /* enum code 0..3 */
#define DVC1124_COMM_CHARGE_PUMP_VOLTAGE        (DVC1124_COMM_REG_BASE + 0x15u) /* volts: 0/6..12 */
#define DVC1124_COMM_CELL_MEAS_MASK             (DVC1124_COMM_REG_BASE + 0x16u)
#define DVC1124_COMM_CELL_SIGNED_MODE           (DVC1124_COMM_REG_BASE + 0x17u)
#define DVC1124_COMM_VADC_ENABLE                (DVC1124_COMM_REG_BASE + 0x18u)
#define DVC1124_COMM_VADC_SYNC                  (DVC1124_COMM_REG_BASE + 0x19u)
#define DVC1124_COMM_VADC_PERIOD_CYCLES         (DVC1124_COMM_REG_BASE + 0x1Au) /* 1/2/4/8 */
#define DVC1124_COMM_VADC_TIME_US               (DVC1124_COMM_REG_BASE + 0x1Bu) /* 790/1540/3030/6020 */

#define DVC1124_COMM_GP1_MODE                   (DVC1124_COMM_REG_BASE + 0x20u)
#define DVC1124_COMM_GP2_MODE                   (DVC1124_COMM_REG_BASE + 0x21u)
#define DVC1124_COMM_GP3_MODE                   (DVC1124_COMM_REG_BASE + 0x22u)
#define DVC1124_COMM_GP4_MODE                   (DVC1124_COMM_REG_BASE + 0x23u)
#define DVC1124_COMM_GP5_MODE                   (DVC1124_COMM_REG_BASE + 0x24u)
#define DVC1124_COMM_GP6_MODE                   (DVC1124_COMM_REG_BASE + 0x25u)
#define DVC1124_COMM_V3P3_SLEEP_ENABLE          (DVC1124_COMM_REG_BASE + 0x28u)
#define DVC1124_COMM_V3P3_WORK_ENABLE           (DVC1124_COMM_REG_BASE + 0x29u)
#define DVC1124_COMM_V3P3_TIMEOUT_RESTART       (DVC1124_COMM_REG_BASE + 0x2Au)
#define DVC1124_COMM_I2C_WDT_SECONDS            (DVC1124_COMM_REG_BASE + 0x2Bu) /* 0/4/8/16/32 */
#define DVC1124_COMM_TIMED_WAKE_SECONDS         (DVC1124_COMM_REG_BASE + 0x2Cu) /* 0,10..60,120..600 */
#define DVC1124_COMM_INTERRUPT_MASK             (DVC1124_COMM_REG_BASE + 0x2Du)
#define DVC1124_COMM_CURRENT_WAKE_UV            (DVC1124_COMM_REG_BASE + 0x30u)
#define DVC1124_COMM_BODY_DIODE_UV              (DVC1124_COMM_REG_BASE + 0x31u)
#define DVC1124_COMM_DSG_PULLDOWN               (DVC1124_COMM_REG_BASE + 0x32u) /* DPC 0..30 */
#define DVC1124_COMM_I2C_TIMEOUT_CLOSE_CHG      (DVC1124_COMM_REG_BASE + 0x33u)
#define DVC1124_COMM_I2C_TIMEOUT_CLOSE_DSG      (DVC1124_COMM_REG_BASE + 0x34u)
#define DVC1124_COMM_CORE_OT_X10C               (DVC1124_COMM_REG_BASE + 0x35u) /* 0=off */

/* Requested hardware protection values. Current x10 unit = 0.1A. */
#define DVC1124_COMM_REQ_COV_MV                 (DVC1124_COMM_REG_BASE + 0x40u)
#define DVC1124_COMM_REQ_COV_DELAY_MS           (DVC1124_COMM_REG_BASE + 0x41u)
#define DVC1124_COMM_REQ_CUV_MV                 (DVC1124_COMM_REG_BASE + 0x42u)
#define DVC1124_COMM_REQ_CUV_DELAY_MS           (DVC1124_COMM_REG_BASE + 0x43u)
#define DVC1124_COMM_REQ_OCD1_X10A              (DVC1124_COMM_REG_BASE + 0x44u)
#define DVC1124_COMM_REQ_OCD1_DELAY_MS          (DVC1124_COMM_REG_BASE + 0x45u)
#define DVC1124_COMM_REQ_OCC1_X10A              (DVC1124_COMM_REG_BASE + 0x46u)
#define DVC1124_COMM_REQ_OCC1_DELAY_MS          (DVC1124_COMM_REG_BASE + 0x47u)
#define DVC1124_COMM_REQ_OCD2_X10A              (DVC1124_COMM_REG_BASE + 0x48u)
#define DVC1124_COMM_REQ_OCD2_DELAY_MS          (DVC1124_COMM_REG_BASE + 0x49u)
#define DVC1124_COMM_REQ_OCC2_X10A              (DVC1124_COMM_REG_BASE + 0x4Au)
#define DVC1124_COMM_REQ_OCC2_DELAY_MS          (DVC1124_COMM_REG_BASE + 0x4Bu)
#define DVC1124_COMM_REQ_SCD_MV                 (DVC1124_COMM_REG_BASE + 0x4Cu)
#define DVC1124_COMM_REQ_SCD_DELAY_US           (DVC1124_COMM_REG_BASE + 0x4Du)

/* Effective values decoded from the live DVC registers; read-only. */
#define DVC1124_COMM_EFF_COV_MV                 (DVC1124_COMM_REG_BASE + 0x50u)
#define DVC1124_COMM_EFF_COV_DELAY_MS           (DVC1124_COMM_REG_BASE + 0x51u)
#define DVC1124_COMM_EFF_CUV_MV                 (DVC1124_COMM_REG_BASE + 0x52u)
#define DVC1124_COMM_EFF_CUV_DELAY_MS           (DVC1124_COMM_REG_BASE + 0x53u)
#define DVC1124_COMM_EFF_OCD1_X10A              (DVC1124_COMM_REG_BASE + 0x54u)
#define DVC1124_COMM_EFF_OCD1_DELAY_MS          (DVC1124_COMM_REG_BASE + 0x55u)
#define DVC1124_COMM_EFF_OCC1_X10A              (DVC1124_COMM_REG_BASE + 0x56u)
#define DVC1124_COMM_EFF_OCC1_DELAY_MS           (DVC1124_COMM_REG_BASE + 0x57u)
#define DVC1124_COMM_EFF_OCD2_X10A              (DVC1124_COMM_REG_BASE + 0x58u)
#define DVC1124_COMM_EFF_OCD2_DELAY_MS           (DVC1124_COMM_REG_BASE + 0x59u)
#define DVC1124_COMM_EFF_OCC2_X10A              (DVC1124_COMM_REG_BASE + 0x5Au)
#define DVC1124_COMM_EFF_OCC2_DELAY_MS           (DVC1124_COMM_REG_BASE + 0x5Bu)
#define DVC1124_COMM_EFF_SCD_MV                 (DVC1124_COMM_REG_BASE + 0x5Cu)
#define DVC1124_COMM_EFF_SCD_DELAY_US            (DVC1124_COMM_REG_BASE + 0x5Du)

#define DVC1124_RAW_REG_BASE                    0x2900u
#define DVC1124_RAW_REG_COUNT                   0x0091u /* DVC offsets 0x00..0x90 */


#define  BMS_SOFTWARE_VERSION_PREFIX    "a009-"
#define  BMS_SOFTWARE_VERSION_SUFFIX    "-c096v1p0"
// #define  BMS_SOFTWARE_VERDION_DEFAULT   BMS_SOFTWARE_VERSION_PREFIX BMS_SOFTWARE_BUILD_TIMESTAMP BMS_SOFTWARE_VERSION_SUFFIX  //32

#define PROD_SN_REG_BASE                   0xc002   // 0xE000 ~ 0xE00F
#define PROD_SN_REG_COUNT                  16

#define PROD_HW_VER_REG_BASE               (PROD_SN_REG_BASE + 16)   // 0xE010 ~ 0xE01F
#define PROD_HW_VER_REG_COUNT              16

#define PROD_SW_VER_REG_BASE               (PROD_HW_VER_REG_BASE + 16)   // 0xE020 ~ 0xE02F
#define PROD_SW_VER_REG_COUNT              16

// #define PROD_SN_LEN_REG                    0xE030
// #define PROD_HW_VER_LEN_REG                0xE031
// #define PROD_SW_VER_LEN_REG                0xE032

// #define PROD_SN_HEAD_ADDR_REG              0xE033
// #define PROD_HW_VER_HEAD_ADDR_REG          0xE034
// #define PROD_SW_VER_HEAD_ADDR_REG           0xE035

// #define PROD_SN_WRITE_FLAG_REG             0xE036
// #define PROD_HW_VER_WRITE_FLAG_REG         0xE037
// #define PROD_SW_VER_WRITE_FLAG_REG          0xE038

#define PRODUCT_ID_LENGTH_MAX 32

typedef struct {
	//因为历史兼容保留固定32字节
	u8 BMS_SerialNumber[PRODUCT_ID_LENGTH_MAX];
	u8 BMS_HardWareVersion[PRODUCT_ID_LENGTH_MAX];
	u8 BMS_SoftWareVersion[PRODUCT_ID_LENGTH_MAX];
}PRODUCTION_ID_INFO;

extern PRODUCTION_ID_INFO ProductionInfor;
