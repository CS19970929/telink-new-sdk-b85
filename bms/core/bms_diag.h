/*
 * 文件功能：启动/存储/采样/SOC/MOS 运行诊断快照与 Trace；主循环更新，
 * 只读窗口供上位机核对状态。
 * bms/core/bms_diag.h；实际编译归属见各产品 sources.txt。
 */
#ifndef BMS_DIAG_H_
#define BMS_DIAG_H_
#include <stdint.h>

/* schema 1，仅主循环使用；无 ISR 生产者，读取路径无 I/O。 */
#ifndef BMS_DIAG_BUILD_ID
#define BMS_DIAG_BUILD_ID 0u /* 0 表示不可用，由 bms.py 提供提交前缀。 */
#endif
#ifndef BMS_DIAG_BUILD_DIRTY
#define BMS_DIAG_BUILD_DIRTY 0
#endif
#if (BMS_DIAG_BUILD_DIRTY != 0) && (BMS_DIAG_BUILD_DIRTY != 1)
#error "BMS_DIAG_BUILD_DIRTY must be 0 or 1"
#endif
#define BMS_DIAG_BASE 0x2A00u
#define BMS_DIAG_TRACE_BASE 0x2B00u
#define BMS_DIAG_END 0x2E00u
#define BMS_DIAG_TRACE_COUNT 64u
#define BMS_DIAG_TRACE_WORDS 12u

/*
 * 开发默认保留轨迹；生产关闭以节省 1536 字节 RAM，
 * 能力位和既有只读窗口随之报告无轨迹。
 */
#ifndef BMS_DIAG_TRACE_ENABLE
#if defined(BMS_PRODUCTION_BUILD) && BMS_PRODUCTION_BUILD
#define BMS_DIAG_TRACE_ENABLE 0
#else
#define BMS_DIAG_TRACE_ENABLE 1
#endif
#endif
#if (BMS_DIAG_TRACE_ENABLE != 0) && (BMS_DIAG_TRACE_ENABLE != 1)
#error "BMS_DIAG_TRACE_ENABLE must be 0 or 1"
#endif

#if defined(BMS_PRODUCTION_BUILD) && BMS_PRODUCTION_BUILD && BMS_DIAG_TRACE_ENABLE
#error "Production firmware must disable BMS_DIAG_TRACE_ENABLE"
#endif

#define BMS_DIAG_CAP_BOOT       0x0001u
#define BMS_DIAG_CAP_TRACE      0x0002u
#define BMS_DIAG_CAP_STORAGE    0x0004u
#define BMS_DIAG_CAP_MOS        0x0008u
#define BMS_DIAG_CAP_UPGRADE    0x0010u
#define BMS_DIAG_CAP_RUNTIME    0x0020u
#define BMS_DIAG_CAPABILITIES   (BMS_DIAG_CAP_BOOT | (BMS_DIAG_TRACE_ENABLE ? BMS_DIAG_CAP_TRACE : 0u) | \
                                 BMS_DIAG_CAP_STORAGE | BMS_DIAG_CAP_MOS | \
                                 BMS_DIAG_CAP_UPGRADE | BMS_DIAG_CAP_RUNTIME)

#define BMS_DIAG_RUNTIME_VERSION 3u
#define BMS_DIAG_RUNTIME_OFFSET  192u
typedef struct {
    uint8_t chemistry;
    uint8_t profile_id;
    uint16_t profile_version;
    uint8_t soc_estimate;
    uint8_t soc_display;
    uint16_t current_deadband_ma;
    uint8_t ocv_state;
    uint8_t ocv_center;
    uint8_t ocv_low;
    uint8_t ocv_high;
    uint8_t ocv_confidence;
    uint8_t capacity_learned;
    uint8_t learning_state;
    uint16_t ocv_cell_mv;
    uint16_t rest_seconds;
    uint16_t learned_capacity_0p1ah;
    uint16_t nominal_capacity_0p1ah;
    uint16_t effective_capacity_0p1ah;
    uint16_t remaining_capacity_0p1ah;
    uint8_t endpoint_state;
    uint8_t endpoint_event_flags;
    int32_t filtered_current_ma;
    uint16_t current_variation_ma;
    uint16_t time_to_empty_min;
    uint16_t time_to_full_min;
    uint8_t eta_state;
    uint8_t eta_direction;
    uint8_t eta_confidence;
    uint8_t eta_valid;
    uint8_t soh;
    uint8_t soh_source;
    uint8_t soh_confidence;
    uint8_t capacity_learning_enable;
    uint8_t capacity_learning_candidate_valid;
    uint8_t capacity_learning_confidence;
    uint16_t candidate_capacity_0p1ah;
    uint16_t valid_learning_count;
    uint16_t rejected_learning_count;
    uint8_t last_learning_reject_reason;
    uint8_t last_sample_state;
    uint8_t last_integral_direction;
    uint8_t last_soc_action;
    uint8_t last_soc_before;
    uint8_t last_soc_after;
    uint8_t last_soc_target;
    uint8_t last_decision_detail;
    uint32_t last_sample_elapsed_32k;
    uint32_t last_integral_delta_as10;
} bms_soc_diag_t;

