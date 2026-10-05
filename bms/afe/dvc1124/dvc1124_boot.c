/*
 * 文件功能：DVC1124 的板级配置应用、初始化与 shutdown 唤醒；
 * 保持 D008 专用脉冲和供电时序。
 */
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
 * DVC1124-2 shutdown 的 I2C 唤醒要求：SCL 高于 SDA 至少 2 V 且持续超过 50 us。
 * HS-D008 使用 PC0=SDA、PC1=SCL，保守取 1 ms。
 * 脉冲期间 Telink I2C 外设复位并与引脚断开；释放 SDA 后才恢复硬件 I2C。
 */
#define DVC1124_I2C_WAKE_PULSE_US       1000u
#define DVC1124_I2C_WAKE_MIN_US           50u
#define DVC1124_AFE_ENABLE_SETTLE_US   20000u
#if (DVC1124_I2C_WAKE_PULSE_US <= DVC1124_I2C_WAKE_MIN_US)
#error "DVC1124 shutdown I2C wake pulse must be greater than 50 us"
#endif

/* 提供 DVC 板级初始化所需微秒等待。 */
static void dvc_project_delay_us(uint32_t delay_us)
{
    uint32_t tick = clock_time();

    while (!clock_time_exceed(tick, delay_us))
    {
    }
}

/* 配置并使能 D008 AFE 物理接口。 */
static void dvc_project_enable_afe_interface(void)
{
    /*
     * HS-D008 MCU-AFE-EN/AFE1-PRO-EN 为 PD7，高电平有效；
     * 施加 PC0/PC1 shutdown 唤醒条件前先开启 AFE 接口。
     */
    gpio_set_func(AFE1_PRO_EN_PIN, AS_GPIO);
    gpio_write(AFE1_PRO_EN_PIN, 1u);
    gpio_set_input_en(AFE1_PRO_EN_PIN, 0u);
    gpio_set_output_en(AFE1_PRO_EN_PIN, 1u);
    dvc_project_delay_us(DVC1124_AFE_ENABLE_SETTLE_US);
}

/* 按 D008 时序产生 I2C 唤醒脉冲。 */
static void dvc_project_i2c_wake_pulse(void)
{
    /* GPIO 产生脉冲时确保外设不能驱动 PC0/PC1。 */
    reset_i2c_module();

    /* SCL/PC1 通过 SDK I2C 使用的同一 10K 上拉释放为高；开漏总线不得推挽输出高。 */
    gpio_set_func(GPIO_PC1, AS_GPIO);
    gpio_write(GPIO_PC1, 1u);
    gpio_set_output_en(GPIO_PC1, 0u);
    gpio_set_input_en(GPIO_PC1, 1u);
    gpio_setup_up_down_resistor(GPIO_PC1, PM_PIN_PULLUP_10K);

    /* SCL 保持释放高时主动拉低 SDA/PC0。先设置输出锁存值再使能输出，避免高电平毛刺。 */
    gpio_set_func(GPIO_PC0, AS_GPIO);
    gpio_write(GPIO_PC0, 0u);
    gpio_setup_up_down_resistor(GPIO_PC0, PM_PIN_PULLUP_10K);
    gpio_set_input_en(GPIO_PC0, 0u);
    gpio_set_output_en(GPIO_PC0, 1u);

    dvc_project_delay_us(DVC1124_I2C_WAKE_PULSE_US);

    /* 释放 SDA；重新启用硬件 I2C 复用前两线均处于空闲高电平。 */
    gpio_set_output_en(GPIO_PC0, 0u);
    gpio_set_input_en(GPIO_PC0, 1u);
    gpio_write(GPIO_PC0, 1u);
}

/* 执行后端 shutdown 流程并返回通信结果。 */
uint8_t dvc1124_backend_enter_shutdown(void)
{
    uint8_t cmd = (uint8_t)DVC1124_CST_ENTER_SHUTDOWN;

    /*
     * shutdown 命令是最后一次正常 I2C 事务。成功后 AFE 会退出正常 I2C 通信，
     * 因此不要再回读 STATUS。
     */
    return DVC1124_WriteRegisters(DVC1124_REG_STATUS, &cmd, 1u);
}

/* 先唤醒 shutdown 状态再复位配置 DVC。 */
static void dvc_project_reset_with_shutdown_wake(void)
{
    dvc_project_enable_afe_interface();
    dvc_project_i2c_wake_pulse();
    DVC1124_AFE_Reset();
}

/* 初始化选定 AFE 后端并应用产品配置。 */
void dvc1124_backend_init(void)
{
    s_project_config_pending = 1u;
    dvc_project_reset_with_shutdown_wake();

    /*
     * dvc1124.c 应用安全基线和持久化硬件保护配置；
     * 下面的首个有效样本进一步落实编译期产品策略中的全部固定运行字段。
     */
    DVC1124_UpdataAfeConfig();

    /*
     * 仅启动时的电流零点学习留在初始化内。底层先独立关闭并校验全部 DVC 功率输出，
     * 执行 CAMZ，再用两个样本学习 RAM 残差偏置。失败不阻断启动，
     * 持久化工厂 offset/gain 仍有效。
     */
    (void)DVC1124_BootCurrentZeroCalibrate();
}

/* 采集 AFE 测量和状态，并更新样本有效性。 */
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

    /*
     * 固定产品配置未完整应用时不得解除通信禁止。复位会使快照失效，
     * 公共门禁保持禁止并沿正常重初始化路径重试；重复 shutdown 唤醒脉冲，
     * 也覆盖 AFE 自行进入 shutdown 的情况。
     */
    dvc_project_reset_with_shutdown_wake();
}

/* 应用独立 AFE 硬件保护配置并返回结果。 */
uint8_t dvc1124_backend_apply_protection_config(void)
{
    /* 只有保护参数归运行时/Flash 状态管理。 */
    return DVC1124_ApplyProtectionConfig();
}

/* 按器件与板级时序进入 AFE 休眠。 */
uint8_t dvc1124_backend_sleep(void)
{
    return DVC1124_AFE_Sleep();
}
