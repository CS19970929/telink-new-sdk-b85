/* 积分、端点和显示共用一份状态，输入时间来自应用的单调毫秒时基。 */
#include "bms_soc.h"
#include "bms_timing.h"
#include <string.h>

#define SOC_CURRENT_MAX_MA 2000000
#define SOC_VOLTAGE_AGE_MS 600u
#define SOC_FULL_MS 30000u
#define SOC_EMPTY_MS 5000u
#define SOC_CUTOFF_RECENT_MS 1500u
#define SOC_FULL_CUTOFF_MS 2000u
#define SOC_EMPTY_CUTOFF_MS 1000u
#define SOC_LINE_CHECK_MS 3200u
#define SOC_FULL_DELTA_MV 100u
#define SOC_EMPTY_DELTA_MV 300u
#define SOC_WEAK_DELTA_MV 650u
#define SOC_WEAK_NEAR 3u
#define SOC_REST_DELTA_MV 150u
#define SOC_REST_INITIAL_MS (60u * 60u * 1000u)
#define SOC_REST_CORRECT_MS (30u * 60u * 1000u)
#define SOC_REST_STABLE_MV 5u
#define SOC_INITIAL_WAIT_MS 2000u
#define SOC_INITIAL_PERCENT 60u
#define SOC_DISPLAY_MS 5000u
#define SOC_APPROACH_DISPLAY_MS 2000u
#define SOC_ENDPOINT_DISPLAY_MS 1000u
#define SOC_APPROACH_PERCENT 4
#define SOC_MA_MS_PER_UNIT 100000
#define SOC_CHARGE (-1)
#define SOC_DISCHARGE 1
#define SOC_CUTOFF_CHARGE 1u
#define SOC_CUTOFF_DISCHARGE 2u

typedef struct {
    uint16_t mv;
    uint8_t percent;
} ocv_point_t;

/* 固定起点模型。LFP 平台跳过运行校正，首次估算使用工装候选值。 */
static const ocv_point_t s_lfp[] = {{2844, 0}, {3163, 5}, {3204, 10}, {3445, 100}};
static const ocv_point_t s_nmc[] = {
    {3000, 0}, {3300, 5}, {3450, 10}, {3550, 20}, {3650, 30}, {3700, 40},
    {3750, 50}, {3800, 60}, {3850, 70}, {3900, 80}, {4000, 90}, {4100, 96}, {4180, 100}
};

static uint32_t capacity_as10(uint16_t capacity_0p1ah)
{
    return (uint32_t)capacity_0p1ah * BMS_SOC_UNITS_PER_0P1AH;
}

