#include "bms_adc.h"

#include "adc.h"
#include "clock.h"
#include "gpio.h"
// #include <stddef.h>
#include "sci_upper.h"
#include "conf.h"

extern struct stCell_Info g_stCellInfoReport;

// ===================== 你板子的三路 ADC 引脚 =====================
#define PIN_ADC_NTC2        GPIO_PB4   // ADC-NTC  (电池温度 NTC2)
#define PIN_ADC_VBUS        GPIO_PB5   // ADC-VBUS (母线/端口电压)
#define PIN_ADC_NTC3_MOS    GPIO_PC4   // ADC-NMOS (MOS温度 NTC3)

// ===================== 需要你填：BUSEN/EN 实际GPIO =====================
// 原理图网络名：ADC-BUSEN / ADC-EN
// 你工程里把它们映射成真实 GPIO，比如 GPIO_PD2 之类
#ifndef PIN_ADC_BUSEN
#define PIN_ADC_BUSEN       GPIO_PA0   // TODO: 改成你的 ADC-BUSEN 实际脚
#endif

#ifndef PIN_ADC_EN
#define PIN_ADC_EN          GPIO_PA1   // TODO: 改成你的 ADC-EN 实际脚（若没有可不接）
#endif

// ===================== 硬件参数（来自原理图阻值） =====================
// VBUS 分压：Rtop=470k, Rbot=15k
#define VBUS_RTOP_OHM       470000u
#define VBUS_RBOT_OHM       15000u
// VBUS 还原增益 = (Rtop+Rbot)/Rbot ≈ 32.333...
// 用有理数近似：32333/1000
#define VBUS_GAIN_NUM       32333u
#define VBUS_GAIN_DEN       1000u

// NTC 上拉：10k；串联电阻：NTC2 串 100Ω，NTC3 串 1k
#define NTC_PULLUP_OHM      10000u
#define NTC2_SERIES_OHM     100u
#define NTC3_SERIES_OHM     1000u

// 上拉电源：3V3-SPS，默认按 3300mV（你也可以换成实际测得的 VDD）
#define VPU_MV              3300u

// VBUS 采样 RC 稳定等待（高阻分压 + 开关，建议 2~5ms）
#define VBUS_SETTLE_US      3000u
// NTC 采样也给一点点建立时间（可更小）
#define NTC_SETTLE_US       50u

static uint8_t s_inited = 0;

// ----------------- 小工具：延时 us（用 Telink clock_time） -----------------
static inline void delay_us(uint32_t us)
{
    uint32_t t0 = clock_time();
    while(!clock_time_exceed(t0, us)) {}
}

// ----------------- 使能脚控制（默认高有效；如你硬件低有效就取反） --------------
static void gpio_out_init(GPIO_PinTypeDef pin, int level)
{
    gpio_set_func(pin, AS_GPIO);
    gpio_set_input_en(pin, 0);
    gpio_set_output_en(pin, 1);
    gpio_write(pin, level ? 1 : 0);
}

static inline void ADC_EN_ON(void)    { gpio_write(PIN_ADC_EN, 1); }
static inline void ADC_EN_OFF(void)   { gpio_write(PIN_ADC_EN, 0); }
static inline void BUSEN_ON(void)     { gpio_write(PIN_ADC_BUSEN, 1); }
static inline void BUSEN_OFF(void)    { gpio_write(PIN_ADC_BUSEN, 0); }

// ----------------- NTC：由电压(mV)估算电阻(Ω) -----------------
// 电路模型：VPU -- Rpullup(10k) -- (Rseries + Rntc) -- GND
// Vnode = VPU * (Rseries + Rntc)/(Rpullup + Rseries + Rntc)
// => Rseries + Rntc = Rpullup * Vnode/(VPU - Vnode)
// => Rntc = 上式 - Rseries
static uint32_t ntc_ohm_from_mv(uint32_t v_mv, uint32_t series_ohm)
{
#if 0
    if (v_mv == 0 || v_mv >= (VPU_MV - 1)) {
        return 0; // 0 或接近 VPU，视为开路/异常
    }

    // 用 64 位防溢出：R = Rpullup * V / (VPU - V)
    uint64_t num = (uint64_t)NTC_PULLUP_OHM * (uint64_t)v_mv;
    uint32_t den = (VPU_MV - v_mv);

    uint32_t r_total = (uint32_t)(num / den); // Rseries + Rntc
    if (r_total <= series_ohm) {
        return 0;
    }
    return (r_total - series_ohm);
#endif
}

