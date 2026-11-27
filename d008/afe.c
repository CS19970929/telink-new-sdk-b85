#include "afe.h"
#include "dvc_bus.h"
#include <string.h>

enum { CC2_DONE = 0x10, VADC_DONE = 0x40, ADC_DONE = CC2_DONE|VADC_DONE };

static const Config *setup;
static uint8_t setup_index, seen, progress, drive_cache;
static bool outputs_known, activity_started;
static uint32_t activity_ms, balance_cache, balance_ms;
static uint16_t rpu;
static const uint32_t ntc_ohms[] = {
    248277, 182221, 135452, 101898, 77523, 59606, 46290, 36290, 28704, 22897, 18410, 14916,
    12171, 10000, 8269, 6881, 5759, 4847, 4101, 3488, 2981, 2559, 2207, 1912,
    1662, 1451, 1272, 1118, 987, 874, 776, 692, 618, 554
}; /* 10 kΩ，B3435，−40 到 125 °C，每 5 °C。 */

static uint16_t be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static bool temperature(uint16_t gp, uint16_t ref, uint16_t *value)
{
    if (!gp || gp >= ref || ref < 15000 || ref > 21000) return false;
    uint32_t ohms = (uint32_t)gp * rpu / (ref - gp);
    unsigned count = sizeof(ntc_ohms)/sizeof(ntc_ohms[0]);
    if (ohms > ntc_ohms[0] || ohms < ntc_ohms[count-1]) return false;
    for (unsigned i = 1; i < count; ++i) {
        if (ohms >= ntc_ohms[i]) {
            *value = (i-1)*50 + (ntc_ohms[i-1]-ohms)*50/(ntc_ohms[i-1]-ntc_ohms[i]);
            return true;
        }
    }
    return false;
}

static uint8_t voltage_delay(uint16_t ms)
{
    return ms < 1000 ? (ms-200)/100 : 7+ms/1000;
}

void afe_begin(const Config *config)
{
    setup = config; setup_index = 0; seen = progress = 0;
    activity_started = outputs_known = false;
}

/* 每次最多配置一个地址，让主循环在寄存器之间服务 BLE。 */
AfeResult afe_setup_step(void)
{
    if (!setup || !config_valid(setup)) return AFE_ERROR;
    const Config *c = setup;
    uint32_t absent = 0xffffffu & ~((1u << c->cells)-1u);
    uint16_t ov = c->hw_ov_mv - 500;
    uint16_t uv = c->hw_uv_mv;
    struct Setting { uint8_t reg, value, writable; };
    const struct Setting settings[] = {
        {0x51,0,0xff},
        /* 双关确认后优先退出复位默认短路配置，保留 SCDE 和 SCD 锁存。 */
        {0x62,0x40 | (c->hw_sc_a10/400),0x7f}, {0x63,c->hw_sc_us*100u/781u,0xff},
        {0x67,0,0xff}, {0x68,0,0xff}, {0x69,0,0xff},
        {0x77,0xc4,0xe7}, /* 4 s 看门狗，保留 V3P3 供电。 */
        {0x53,0x50,0xff}, {0x54,0x78,0xff}, {0x66,2,0xff},
        {0x74,0x09,0xff}, /* GP1 高阻，GP2/3 为 NTC。 */
        {0x75,0x7f,0xff}, {0x55,0x88,0x8d},
        {0x6a,absent >> 16,0xff}, {0x6b,absent >> 8,0xff}, {0x6c,absent,0xff},
        {0x6d,0x28,0x3f}, {0x6e,0xc1,0xf3},
        {0x70,ov >> 4,0xff}, {0x71,(ov << 4) | voltage_delay(c->hw_ov_ms),0xff},
        {0x72,uv >> 4,0xff}, {0x73,(uv << 4) | voltage_delay(c->hw_uv_ms),0xff},
        {0x59,c->hw_d1_a10/10,0xff}, {0x5a,c->hw_c1_a10/10,0xff},
        {0x5b,c->hw_d1_ms/8-1,0xff}, {0x5c,c->hw_c1_ms/8-1,0xff},
        {0x5e,0x40 | (c->hw_d2_a10/160-1),0x7f}, {0x5f,0x40 | (c->hw_c2_a10/160-1),0x7f},
        {0x60,c->hw_d2_ms/4-1,0xff}, {0x61,c->hw_c2_ms/4-1,0xff}
    };
    if (setup_index < sizeof(settings)/sizeof(settings[0])) {
        const struct Setting *s = &settings[setup_index++];
        uint8_t value = s->value, old;
        if (s->writable != 0xff) {
            if (!dvc_read(s->reg, &old, 1)) return AFE_ERROR;
            value |= old & (uint8_t)~s->writable;
        }
        if (s->reg==0x51) {
            uint8_t fet;
            if (!dvc_startup_set(s->reg,value,s->writable) ||
                !dvc_read(0x06,&fet,1) || (fet & 3u)) return AFE_ERROR;
        } else if (!dvc_set(s->reg,value,s->writable)) return AFE_ERROR;
        return AFE_WAIT;
    }
    uint8_t fet, trim, status;
    if (!dvc_read(0x06,&fet,1) || (fet & 3) || !dvc_read(0x7e,&trim,1) ||
        !dvc_read(0x01,&status,1)) return AFE_ERROR;
    rpu = 6800u + trim*25u;
    /* 新读的完成标志已清。等新 CADC/VADC 活动后再交付。 */
    drive_cache = 0; balance_cache = 0; outputs_known = true;
    return AFE_OK;
}