static uint16_t distance_u16(uint16_t a, uint16_t b)
{
    return a >= b ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

void bms_soc_defaults(uint8_t chemistry, bms_soc_limits_t *limits)
{
    limits->full_mv = chemistry == BMS_SOC_CHEMISTRY_NMC ? 4180u : 3500u;
    limits->empty_mv = chemistry == BMS_SOC_CHEMISTRY_NMC ? 3100u : 2850u;
}

uint8_t bms_soc_limits_valid(uint8_t chemistry, const bms_soc_limits_t *limits)
{
    if ((uint32_t)limits->empty_mv + 300u > limits->full_mv) return 0u;
    if (chemistry == BMS_SOC_CHEMISTRY_LFP)
        return limits->full_mv >= 3400u && limits->full_mv <= 3600u &&
            limits->empty_mv >= 2700u && limits->empty_mv <= 3050u;
    return chemistry == BMS_SOC_CHEMISTRY_NMC &&
        limits->full_mv >= 4050u && limits->full_mv <= 4200u &&
        limits->empty_mv >= 3000u && limits->empty_mv <= 3300u;
}

static uint32_t endpoint_basis(const bms_soc_config_t *config)
{
    /* 配置已校验。高低端点各占 10 位，另外包含固定装配串数和化学体系。 */
    return ((uint32_t)config->limits.full_mv - 3400u) |
        (((uint32_t)config->limits.empty_mv - 2700u) << 10) |
        ((uint32_t)config->cell_count << 20) | ((uint32_t)config->chemistry << 25);
}

uint8_t bms_soc_checkpoint_valid(const bms_soc_checkpoint_t *cp)
{
    uint32_t cap = capacity_as10(cp->capacity_0p1ah);
    return cp->capacity_0p1ah != 0u && cp->capacity_0p1ah <= BMS_SOC_CAPACITY_MAX_0P1AH &&
        cp->charge_as10 <= cap && cp->discharge_as10 < cap && cp->shown <= 100u &&
        cp->estimated <= 1u && cp->endpoint <= BMS_SOC_EMPTY &&
        ((cp->endpoint != BMS_SOC_NO_ENDPOINT) == (cp->endpoint_basis != 0u));
}

static uint32_t resize_charge(uint32_t charge, uint16_t from, uint16_t to)
{
    /* 先除额定容量再乘，余数乘积至多 6552*6553。 */
    return charge / from * to + (charge % from) * to / from;
}

static void clear_endpoint(bms_soc_t *soc)
{
    soc->state.endpoint = BMS_SOC_NO_ENDPOINT;
    soc->state.endpoint_basis = 0u;
    soc->display_ms = 0u;
}

static void clear_observation(bms_soc_t *soc)
{
    soc->near = BMS_SOC_NO_ENDPOINT;
    soc->near_elapsed_ms = 0u;
    soc->resting = 0u;
    soc->has_voltage = 0u;
    soc->drive = 0;
    soc->display_ms = 0u;
    soc->weak_check = 0u;
    soc->pending_cutoff = 0u;
}

void bms_soc_reconfigure(bms_soc_t *soc, const bms_soc_config_t *config)
{
    uint16_t previous = soc->state.capacity_0p1ah;
    uint16_t next = config->capacity_0p1ah;

    if (previous != next) {
        soc->state.charge_as10 = resize_charge(soc->state.charge_as10, previous, next);
        soc->state.discharge_as10 = resize_charge(soc->state.discharge_as10, previous, next);
        soc->state.capacity_0p1ah = next;
        soc->carry_ma_ms = 0;
        clear_endpoint(soc);
    }
    if (soc->state.endpoint_basis != endpoint_basis(config)) clear_endpoint(soc);
    soc->config = *config;
    clear_observation(soc);
}

void bms_soc_init(bms_soc_t *soc, const bms_soc_config_t *config,
                  const bms_soc_checkpoint_t *checkpoint, uint32_t now_ms)
{
    memset(soc, 0, sizeof(*soc));
    if (checkpoint && bms_soc_checkpoint_valid(checkpoint)) {
        soc->state = *checkpoint;
        bms_soc_reconfigure(soc, config);
    } else {
        soc->config = *config;
        soc->state.capacity_0p1ah = config->capacity_0p1ah;
        soc->state.charge_as10 = capacity_as10(config->capacity_0p1ah) * SOC_INITIAL_PERCENT / 100u;
        soc->state.shown = SOC_INITIAL_PERCENT;
        soc->initial_estimate_pending = 1u;
        soc->init_start_ms = now_ms;
    }
}

uint8_t bms_soc_percent(const bms_soc_t *soc)
{
    uint32_t cap = capacity_as10(soc->state.capacity_0p1ah);
    return (uint8_t)((soc->state.charge_as10 * 100u + cap / 2u) / cap);
}

static void integrate(bms_soc_t *soc, uint32_t cap, uint32_t elapsed)
{
    int32_t product = soc->previous_ma * (int32_t)elapsed + soc->carry_ma_ms;
    int32_t delta = product / SOC_MA_MS_PER_UNIT;
    bms_soc_checkpoint_t *state = &soc->state;

    soc->carry_ma_ms = product - delta * SOC_MA_MS_PER_UNIT;
    if (delta > 0) {
        uint32_t used = (uint32_t)delta;
        uint32_t throughput = state->discharge_as10 + used;
        if (throughput >= cap) {
            uint32_t cycles = state->cycles + throughput / cap;
            state->cycles = cycles > UINT16_MAX ? UINT16_MAX : (uint16_t)cycles;
            throughput %= cap;
        }
        state->discharge_as10 = throughput;
        state->charge_as10 = used >= state->charge_as10 ? 0u : state->charge_as10 - used;
    } else if (delta < 0) {
        uint32_t gained = (uint32_t)(-delta);
        state->charge_as10 = gained >= cap - state->charge_as10 ? cap : state->charge_as10 + gained;
    }
    if ((state->charge_as10 == 0u && soc->carry_ma_ms > 0) ||
        (state->charge_as10 == cap && soc->carry_ma_ms < 0))
        soc->carry_ma_ms = 0;
}

static uint8_t plateau(const bms_soc_t *soc, const bms_soc_sample_t *m)
{
    return soc->config.chemistry == BMS_SOC_CHEMISTRY_LFP &&
        m->min_mv <= 3400u && m->max_mv >= 3200u;
}

static uint8_t ocv_percent(uint8_t chemistry, uint16_t mv)
{
    const ocv_point_t *curve = chemistry == BMS_SOC_CHEMISTRY_LFP ? s_lfp : s_nmc;
    unsigned count = chemistry == BMS_SOC_CHEMISTRY_LFP ?
        sizeof(s_lfp) / sizeof(s_lfp[0]) : sizeof(s_nmc) / sizeof(s_nmc[0]);
    unsigned i;

    if (mv <= curve[0].mv) return curve[0].percent;
    for (i = 1u; i < count; ++i) {
        uint16_t width = (uint16_t)(curve[i].mv - curve[i - 1u].mv);
        if (mv <= curve[i].mv) {
            uint32_t rise = (uint32_t)(mv - curve[i - 1u].mv) *
                (uint32_t)(curve[i].percent - curve[i - 1u].percent);
            return (uint8_t)(curve[i - 1u].percent + (rise + width - 1u) / width);
        }
    }
    return curve[count - 1u].percent;
}

static void rest_service(bms_soc_t *soc, const bms_soc_sample_t *m, uint32_t cap, int8_t dir)
{
    if (dir || !m->resting_allowed || !soc->has_voltage || plateau(soc, m) ||
        m->max_mv - m->min_mv > (int)SOC_REST_DELTA_MV ||
        soc->state.endpoint != BMS_SOC_NO_ENDPOINT) {
        soc->resting = 0u;
        return;
    }
    if (!m->voltage_fresh) return;
    if (!soc->resting || distance_u16(m->min_mv, soc->rest_min_mv) > SOC_REST_STABLE_MV ||
        distance_u16(m->max_mv, soc->rest_max_mv) > SOC_REST_STABLE_MV ||
        distance_u16(m->mean_mv, soc->rest_mean_mv) > SOC_REST_STABLE_MV ||
        distance_u16(m->temperature_x10, soc->rest_temp_x10) > 20u) {
        soc->rest_min_mv = m->min_mv;
        soc->rest_max_mv = m->max_mv;
        soc->rest_mean_mv = m->mean_mv;
        soc->rest_temp_x10 = m->temperature_x10;
        soc->rest_start_ms = m->now_ms;
        soc->resting = 1u;
        return;
    }
    if (m->now_ms - soc->rest_start_ms >= SOC_REST_INITIAL_MS + SOC_REST_CORRECT_MS) {
        uint32_t upper_percent = ocv_percent(soc->config.chemistry, m->mean_mv) +
            (soc->config.chemistry == BMS_SOC_CHEMISTRY_LFP ? 7u : 5u);
        uint32_t upper = cap * upper_percent / 100u;
        if (soc->state.charge_as10 > upper) {
            uint32_t delta = soc->state.charge_as10 - upper;
            uint32_t limit = cap / 100u;
            soc->state.charge_as10 -= delta < limit ? delta : limit;
            soc->carry_ma_ms = 0;
        }
        /* 持续合格静置每 30 分钟最多下调一个百分点。 */
        soc->rest_start_ms = m->now_ms - SOC_REST_INITIAL_MS;
    }
}

static void anchor(bms_soc_t *soc, uint32_t cap, uint8_t endpoint)
{
    if (soc->state.endpoint == endpoint) return;
    soc->state.charge_as10 = endpoint == BMS_SOC_FULL ? cap : 0u;
    soc->state.endpoint = endpoint;
    soc->state.endpoint_basis = endpoint_basis(&soc->config);
    soc->state.estimated = 1u;
    soc->carry_ma_ms = 0;
    soc->display_ms = 0u;
    soc->near = BMS_SOC_NO_ENDPOINT;
    soc->resting = 0u;
}

static uint8_t endpoint_service(bms_soc_t *soc, const bms_soc_sample_t *m,
                                uint32_t cap, int8_t dir, uint8_t cutoff_edge)
{
    uint32_t now = m->now_ms;
    uint16_t full = soc->config.limits.full_mv;
    uint16_t empty = soc->config.limits.empty_mv;
    uint16_t delta = (uint16_t)(m->max_mv - m->min_mv);
    uint8_t candidate = BMS_SOC_NO_ENDPOINT;
    uint8_t recent = soc->drive && now - soc->drive_ms <= SOC_CUTOFF_RECENT_MS;

    if (soc->state.endpoint != BMS_SOC_NO_ENDPOINT) return 0u;
    if (soc->weak_check) {
        /* 弱串 UV 发生后仅消费这一次有界线路检查。保留截断前低压证据。 */
        if (dir == SOC_CHARGE || !m->discharge_cutoff ||
            now - soc->near_start_ms > SOC_LINE_CHECK_MS) soc->weak_check = 0u;
        else if (m->line_check_active) soc->weak_check = 2u;
        else if (soc->weak_check == 2u) {
            soc->weak_check = 0u;
            if (m->line_check_passed && m->voltage_valid && m->voltage_fresh &&
                m->min_mv <= empty + 200u && delta <= SOC_WEAK_DELTA_MV)
                anchor(soc, cap, BMS_SOC_EMPTY);
        }
        return 0u;
    }

    if (!soc->has_voltage || !recent || (dir && dir != soc->drive)) {
        soc->near = BMS_SOC_NO_ENDPOINT;
        return 0u;
    }
    if (!m->voltage_fresh) return 0u;

    if (soc->drive == SOC_CHARGE && !m->discharge_cutoff &&
        m->max_mv + 20u >= full && m->min_mv + SOC_FULL_DELTA_MV >= full &&
        delta <= SOC_FULL_DELTA_MV) {
        if (soc->near == BMS_SOC_FULL && soc->near_elapsed_ms >= SOC_FULL_CUTOFF_MS &&
            ((cutoff_edge & SOC_CUTOFF_CHARGE) || !dir)) {
            anchor(soc, cap, BMS_SOC_FULL);
            return 0u;
        }
        if (dir == SOC_CHARGE && m->max_mv >= full && !m->charge_cutoff)
            candidate = BMS_SOC_FULL;
    }
    if (soc->drive == SOC_DISCHARGE && !m->charge_cutoff) {
        if ((cutoff_edge & SOC_CUTOFF_DISCHARGE) &&
            (soc->near == BMS_SOC_EMPTY || soc->near == SOC_WEAK_NEAR) &&
            soc->near_elapsed_ms >= SOC_EMPTY_CUTOFF_MS &&
            m->min_mv <= empty + 120u && delta <= SOC_WEAK_DELTA_MV) {
            if (soc->near == BMS_SOC_EMPTY && delta <= SOC_EMPTY_DELTA_MV) {
                anchor(soc, cap, BMS_SOC_EMPTY);
                return 0u;
            }
            soc->weak_check = 1u;
            soc->near = BMS_SOC_NO_ENDPOINT;
            soc->near_start_ms = now;
            return 1u;
        }
        if (dir == SOC_DISCHARGE && m->min_mv <= empty &&
            delta <= SOC_WEAK_DELTA_MV && !m->discharge_cutoff)
            candidate = delta <= SOC_EMPTY_DELTA_MV ? BMS_SOC_EMPTY : SOC_WEAK_NEAR;
    }

    /* 突然跌落须重建低压历程；单个新 VADF 无法确认为空。 */
    if (candidate != soc->near ||
        ((candidate == BMS_SOC_EMPTY || candidate == SOC_WEAK_NEAR) &&
         m->min_mv + 150u < soc->near_min_mv)) {
        soc->near_start_ms = now;
        soc->near_elapsed_ms = 0u;
    }
    soc->near = candidate;
    soc->near_min_mv = m->min_mv;
    if (candidate == BMS_SOC_NO_ENDPOINT) return 0u;
    {
        uint32_t elapsed = now - soc->near_start_ms;
        soc->near_elapsed_ms = elapsed < SOC_FULL_MS ? (uint16_t)elapsed : SOC_FULL_MS;
    }
    if (candidate == BMS_SOC_FULL && soc->near_elapsed_ms >= SOC_FULL_MS)
        anchor(soc, cap, BMS_SOC_FULL);
    if (candidate == BMS_SOC_EMPTY && soc->near_elapsed_ms >= SOC_EMPTY_MS &&
        delta <= SOC_EMPTY_DELTA_MV)
        anchor(soc, cap, BMS_SOC_EMPTY);
    return 0u;
}

static void follow_display(bms_soc_t *soc, uint32_t elapsed, int8_t dir, uint16_t delta_mv)
{
    int target = bms_soc_percent(soc);
    uint16_t pace = SOC_DISPLAY_MS;
    uint8_t end = soc->state.endpoint;
    uint8_t shown = soc->state.shown;

    if (end != BMS_SOC_NO_ENDPOINT) {
        pace = SOC_ENDPOINT_DISPLAY_MS;
        target = end == BMS_SOC_FULL ? 100 : 0;
    } else {
        if (soc->near == BMS_SOC_FULL && dir == SOC_CHARGE) {
            target += SOC_APPROACH_PERCENT;
            pace = SOC_APPROACH_DISPLAY_MS;
        } else if (soc->near == BMS_SOC_EMPTY && dir == SOC_DISCHARGE &&
                   delta_mv <= SOC_EMPTY_DELTA_MV) {
            target -= SOC_APPROACH_PERCENT;
            pace = SOC_APPROACH_DISPLAY_MS;
        }
        if (target < 1) target = 1;
        if (target > 99) target = 99;
    }

    if (target == shown || (target > shown && dir != SOC_CHARGE && end != BMS_SOC_FULL) ||
        (target < shown && dir == SOC_CHARGE)) {
        soc->display_ms = 0u;
        return;
    }
    /* 切换节拍时不把旧计时折算成多步，避免末段切换产生跳变。 */
    if (soc->display_ms >= pace) soc->display_ms = 0u;
    soc->display_ms = (uint16_t)(soc->display_ms + elapsed);
    if (soc->display_ms >= pace) {
        soc->state.shown = target > shown ? shown + 1u : shown - 1u;
        soc->display_ms = (uint16_t)(soc->display_ms - pace);
    }
}

uint8_t bms_soc_step(bms_soc_t *soc, const bms_soc_sample_t *m)
{
    uint32_t elapsed = m->now_ms - soc->previous_ms;
    uint32_t cap = capacity_as10(soc->state.capacity_0p1ah);
    uint8_t valid = m->current_valid && m->current_ma >= -SOC_CURRENT_MAX_MA &&
        m->current_ma <= SOC_CURRENT_MAX_MA &&
        m->now_ms - m->current_time_ms <= BMS_CURRENT_MAX_AGE_MS;
    uint8_t cutoff = (m->charge_cutoff ? SOC_CUTOFF_CHARGE : 0u) |
        (m->discharge_cutoff ? SOC_CUTOFF_DISCHARGE : 0u);
    uint8_t cutoff_edge = (uint8_t)(cutoff & ~soc->cutoff);
    int8_t dir;
    uint8_t check;

    if (!valid) {
        soc->previous_valid = 0u;
        soc->cutoff = cutoff;
        clear_observation(soc);
        return 0u;
    }
    if (soc->previous_valid && !elapsed) return 0u;
    if (soc->previous_valid && elapsed <= BMS_SAMPLE_MAX_POLL_GAP_MS &&
        m->now_ms - soc->previous_current_ms <= BMS_CURRENT_MAX_AGE_MS) {
        integrate(soc, cap, elapsed);
    } else {
        clear_observation(soc);
        elapsed = 0u;
        cutoff_edge = 0u;
    }
    soc->previous_ms = m->now_ms;
    soc->previous_current_ms = m->current_time_ms;
    soc->previous_ma = m->current_ma;
    soc->previous_valid = 1u;
    soc->cutoff = cutoff;
    soc->pending_cutoff = (uint8_t)((soc->pending_cutoff | cutoff_edge) & cutoff);

    if (!m->voltage_valid || m->line_check_active) soc->has_voltage = 0u;
    else if (m->voltage_fresh) {
        if (!soc->has_voltage || m->now_ms - soc->voltage_ms > SOC_VOLTAGE_AGE_MS)
            soc->near = BMS_SOC_NO_ENDPOINT;
        soc->voltage_ms = m->now_ms;
        soc->has_voltage = 1u;
    } else if (m->now_ms - soc->voltage_ms > SOC_VOLTAGE_AGE_MS) soc->has_voltage = 0u;

    dir = m->current_ma > (int32_t)BMS_CURRENT_UNRELIABLE_MAX_MA ? SOC_DISCHARGE :
        m->current_ma < -(int32_t)BMS_CURRENT_UNRELIABLE_MAX_MA ? SOC_CHARGE : 0;
    if (dir) {
        if (dir != soc->drive) soc->near = BMS_SOC_NO_ENDPOINT;
        soc->drive = dir;
        soc->drive_ms = m->now_ms;
    }
    if (soc->initial_estimate_pending) {
        if (!dir && soc->has_voltage && m->voltage_fresh && m->resting_allowed &&
            m->max_mv - m->min_mv <= (int)SOC_REST_DELTA_MV) {
            uint8_t pct = plateau(soc, m) ? SOC_INITIAL_PERCENT :
                ocv_percent(soc->config.chemistry, m->mean_mv);
            if (pct == 0u) pct = 1u;
            if (pct == 100u) pct = 99u;
            soc->state.charge_as10 = cap * pct / 100u;
            soc->state.shown = pct;
            soc->state.estimated = !plateau(soc, m);
            soc->carry_ma_ms = 0;
            soc->initial_estimate_pending = 0u;
        } else if (dir || m->now_ms - soc->init_start_ms >= SOC_INITIAL_WAIT_MS) {
            soc->initial_estimate_pending = 0u;
        }
    }
    if ((soc->state.endpoint == BMS_SOC_FULL &&
         (dir == SOC_DISCHARGE || cap - soc->state.charge_as10 >= cap / 100u)) ||
        (soc->state.endpoint == BMS_SOC_EMPTY &&
         (dir == SOC_CHARGE || soc->state.charge_as10 >= cap / 100u)))
        clear_endpoint(soc);

    check = endpoint_service(soc, m, cap, dir, soc->pending_cutoff);
    if (m->voltage_fresh) soc->pending_cutoff = 0u;
    rest_service(soc, m, cap, dir);
    follow_display(soc, elapsed, dir, (uint16_t)(m->max_mv - m->min_mv));
    return check;
}
