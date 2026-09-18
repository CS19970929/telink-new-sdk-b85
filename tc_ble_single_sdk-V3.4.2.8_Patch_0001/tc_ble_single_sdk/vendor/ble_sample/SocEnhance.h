#ifndef SOCENHANCE_H
#define SOCENHANCE_H

#include "conf.h"
#include "soc_kv_store.h"
#include "bms_soc_defs.h"

#define BMS_SOC_OCV_WAIT_CURRENT   0u
#define BMS_SOC_OCV_PREPARE        1u
#define BMS_SOC_OCV_READY          2u
#define BMS_SOC_OCV_CORRECT_DOWN   3u

#define BMS_SOC_LEARNING_NONE          0u
#define BMS_SOC_LEARNING_EMPTY_TO_FULL 1u
#define BMS_SOC_LEARNING_FULL_TO_EMPTY 2u

typedef struct
{
    uint8_t chemistry;                  /* AUTO/LFP/NMC */
    uint8_t profile_id;                 /* AUTO/generic LFP/generic NMC */
    uint16_t current_deadband_ma;       /* currents at/below this value are ignored; D008 floor also applies */
    uint16_t ocv_rest_prepare_s;        /* stable idle time before OCV may correct */
    uint8_t ocv_error_band_percent;     /* +/- percentage points around OCV center */
    uint8_t capacity_learning_enable;   /* default disabled */
    uint8_t hide_capacity_until_learned;/* active only when learning is enabled */
} bms_soc_config_t;

typedef struct
{
    uint8_t chemistry;
    uint8_t profile_id;
    uint16_t profile_version;
    uint8_t soc_estimate;
    uint8_t soc_display;
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
} bms_soc_diag_t;

struct SOC_CALCULATE_ELEMENT
{
    UINT32 u32CapFactory;         /* As*10 */
    UINT32 u32CapChange;          /* As*10 since the last integer SOC step */
    uint8_t u8CHG_AHCalcu_Flag;
    uint8_t u8DSG_AHCalcu_Flag;
    uint8_t u8SOC_Now;            /* estimated SOC, 0..100 */
    UINT32 u32CapNow;             /* As*10 */
    uint8_t u8DSG_SOC_Int;        /* equivalent-discharge percent accumulator */
    UINT32 u32Cycle_times;
    UINT32 u32CapFull;            /* As*10 */
    uint8_t u8SOC_Old;
    UINT32 u32CapFull_Cal_As;
    uint8_t soh;
};

extern struct SOC_CALCULATE_ELEMENT SOC_Calculate_Element;

void bms_soc_get_default_config(bms_soc_config_t *config);
uint8_t bms_soc_config_valid(const bms_soc_config_t *config);
uint8_t bms_soc_configure(const bms_soc_config_t *config);
uint8_t bms_soc_set_product_config(uint8_t chemistry, uint8_t profile_id);
uint8_t bms_soc_get_chemistry(void);
void bms_soc_get_diag(bms_soc_diag_t *diag);
void bms_soc_refresh_profile_from_params(void);

/* 400 ms = two nominal samples. Longer/unobserved intervals are not integrated
 * or counted as rest. SDK 32k clock wraps by unsigned subtraction. */
#define BMS_SOC_TIME_TICKS_PER_SECOND 32000u
#define BMS_SOC_MAX_SAMPLE_GAP_32K    12800u
void APP_SOC_IntEnhance_Ctrl(uint8_t valid, int32_t current_ma, uint32_t sample_tick_32k);
void SOC_Result_Pass(void);
void SOC_Cont_AH_Int_CHG(void);
void SOC_Cont_AH_Int_DSG(void);
void SOC_State_Transfer(void);
void set_soc_param(uint8_t soc, uint16_t cap_factory, uint8_t sync_display);
void set_calsoc(uint8_t soc);
void set_dispsoc(uint8_t soc);
uint8_t get_soc_real(void);
uint8_t isCHG(void);
uint8_t isDSG(void);
void soc_param_lib_init(const soc_kv_data_t *soc);
uint8_t bms_soh_from_cycle(uint16_t cycle);

void bms_soc_nominal_capacity_changed(void);

#endif /* SOCENHANCE_H */
