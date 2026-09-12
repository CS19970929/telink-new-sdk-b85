#ifndef SOC_ENHANCE_H_
#define SOC_ENHANCE_H_

#include <stdint.h>
#include "soc_kv_store.h"

/*
 * Internal SOC accumulator state that is still referenced by the legacy
 * communication map. Keep field layout stable until register mapping is
 * decoupled from this structure.
 */
struct SOC_CALCULATE_ELEMENT
{
    uint32_t u32CapFactory;        /* factory capacity, As * 10 */
    uint32_t u32CapChange;         /* integration accumulator, As * 10 */
    uint8_t u8CHG_AHCalcu_Flag;
    uint8_t u8DSG_AHCalcu_Flag;

    uint8_t u8SOC_Now;             /* real SOC, 0..100 */
    uint32_t u32CapNow;            /* remaining capacity, As * 10 */
    uint8_t u8DSG_SOC_Int;
    uint32_t u32Cycle_times;
    uint32_t u32CapFull;           /* full capacity, As * 10 */

    uint8_t u8SOC_Old;
    uint32_t u32CapFull_Cal_As;
    uint8_t soh;
};

extern struct SOC_CALCULATE_ELEMENT SOC_Calculate_Element;

void APP_SOC_IntEnhance_Ctrl(void);
void set_soc_param(uint8_t soc, uint16_t capacity_factory, uint8_t sync_display);
void soc_param_lib_init(const soc_kv_data_t *soc);

#endif /* SOC_ENHANCE_H_ */
