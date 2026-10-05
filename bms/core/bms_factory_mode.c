/*
 * 文件功能：工厂老化模式与运行分钟累计；使用 32K 差值计时并持久化，
 * deep sleep 时间不计入运行时长。
 * bms/core/bms_factory_mode.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_factory_mode.h"
#include "bms_error.h"
#include "drivers.h"
#include "bms_state_store.h"

#define RUNTIME_SAVE_INTERVAL_MIN  1u
#define RUNTIME_PM_TICKS_PER_SEC   32000u
#define RUNTIME_PM_TICKS_PER_MIN   (RUNTIME_PM_TICKS_PER_SEC * 60u)

static u32 g_runtime_min = 0u;
static u32 g_runtime_last_saved_min = 0u;
static u8 g_runtime_store_ready = 0u;
static bms_mode_t g_mode = MODE_NORMAL;
static u32 g_runtime_last_tick_32k = 0u;
/* 不足一分钟的 32K tick 余量；只有工厂运行计时路径消费，deep sleep 不补算。 */
static u32 g_runtime_pending_tick_32k = 0u;
static u8 g_runtime_tick_ready = 0u;

/* 建立工厂模式与运行计时的初始状态。 */
static void runtime_set_initial_state(void)
{
    g_runtime_min = 0u;
    g_runtime_last_saved_min = 0u;
    g_runtime_store_ready = 0u;
    g_mode = MODE_FACTORY;
    g_runtime_last_tick_32k = 0u;
    g_runtime_pending_tick_32k = 0u;
    g_runtime_tick_ready = 0u;
}

/* 记录运行计时持久化失败。 */
static void runtime_note_store_error(void) { bms_error_raise(BMS_ERROR_EEPROM_STORE); }

/* 保存累计运行分钟数，成功后更新上次保存基准。 */
static int runtime_state_save(void)
{
    if (!g_runtime_store_ready || !bms_state_store_write_runtime_min(g_runtime_min)) return 0;
    g_runtime_last_saved_min = g_runtime_min;
    return 1;
}

/* 老化时间完成后退出工厂模式并保存状态。 */
static void runtime_finish_factory_mode(void)
{
    g_mode = MODE_NORMAL;
    if (g_runtime_store_ready && !runtime_state_save()) runtime_note_store_error();
}

/* 累积完整运行分钟并检查老化完成条件。 */
static void runtime_apply_elapsed_minutes(u32 elapsed_min)
{
    u32 remain_min;
    if ((elapsed_min == 0u) || (g_mode == MODE_NORMAL)) return;
    remain_min = (FACTORY_TIME_LIMIT_MIN > g_runtime_min) ?
                 (FACTORY_TIME_LIMIT_MIN - g_runtime_min) : 0u;
    if (elapsed_min >= remain_min) {
        g_runtime_min = FACTORY_TIME_LIMIT_MIN;
        runtime_finish_factory_mode();
        return;
    }
    g_runtime_min += elapsed_min;
    if (g_runtime_store_ready &&
        ((g_runtime_min - g_runtime_last_saved_min) >= RUNTIME_SAVE_INTERVAL_MIN) &&
        !runtime_state_save()) runtime_note_store_error();
}

/* 累计无符号 32K tick 差值并保留不足一分钟的余量；调用节拍必须小于计数器回绕周期。 */
static void runtime_apply_elapsed_ticks(u32 elapsed_tick_32k)
{
    u32 total_tick_32k;
    u32 elapsed_min;
    if ((elapsed_tick_32k == 0u) || (g_mode == MODE_NORMAL)) return;
    total_tick_32k = g_runtime_pending_tick_32k + elapsed_tick_32k;
    elapsed_min = total_tick_32k / RUNTIME_PM_TICKS_PER_MIN;
    g_runtime_pending_tick_32k = total_tick_32k % RUNTIME_PM_TICKS_PER_MIN;
    runtime_apply_elapsed_minutes(elapsed_min);
}

/* 加载工厂模式状态并初始化 32K 计时基准。 */
void Runtime_Init(void)
{
    runtime_set_initial_state();
    if (bms_state_store_init()) {
        g_runtime_store_ready = 1u;
        g_runtime_min = bms_state_store_get_runtime_min();
        g_runtime_last_saved_min = g_runtime_min;
    }
    g_mode = (g_runtime_min >= FACTORY_TIME_LIMIT_MIN) ? MODE_NORMAL : MODE_FACTORY;
    g_runtime_last_tick_32k = pm_get_32k_tick();
    g_runtime_tick_ready = 1u;
}

/* 消费当前运行时间差并推进老化计时。 */
void Runtime_Poll(void)
{
    u32 now_tick_32k;
    /* 工厂老化已结束；重入/复位自行建立时基，正常运行不需读取 PM 时钟或计算时间差。 */
    if (g_mode == MODE_NORMAL) return;
    now_tick_32k = pm_get_32k_tick();
    if (!g_runtime_tick_ready) {
        g_runtime_last_tick_32k = now_tick_32k;
        g_runtime_tick_ready = 1u;
        return;
    }
    runtime_apply_elapsed_ticks(now_tick_32k - g_runtime_last_tick_32k);
    g_runtime_last_tick_32k = now_tick_32k;
}

/* 深睡前重设运行计时基准，不把睡眠时长计入老化。 */
void Runtime_PrepareForDeepSleep(void)
{
    /* 老化累计只计算 BMS 唤醒执行时间，不补偿深睡时长。 */
    g_runtime_last_tick_32k = pm_get_32k_tick();
    g_runtime_tick_ready = 1u;
}

/* 取消深睡准备并恢复运行计时基准。 */
void Runtime_CancelPendingDeepSleep(void)
{
    Runtime_PrepareForDeepSleep();
}

/* 查询当前运行或工厂老化模式。 */
bms_mode_t Runtime_GetMode(void) { return g_mode; }

/* 恢复工厂模式默认状态和累计时间。 */
int Runtime_FactoryReset(void)
{
    if (!bms_state_store_init() || !bms_state_store_reset_runtime()) return 0;
    runtime_set_initial_state();
    g_runtime_store_ready = 1u;
    g_runtime_last_tick_32k = pm_get_32k_tick();
    g_runtime_tick_ready = 1u;
    return 1;
}

/* 重新进入工厂老化模式并重建计时状态。 */
int Runtime_ReenterFactoryMode(void) { return Runtime_FactoryReset(); }