// ----------------- 对外：初始化 -----------------
void bms_adc_init(void)
{
    if (s_inited) return;

    // 初始化使能脚（如果你板子没有 ADC_EN 或 BUSEN，可自行注释）
    gpio_out_init(PIN_ADC_EN, 1);
    gpio_out_init(PIN_ADC_BUSEN, 0);

    // ADC 底层初始化（你已有 SDK adc.c）
    adc_init();

    // 用任一 ADC 引脚做 base_init（配置 vref/res/tsample/prescaler 等）
    // 你原始 adc_base_init 默认 prescaler=1/8，建议改成 1/4（更适合 0~3.3V）
    adc_base_init(PIN_ADC_NTC2);
    adc_set_ain_pre_scaler(ADC_PRESCALER_1F4);

    s_inited = 1;
}

// ----------------- 读取某 ADC 引脚节点电压(mV) -----------------
uint32_t bms_adc_read_pin_mv(GPIO_PinTypeDef pin)
{
    if (!s_inited) bms_adc_init();

    // 让 ADC 对应输入切换到该 pin
    adc_base_pin_init(pin);

    // 给一点建立时间
    delay_us(NTC_SETTLE_US);

    // 返回 mV（由 adc.c 内部换算）
    return (uint32_t)adc_sample_and_get_result();
}

uint32_t bms_adc_read_ntc2_mv(void)
{
    ADC_EN_ON();
    return bms_adc_read_pin_mv(PIN_ADC_NTC2);
}

uint32_t bms_adc_read_ntc3_mv(void)
{
    ADC_EN_ON();
    return bms_adc_read_pin_mv(PIN_ADC_NTC3_MOS);
}

// ----------------- 读取 VBUS（分压还原） -----------------
uint32_t bms_adc_read_vbus_mv(void)
{
    if (!s_inited) bms_adc_init();

    ADC_EN_ON();

    BUSEN_ON();
    delay_us(VBUS_SETTLE_US);

    uint32_t v_div_mv = bms_adc_read_pin_mv(PIN_ADC_VBUS);

    // g_stCellInfoReport.u16VCell[29] = v_bat_mv;
	// g_stCellInfoReport.u16VCell[30] = ntc1_mv;
	g_stCellInfoReport.u16VCell[31] = v_div_mv;
    

    BUSEN_OFF();

#if 0
    // VBUS = Vdiv * 32333 / 1000  （四舍五入）
    uint64_t num = (uint64_t)v_div_mv * (uint64_t)VBUS_GAIN_NUM + (VBUS_GAIN_DEN / 2);
    return (uint32_t)(num / VBUS_GAIN_DEN);
#endif
}

// ----------------- 一次性读三路 -----------------
void bms_adc_read_all(bms_adc_sample_t *out)
{
    if (!out) return;
    if (!s_inited) bms_adc_init();

    out->ntc2_mv = bms_adc_read_ntc2_mv();
    out->ntc3_mv = bms_adc_read_ntc3_mv();
    out->vbus_mv = bms_adc_read_vbus_mv();

    out->ntc2_ohm = ntc_ohm_from_mv(out->ntc2_mv, NTC2_SERIES_OHM);
    out->ntc3_ohm = ntc_ohm_from_mv(out->ntc3_mv, NTC3_SERIES_OHM);
}


// s.ntc2_mv / s.ntc2_ohm：电池温度通道
// s.ntc3_mv / s.ntc3_ohm：MOS温度通道
// s.vbus_mv：母线电压