AfeResult afe_sample(const Config *c, uint32_t ms, Sample *out)
{
    uint8_t header[7], therm[10], cells[16];
    Sample next = {0};
    if (!dvc_read(0,header,sizeof(header))) return AFE_ERROR;
    uint8_t flags = header[1] & ADC_DONE; /* 0x01 的 VADF 与 CC2F 读清 */
    seen |= flags; progress |= flags;
    if (!activity_started || progress == ADC_DONE) {
        activity_ms = ms; activity_started = true; progress = 0;
    }
    if (ms-activity_ms >= 1200u) return AFE_ERROR;
    if (seen != ADC_DONE) return AFE_WAIT;
    if (!dvc_read(0x0f,therm,sizeof(therm))) return AFE_ERROR;
    uint16_t ref=be16(therm);
    /* GP1 的两个字节仅随连续读包到达。 */
    for (unsigned i=0;i<NTC_COUNT;++i)
        if (!temperature(be16(therm+4+2*i),ref,&next.temp_x10[i])) return AFE_ERROR;
    next.alarms = header[0];
    next.fet = header[6] & 3;
    next.current_fresh = (flags & CC2_DONE) != 0;
    int32_t cc2 = ((int32_t)header[4] << 12) | (header[5] << 4) | (header[6] >> 4);
    if (cc2 & 0x80000) cc2 -= 0x100000;
    next.raw_ma = cc2 * 625 / (2 * (int32_t)SHUNT_UOHM);
    next.ma = (int32_t)(((int64_t)(next.raw_ma-c->offset_ma)*c->gain_ppm)/1000000);
    next.min_mv = UINT16_MAX;
    for (unsigned start = 0; start < c->cells; start += 8) {
        unsigned count = c->cells - start;
        if (count > 8) count = 8;
        if (!dvc_read(0x1d+start*2,cells,count*2)) return AFE_ERROR;
        for (unsigned j = 0; j < count; ++j) {
            unsigned i = start+j;
            uint16_t mv = (be16(&cells[2*j])+5u)/10u;
            next.mv[i] = mv; next.sum_mv += mv;
            if (mv < next.min_mv) {next.min_mv = mv; next.min_index = i;}
            if (mv > next.max_mv) {next.max_mv = mv; next.max_index = i;}
        }
    }
    *out = next;
    return AFE_OK;
}

bool afe_outputs(uint8_t drive, uint32_t balance, uint32_t ms)
{
    if ((!outputs_known || drive_cache != drive) && !dvc_set(0x51,drive,0xff)) {
        outputs_known = false; return false;
    }
    drive_cache = drive;
    if (!outputs_known || balance != balance_cache || (balance && ms-balance_ms >= 45000)) {
        uint8_t data[] = {balance >> 16,balance >> 8,balance}, check[3];
        if (!dvc_write(0x67,data,3) || !dvc_read(0x67,check,3) || memcmp(data,check,3)) {
            outputs_known = false; return false;
        }
        balance_cache = balance; balance_ms = ms;
    }
    outputs_known = true;
    return true;
}

bool afe_disable_outputs(uint32_t ms)
{
    /* 请求硬关闭时忽略缓存，实际重写并读回驱动和均衡寄存器。 */
    outputs_known=false;
    return afe_outputs(0,0,ms);
}

bool afe_ack(uint8_t flags, uint8_t *remaining)
{
    /* W0C。未确认恢复的位写 1，避免误清其他故障。 */
    uint8_t keep = (uint8_t)~flags;
    return dvc_write(0x00,&keep,1) && dvc_read(0x00,remaining,1);
}

bool afe_shutdown(void)
{
    uint8_t cmd = 0x0f;
    return dvc_write(0x01,&cmd,1);
}
