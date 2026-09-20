#ifndef SOCENHANCE_H
#define SOCENHANCE_H

#include "conf.h"
#include "soc_kv_store.h"
#include "bms_soc_defs.h"
#include "bms_diag.h"

#define BMS_SOC_OCV_WAIT_CURRENT   0u
#define BMS_SOC_OCV_PREPARE        1u
#define BMS_SOC_OCV_READY          2u
#define BMS_SOC_OCV_CORRECT_DOWN   3u

#define BMS_SOC_LEARNING_NONE          0u
#define BMS_SOC_LEARNING_EMPTY_TO_FULL 1u
#define BMS_SOC_LEARNING_FULL_TO_EMPTY 2u

#define BMS_SOC_ENDPOINT_NORMAL          0u
#define BMS_SOC_ENDPOINT_FULL_APPROACH   1u
#define BMS_SOC_ENDPOINT_CONFIRMED_FULL  2u
#define BMS_SOC_ENDPOINT_EMPTY_APPROACH  3u
#define BMS_SOC_ENDPOINT_CONFIRMED_EMPTY 4u

#define BMS_SOC_ETA_INVALID        0u
#define BMS_SOC_ETA_STABILIZING    1u
#define BMS_SOC_ETA_VALID          2u
#define BMS_SOC_ETA_LOW_CONFIDENCE 3u
#define BMS_SOC_ETA_DIR_NONE       0u
#define BMS_SOC_ETA_DIR_CHARGE     1u
#define BMS_SOC_ETA_DIR_DISCHARGE  2u
#define BMS_SOC_ETA_MINUTES_INVALID 0xFFFFu

#define BMS_SOC_SOH_SOURCE_ESTIMATED_CYCLE 1u
#define BMS_SOC_SOH_SOURCE_CAPACITY       2u

#define BMS_SOC_LEARNING_REJECT_NONE                   0u
#define BMS_SOC_LEARNING_REJECT_INVALID_SAMPLE         1u
#define BMS_SOC_LEARNING_REJECT_SAMPLE_GAP             2u
#define BMS_SOC_LEARNING_REJECT_REBOOT                  3u
#define BMS_SOC_LEARNING_REJECT_DIRECTION_REVERSE       4u
#define BMS_SOC_LEARNING_REJECT_OPEN_WIRE               5u
#define BMS_SOC_LEARNING_REJECT_CELL_IMBALANCE          6u
#define BMS_SOC_LEARNING_REJECT_TEMPERATURE              7u
#define BMS_SOC_LEARNING_REJECT_PROTECTION               8u
#define BMS_SOC_LEARNING_REJECT_LOW_QUALITY_EMPTY        9u
#define BMS_SOC_LEARNING_REJECT_LOW_QUALITY_FULL        10u
#define BMS_SOC_LEARNING_REJECT_CAPACITY_RANGE          11u
#define BMS_SOC_LEARNING_REJECT_CANDIDATE_INCONSISTENT  12u
#define BMS_SOC_LEARNING_REJECT_AFE_COMMUNICATION       13u
#define BMS_SOC_LEARNING_REJECT_CALIBRATION_CHANGED     14u
#define BMS_SOC_LEARNING_REJECT_BALANCING               15u
#define BMS_SOC_LEARNING_REJECT_HEATING                 16u
#define BMS_SOC_LEARNING_REJECT_CHARGER_CHANGE          17u
#define BMS_SOC_LEARNING_REJECT_LOAD_CHANGE             18u

/* Hardware-neutral SOC input. Units are part of the ABI: voltage in mV,
 * current in mA, temperature in the existing (degC + 40) * 10 encoding and
 * time in the SDK 32 kHz domain. AFE adapters populate this structure; the
 * SOC core never needs DVC/SH registers or transport details. */
typedef struct
{
    uint32_t timestamp_32k;
    uint32_t pack_voltage_mv;
    int32_t current_ma;             /* negative=charge, positive=discharge */
    uint16_t cell_min_mv;
    uint16_t cell_max_mv;
    uint16_t cell_delta_mv;
    uint16_t temperature_min_x10;
    uint16_t temperature_max_x10;
    uint8_t sample_valid;
    uint8_t voltage_valid;
    uint8_t temperature_valid;
    uint8_t balancing_active;
    uint8_t heating_active;
    uint8_t open_wire_active;
    uint8_t open_wire_suspected;
    uint8_t afe_fault;
    uint8_t temperature_fault;
    uint8_t current_fault;
    uint8_t pack_fault;
    uint8_t third_cell_ovp;
    uint8_t third_cell_uvp;
    uint8_t charger_state_known;
    uint8_t charger_present;
    uint8_t load_state_known;
    uint8_t load_present;
} bms_soc_sample_t;

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
void bms_soc_process_sample(const bms_soc_sample_t *sample);
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
