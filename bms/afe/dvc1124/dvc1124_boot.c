/* 文件功能：DVC1124 的板级配置应用、初始化与 shutdown 唤醒；保持 D008 专用脉冲和供电时序。 */
#include "dvc1124_boot.h"
#include "bms_afe_driver.h"

#include "tl_common.h"
#include "drivers.h"
#include "conf.h"
#include "dvc1124_project_config.h"
#include <string.h>

/* 应用 D008 编译期板级配置，并通过公共 AFE profile 初始化硬件保护。 */
static uint8_t s_project_config_pending = 1u;

/*
 * DVC1124-2 shutdown I2C wake requirement:
 *   SCL must be at least 2 V above SDA for more than 50 us.
 *
 * HS-D008 uses PC0=SDA and PC1=SCL. Use 1 ms for margin. The wake pulse is
 * generated while the Telink I2C peripheral is reset/disconnected from the
 * pads; only after SDA is released again does normal hardware I2C resume.
 */
#define DVC1124_I2C_WAKE_PULSE_US       1000u
#define DVC1124_I2C_WAKE_MIN_US           50u
#define DVC1124_AFE_ENABLE_SETTLE_US   20000u
#if (DVC1124_I2C_WAKE_PULSE_US <= DVC1124_I2C_WAKE_MIN_US)
#error "DVC1124 shutdown I2C wake pulse must be greater than 50 us"
#endif

static void dvc_project_delay_us(uint32_t delay_us)
{
    uint32_t tick = clock_time();

    while (!clock_time_exceed(tick, delay_us))
    {
    }
}

static void dvc_project_enable_afe_interface(void)
{
    /* HS-D008 MCU-AFE-EN / AFE1-PRO-EN is PD7, active high. Bring the AFE
     * interface up before applying the PC0/PC1 shutdown-wake condition. */
    gpio_set_func(AFE1_PRO_EN_PIN, AS_GPIO);
    gpio_write(AFE1_PRO_EN_PIN, 1u);
    gpio_set_input_en(AFE1_PRO_EN_PIN, 0u);
    gpio_set_output_en(AFE1_PRO_EN_PIN, 1u);
    dvc_project_delay_us(DVC1124_AFE_ENABLE_SETTLE_US);
}

static void dvc_project_i2c_wake_pulse(void)
{
    /* Ensure the peripheral cannot drive PC0/PC1 while GPIO owns the pulse. */
    reset_i2c_module();

    /* SCL/PC1: released high through the same 10K pull-up used by the SDK I2C
     * setup. Do not push-pull high an open-drain bus. */
    gpio_set_func(GPIO_PC1, AS_GPIO);
    gpio_write(GPIO_PC1, 1u);
    gpio_set_output_en(GPIO_PC1, 0u);
    gpio_set_input_en(GPIO_PC1, 1u);
    gpio_setup_up_down_resistor(GPIO_PC1, PM_PIN_PULLUP_10K);

    /* SDA/PC0: actively pull low while SCL remains released high. Program the
     * output latch before enabling output to avoid a high-going glitch. */
    gpio_set_func(GPIO_PC0, AS_GPIO);
    gpio_write(GPIO_PC0, 0u);
    gpio_setup_up_down_resistor(GPIO_PC0, PM_PIN_PULLUP_10K);
    gpio_set_input_en(GPIO_PC0, 0u);
    gpio_set_output_en(GPIO_PC0, 1u);

    dvc_project_delay_us(DVC1124_I2C_WAKE_PULSE_US);

    /* Release SDA. Both lines are now idle-high before the hardware I2C mux is
     * enabled again. */
    gpio_set_output_en(GPIO_PC0, 0u);
    gpio_set_input_en(GPIO_PC0, 1u);
    gpio_write(GPIO_PC0, 1u);
}

uint8_t dvc1124_backend_enter_shutdown(void)
{
    uint8_t cmd = (uint8_t)DVC1124_CST_ENTER_SHUTDOWN;

    /* The command itself is the last normal I2C transaction. Do not read back
     * STATUS afterwards because a successful shutdown intentionally removes
     * the AFE from normal I2C communication. */
    return DVC1124_WriteRegisters(DVC1124_REG_STATUS, &cmd, 1u);
}

static void dvc_project_reset_with_shutdown_wake(void)
{
    dvc_project_enable_afe_interface();
    dvc_project_i2c_wake_pulse();
    DVC1124_AFE_Reset();
}

void dvc1124_backend_init(void)
{
    s_project_config_pending = 1u;
    dvc_project_reset_with_shutdown_wake();

    /* dvc1124.c applies the safety baseline and persistent HW protection
     * profile.  The first valid sample below then finalizes every fixed
     * operating field from compile-time product policy. */
    DVC1124_UpdataAfeConfig();

    /*
     * Boot-only current-zero learning stays inside initialization. The low-level
     * routine independently disables and verifies every DVC power-path output,
     * runs CAMZ, then learns a two-sample residual offset in RAM. Failure is
     * fail-open for startup: persistent factory offset/gain remain active.
     */
    (void)DVC1124_BootCurrentZeroCalibrate();
}

void dvc1124_backend_sample(void)
{
    dvc1124_snapshot_t snapshot;

    DVC1124_BmsApp_AFEGet();

    if (!s_project_config_pending) return;

    DVC1124_GetSnapshot(&snapshot);
    if (!snapshot.valid) return;

    if (DVC1124_ApplyProjectOperatingConfig())
    {
        s_project_config_pending = 0u;
        return;
    }

    /* Never release communication inhibit with a partially-applied fixed
     * product configuration. Reset invalidates the snapshot; the common guard
     * will remain inhibited and retry through the normal re-init path. The
     * shutdown wake pulse is repeated so recovery also covers an AFE that has
     * independently entered shutdown. */
    dvc_project_reset_with_shutdown_wake();
}

uint8_t dvc1124_backend_apply_protection_config(void)
{
    /* Only protection parameters are runtime/Flash-owned. */
    return DVC1124_ApplyProtectionConfig();
}

uint8_t dvc1124_backend_sleep(void)
{
    return DVC1124_AFE_Sleep();
}