enum {
    BMS_SOC_SAMPLE_NONE=0, BMS_SOC_SAMPLE_INVALID=1,
    BMS_SOC_SAMPLE_FIRST=2, BMS_SOC_SAMPLE_DUPLICATE=3,
    BMS_SOC_SAMPLE_GAP=4, BMS_SOC_SAMPLE_ACCEPTED=5,
    BMS_SOC_SAMPLE_DIRECTION_CHANGE=6
};
enum {
    BMS_SOC_ACTION_NONE=0, BMS_SOC_ACTION_INTEGRATE=1,
    BMS_SOC_ACTION_OCV_DOWN=2, BMS_SOC_ACTION_TERMINAL_DOWN=3,
    BMS_SOC_ACTION_FULL_ANCHOR=4, BMS_SOC_ACTION_FORCED_EMPTY=5,
    BMS_SOC_ACTION_IDLE_EMPTY=6, BMS_SOC_ACTION_PARAMETER_SET=7,
    BMS_SOC_ACTION_STATE_RESTORE=8
};
enum { DIAG_NOT_RUN=0, DIAG_OK=1, DIAG_PORT=2, DIAG_LAYOUT=3,
    DIAG_REGION=4, DIAG_OPEN=5, DIAG_DEFAULTS=6, DIAG_SAVE=7,
    DIAG_PROGRAM_VERIFY=8, DIAG_ERASE_VERIFY=9, DIAG_OTA=10,
    DIAG_LOCK=11, DIAG_BACKOFF=12, DIAG_INVALID=13, DIAG_STARTED=14 };
enum { DIAG_EV_BOOT=1, DIAG_EV_INIT=2, DIAG_EV_STORAGE=3,
    DIAG_EV_PARAMS=4, DIAG_EV_MOS=5, DIAG_EV_AFE=6, DIAG_EV_BOOT_DONE=7,
    DIAG_EV_DRIVER=8, DIAG_EV_UPGRADE=9, DIAG_EV_CURRENT_RECOVERY=10,
    DIAG_EV_PM_STATE=11, DIAG_EV_PROTECTION=12, DIAG_EV_SAMPLE_STATE=13 };
/* 能力位 4 表示冻结的启动更新编号结果，与打开存储的结果独立。 */
enum { DIAG_UPGRADE_STARTED=1, DIAG_UPGRADE_CONFIG_LOAD=2,
    DIAG_UPGRADE_VALIDATION=3, DIAG_UPGRADE_SAVE=4, DIAG_UPGRADE_CONFIG_OK=5,
    DIAG_UPGRADE_STATE=6, DIAG_UPGRADE_EVENT=7, DIAG_UPGRADE_OK=8 };
enum { DIAG_UPGRADE_BAD_SW=1u, DIAG_UPGRADE_BAD_AFE=2u,
    DIAG_UPGRADE_BAD_SOC=4u, DIAG_UPGRADE_BAD_CAPACITY=8u };
/* 记录持久参数升级与迁移诊断。 */
void bms_diag_upgrade(uint16_t stage, uint16_t invalid_mask);

enum { DIAG_BLOCK_PARAMS=1u, DIAG_BLOCK_UPGRADE=2u, DIAG_BLOCK_OUTPUT=4u,
    DIAG_BLOCK_COMM=8u, DIAG_BLOCK_SW=16u, DIAG_BLOCK_HW=32u,
    DIAG_BLOCK_OPENWIRE=64u, DIAG_BLOCK_HEATER=128u,
    DIAG_BLOCK_SHUTDOWN=256u, DIAG_BLOCK_TEMP=512u, DIAG_BLOCK_BACKEND=1024u };

