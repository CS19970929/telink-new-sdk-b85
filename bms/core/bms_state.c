#include "bms_state.h"
#include "bms_error.h"
#include <stddef.h>

volatile bms_system_status_t g_bms_system_status;
struct stCell_Info g_stCellInfoReport;

static volatile uint8_t s_error_count[BMS_ERROR_COUNT];
static uint8_t s_fault_write_index[3];
static uint8_t s_fault_history[3][BMS_FAULT_HISTORY_DEPTH];

void bms_error_raise(bms_error_id_t error)
{
    if ((uint8_t)error >= (uint8_t)BMS_ERROR_COUNT) return;

    if (error == BMS_ERROR_TEMP_BREAK)
    {
        s_error_count[error] = 1u;
    }
    else if (s_error_count[error] != UINT8_MAX)
    {
        ++s_error_count[error];
    }
}

void bms_error_clear(bms_error_id_t error)
{
    if ((uint8_t)error < (uint8_t)BMS_ERROR_COUNT)
    {
        s_error_count[error] = 0u;
    }
}

uint8_t bms_error_get(bms_error_id_t error)
{
    if ((uint8_t)error >= (uint8_t)BMS_ERROR_COUNT) return 0u;
    return s_error_count[error];
}

void bms_fault_history_record(bms_fault_code_t fault)
{
    uint8_t code = (uint8_t)fault;
    uint8_t level;
    uint8_t index;

    if (code < (uint8_t)BMS_FAULT_CELL_OVP_FIRST ||
        code > (uint8_t)BMS_FAULT_SOC_HIGH_THIRD)
    {
        return;
    }

    level = (uint8_t)((code - 1u) / 13u);
    index = s_fault_write_index[level];
    s_fault_history[level][index] = code;
    s_fault_write_index[level] = (uint8_t)((index + 1u) % BMS_FAULT_HISTORY_DEPTH);
}

uint8_t bms_fault_history_recent(bms_fault_level_t level, uint8_t age)
{
    uint8_t level_index;
    uint8_t record_index;

    if ((uint8_t)level < (uint8_t)BMS_FAULT_LEVEL_FIRST ||
        (uint8_t)level > (uint8_t)BMS_FAULT_LEVEL_THIRD ||
        age >= BMS_FAULT_HISTORY_DEPTH)
    {
        return 0u;
    }

    level_index = (uint8_t)((uint8_t)level - 1u);
    record_index = (uint8_t)((s_fault_write_index[level_index] +
                              BMS_FAULT_HISTORY_DEPTH - 1u - age) %
                             BMS_FAULT_HISTORY_DEPTH);
    return s_fault_history[level_index][record_index];
}

uint16_t bms_lookup_u16(const uint16_t *table, uint16_t table_size, uint16_t input)
{
    uint16_t i;
    uint32_t x1 = 0u;
    uint32_t y1 = 0u;
    uint32_t x2 = 1u;
    uint32_t y2 = 1u;
    uint16_t endpoint;

    if (table == NULL || table_size < 4u || (table_size & 1u) != 0u) return 0u;

    for (i = 0u; i < table_size - 2u; i = (uint16_t)(i + 2u))
    {
        uint16_t first = table[i];
        uint16_t second = table[i + 2u];

        if (((input >= first) && (input <= second)) ||
            ((input <= first) && (input >= second)))
        {
            x1 = first;
            x2 = second;
            y1 = table[i + 1u];
            y2 = table[i + 3u];
            break;
        }
    }

    if (i >= table_size - 2u)
    {
        uint16_t first = table[0];
        uint16_t last = table[table_size - 2u];

        if (first <= last)
        {
            endpoint = (input >= last) ? table[table_size - 1u] : table[1];
        }
        else
        {
            endpoint = (input >= first) ? table[1] : table[table_size - 1u];
        }
        return endpoint;
    }

    if (x2 < x1)
    {
        uint32_t swap = x2;
        x2 = x1;
        x1 = swap;
        swap = y2;
        y2 = y1;
        y1 = swap;
    }

    if (y2 >= y1)
    {
        int32_t intercept = (int32_t)(y1 * x2) - (int32_t)(y2 * x1);
        int32_t result = (int32_t)(input * (y2 - y1)) + intercept;
        return (uint16_t)(result / (int32_t)(x2 - x1));
    }
    else
    {
        uint32_t intercept = (y1 * x2) - (y2 * x1);
        uint32_t result = intercept - (input * (y1 - y2));
        return (uint16_t)(result / (x2 - x1));
    }
}
