#include "btname_modbus.h"

#include "bms_cold_kv_store.h"
#include "bms_error.h"
#include "stack/ble/ble.h"
#include "tl_common.h"
#include <string.h>

static char s_name[BTNAME_TOTAL_MAX_LEN + 1] = BTNAME_PREFIX "DEFAULT";

extern u8 my_devName[BTNAME_TOTAL_MAX_LEN];

static void btname_ble_apply(const char *name)
{
    uint8_t scan_rsp[31] = {0};
    uint8_t index = 0u;
    uint8_t name_len = 0u;

    while ((name_len < BTNAME_TOTAL_MAX_LEN) && (name[name_len] != '\0'))
    {
        ++name_len;
    }

    scan_rsp[index++] = (uint8_t)(name_len + 1u);
    scan_rsp[index++] = 0x09u;
    memcpy(&scan_rsp[index], name, name_len);
    index = (uint8_t)(index + name_len);

    bls_ll_setAdvEnable(BLC_ADV_DISABLE);
    bls_ll_setScanRspData(scan_rsp, index);

    memset(my_devName, 0, BTNAME_TOTAL_MAX_LEN);
    memcpy(my_devName, name, name_len);
    bls_ll_setAdvEnable(BLC_ADV_ENABLE);
}

static void btname_build_full_name(const char *suffix,
                                   char out[BTNAME_TOTAL_MAX_LEN + 1])
{
    uint8_t suffix_len = 0u;

    memcpy(out, BTNAME_PREFIX, BTNAME_PREFIX_LEN);
    while ((suffix_len < BTNAME_SUFFIX_MAX_LEN) && (suffix[suffix_len] != '\0'))
    {
        ++suffix_len;
    }
    memcpy(out + BTNAME_PREFIX_LEN, suffix, suffix_len);
    out[BTNAME_PREFIX_LEN + suffix_len] = '\0';
}

static int btname_suffix_char_allowed(unsigned char c)
{
#if (BTNAME_SUFFIX_STRICT)
    if ((c >= '0') && (c <= '9')) return 1;
    if ((c >= 'A') && (c <= 'Z')) return 1;
    if ((c >= 'a') && (c <= 'z')) return 1;
    if ((c == '_') || (c == '-')) return 1;
    return 0;
#else
    return ((c >= 0x20u) && (c <= 0x7Eu));
#endif
}

static uint8_t btname_sanitize_suffix(char *suffix)
{
    uint8_t read_index;
    uint8_t write_index = 0u;

    for (read_index = 0u; read_index < BTNAME_SUFFIX_MAX_LEN; ++read_index)
    {
        unsigned char c = (unsigned char)suffix[read_index];
        if (c == 0u) break;
        if (!btname_suffix_char_allowed(c)) continue;
        suffix[write_index++] = (char)c;
    }
    suffix[write_index] = '\0';
    return write_index;
}

static void btname_default_suffix(char suffix[BTNAME_SUFFIX_MAX_LEN + 1])
{
    memset(suffix, 0, BTNAME_SUFFIX_MAX_LEN + 1u);
    strncpy(suffix, "DEFAULT", BTNAME_SUFFIX_MAX_LEN);
}

static int btname_load_suffix(char suffix[BTNAME_SUFFIX_MAX_LEN + 1])
{
    if (!bms_cold_kv_store_get_bt_name_suffix(suffix,
                                               BTNAME_SUFFIX_MAX_LEN + 1u))
    {
        return 0;
    }

    btname_sanitize_suffix(suffix);
    return suffix[0] != '\0';
}

void btname_init(void)
{
    char suffix[BTNAME_SUFFIX_MAX_LEN + 1];

    if (!btname_load_suffix(suffix))
    {
        btname_default_suffix(suffix);
    }

    btname_build_full_name(suffix, s_name);
    btname_ble_apply(s_name);
}

const char *btname_get(void)
{
    return s_name;
}

int btname_modbus_on_write_holding(uint16_t addr,
                                   uint16_t qty,
                                   const uint16_t *regs)
{
    const uint8_t *bytes = (const uint8_t *)regs;
    uint16_t byte_len = (uint16_t)(qty * 2u);
    char suffix[BTNAME_SUFFIX_MAX_LEN + 1];
    char new_name[BTNAME_TOTAL_MAX_LEN + 1];
    uint16_t byte_index = 0u;
    uint16_t index;

    (void)addr;
    if ((qty == 0u) || (regs == NULL))
    {
        return 1;
    }

    for (index = 0u;
         (index < byte_len) && (byte_index < BTNAME_SUFFIX_MAX_LEN);
         ++index)
    {
        uint8_t c = bytes[index];
        if (c == 0u) break;
        suffix[byte_index++] = (char)c;
    }
    suffix[byte_index] = '\0';

    btname_sanitize_suffix(suffix);
    if (suffix[0] == '\0')
    {
        return 1;
    }

    btname_build_full_name(suffix, new_name);
    if (strncmp(new_name, s_name, BTNAME_TOTAL_MAX_LEN) == 0)
    {
        return 1;
    }

    if (!bms_cold_kv_store_set_bt_name_suffix(suffix))
    {
        bms_error_note_store_failure();
        return 1;
    }

    memset(s_name, 0, sizeof(s_name));
    strncpy(s_name, new_name, BTNAME_TOTAL_MAX_LEN);
    btname_ble_apply(s_name);
    return 1;
}