enum {
    DIAG_PM_BLOCK_SAMPLE_INVALID = 1u,
    DIAG_PM_BLOCK_OTA = 2u,
    DIAG_PM_BLOCK_FLASH = 4u,
    DIAG_PM_BLOCK_BUS = 8u,
    DIAG_PM_BLOCK_CURRENT = 16u,
    DIAG_PM_BLOCK_SAMPLE_PENDING = 32u,
    DIAG_PM_BLOCK_POWER_OFF = 64u,
    DIAG_PM_BLOCK_ACC_SLEEP = 128u
};
/* 取得存储诊断使用的系统时间戳。 */
uint32_t bms_diag_tick(void);
/* 初始化诊断窗口和启动快照。 */
void bms_diag_init(void);
/* 冻结启动快照，防止运行阶段覆盖启动证据。 */
void bms_diag_freeze_boot(void);
/* 追加一条带时间与参数的诊断轨迹。 */
void bms_diag_trace(uint16_t event, uint32_t arg0, uint32_t arg1);
/* 更新尚未冻结的启动诊断 16 位字段。 */
void bms_diag_boot_word(uint16_t offset, uint16_t value);
/* 更新尚未冻结的启动诊断 32 位字段。 */
void bms_diag_boot_u32(uint16_t offset, uint32_t value);
/* 记录存储或配置事务开始尝试。 */
void bms_diag_attempt(uint8_t domain);
/* 记录事务最终结果及诊断计数。 */
void bms_diag_result(uint8_t domain, uint16_t result);
/* 记录存储错误和对应操作信息。 */
void bms_diag_storage_error(uint16_t reason, uint32_t address);
/* 更新当前参数来源和提交状态诊断。 */
void bms_diag_params(uint8_t valid, uint8_t upgrade);
/* 记录软件 MOS 请求与充放电阻断原因，不作为物理 Gate 证据。 */
void bms_diag_mos(uint16_t requested, uint32_t charge, uint32_t discharge);
/* 记录外部控制命令及执行结果。 */
void bms_diag_command(uint8_t command, uint8_t valid);
/* 发布驱动 FET 缓存标志与有效性，不作为物理 Gate 证据。 */
void bms_diag_driver(uint8_t flags, uint8_t valid);
/* 刷新业务参数与启动门禁诊断快照。 */
void bms_param_diag_poll(void);
/* 从选定后端刷新诊断，遵守总线静默门禁。 */
void bms_afe_diag_poll(void);
/* 更新所选 AFE 后端的诊断字段。 */
void bms_diag_backend(uint16_t charge, uint16_t discharge);
/* 取得单个缓存诊断字，不访问硬件。 */
uint16_t bms_diag_cached_word(uint16_t offset);
/* 更新指定诊断计数项。 */
void bms_diag_counter(uint16_t index, uint32_t value);
/* 发布运行采样资格、测量与错误快照。 */
void bms_diag_runtime_sample(uint8_t valid, int32_t raw_current_ma,
                             int32_t current_ma, uint32_t sample_tick_32k,
                             uint8_t current_recovery_pending);
/* 发布 SOC 基础运行诊断。 */
void bms_diag_runtime_soc(uint8_t soc_estimate, uint8_t soc_display,
                          uint8_t ocv_state, uint8_t ocv_center,
                          uint8_t ocv_low, uint8_t ocv_high,
                          uint8_t ocv_confidence, uint16_t rest_seconds,
                          uint8_t learning_state, uint8_t capacity_learned,
                          uint16_t learned_capacity_0p1ah,
                          uint16_t current_deadband_ma);
/* 发布 SOC 端点、OCV 与学习扩展诊断。 */
void bms_diag_runtime_soc_extended(const bms_soc_diag_t *soc);
/* 发布低功耗状态与阻断原因，变化时记录轨迹。 */
void bms_diag_runtime_pm(uint8_t suspend_allowed, uint32_t block_mask,
                         uint8_t low_voltage_region, uint32_t low_voltage_seconds,
                         uint8_t ble_connected, uint8_t sample_pending,
                         uint16_t suspend_current_threshold_ma);
/* 发布各级保护故障位快照。 */
void bms_diag_runtime_faults(uint16_t level1, uint16_t level2, uint16_t level3);
/* 发布运行模式与累计时间诊断。 */
void bms_diag_runtime_mode(uint8_t factory_mode);
/* 判断请求寄存器范围是否与诊断窗口重叠。 */
int bms_diag_overlaps(uint16_t start, uint16_t count);
/* 从 RAM 诊断快照读取指定寄存器范围。 */
int bms_diag_read(uint16_t start, uint16_t count, uint8_t *bytes);

enum {
    DIAG_GUARD_OUTPUT_ENABLED=1u, DIAG_GUARD_COMM_INHIBIT=2u,
    DIAG_GUARD_BUS_SILENCED=4u, DIAG_GUARD_FAULT_LATCHED=8u,
    DIAG_GUARD_SAMPLES_QUALIFIED=16u
};
/* 从应用与后端缓存汇总运行诊断，不额外采样 AFE。 */
void bms_diag_poll_runtime(uint8_t, int32_t, uint32_t, uint8_t);
/* 发布后端配置、控制与恢复的详细诊断。 */
void bms_diag_backend_details(const uint16_t *words);
enum {
    DIAG_SH_OUTPUT_ENABLED=1u, DIAG_SH_SNAPSHOT_VALID=2u,
    DIAG_SH_OUTPUT_INHIBIT=4u, DIAG_SH_E2P_ERROR=8u,
    DIAG_SH_CHARGE_HW_BLOCK=16u, DIAG_SH_DISCHARGE_HW_BLOCK=32u,
    DIAG_SH_SHORT_LATCHED=64u, DIAG_SH_RECONFIGURE=128u,
    DIAG_SH_TEMP_BREAK=256u, DIAG_SH_AFE_ERROR=512u,
    DIAG_SH_SPI_ERROR=1024u
};
enum {
    DIAG_SH_TS1_VALID=1u, DIAG_SH_TS2_VALID=2u,
    DIAG_SH_MOS_NTC_SUPPORTED=4u, DIAG_SH_MOS_NTC_VALID=8u
};
/* 记录产品编译功能标志。 */
void bms_diag_set_build_flags(uint16_t);
/* 记录启动门禁最终结果。 */
void bms_diag_set_boot_result(uint16_t, uint16_t);

#endif
