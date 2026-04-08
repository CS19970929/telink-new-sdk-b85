#include "btname_modbus.h"
#include "bms_cold_kv_store.h"
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "sh367309_datadeal.h"

static void *m_memcpy(void *dst, const void *src, unsigned n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

static int m_strncmp(const char *a, const char *b, unsigned n)
{
    while (n--) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca != cb) return (ca < cb) ? -1 : 1;
        if (ca == 0) return 0;
    }
    return 0;
}

static char *m_strncpy(char *dst, const char *src, unsigned n)
{
    unsigned i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

static char g_name[BTNAME_TOTAL_MAX_LEN + 1] = BTNAME_PREFIX "DEFAULT";

extern u8 my_devName[BTNAME_TOTAL_MAX_LEN];

static void btname_ble_apply(const char *name)
{
    uint8_t scanrsp[31];
    uint8_t j = 0;
    uint8_t nlen = 0;

    bls_ll_setAdvEnable(0);

    while (nlen < BTNAME_TOTAL_MAX_LEN && name[nlen] != '\0') nlen++;

    scanrsp[j++] = (uint8_t)(1u + nlen);
    scanrsp[j++] = 0x09;
    m_memcpy(&scanrsp[j], name, nlen);
    j += nlen;

    bls_ll_setScanRspData(scanrsp, j);
    m_memcpy(my_devName, name, nlen);
    if (nlen < BTNAME_TOTAL_MAX_LEN) {
        my_devName[nlen] = '\0';
    }

    bls_ll_setAdvEnable(1);
}

static void build_full_name_from_suffix(const char *suffix, char out[BTNAME_TOTAL_MAX_LEN + 1])
{
    uint8_t slen = 0;

    out[0] = 'B';
    out[1] = 'T';
    out[2] = '_';

    while (slen < BTNAME_SUFFIX_MAX_LEN && suffix[slen] != '\0') slen++;
    m_memcpy(out + BTNAME_PREFIX_LEN, suffix, slen);
    out[BTNAME_PREFIX_LEN + slen] = '\0';
}

static int is_allowed_suffix_char(unsigned char c)
{
#if (BTNAME_SUFFIX_STRICT)
    if (c >= '0' && c <= '9') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= 'a' && c <= 'z') return 1;
    if (c == '_' || c == '-') return 1;
    return 0;
#else
    return (c >= 0x20 && c <= 0x7E);
#endif
}

static uint8_t sanitize_suffix(char *s)
{
    uint8_t w = 0;
    uint8_t r;

    for (r = 0; r < BTNAME_SUFFIX_MAX_LEN; r++) {
        unsigned char c = (unsigned char)s[r];
        if (c == 0) break;
        if (!is_allowed_suffix_char(c)) continue;
        s[w++] = (char)c;
    }
    s[w] = '\0';
    return w;
}

static void btname_set_default_suffix(char suffix[BTNAME_SUFFIX_MAX_LEN + 1])
{
    m_strncpy(suffix, "DEFAULT", BTNAME_SUFFIX_MAX_LEN);
    suffix[BTNAME_SUFFIX_MAX_LEN] = '\0';
}

static int btname_load_suffix_from_store(char suffix[BTNAME_SUFFIX_MAX_LEN + 1])
{
    if (!bms_cold_kv_store_get_bt_name_suffix(suffix, BTNAME_SUFFIX_MAX_LEN + 1u)) {
        return 0;
    }

    sanitize_suffix(suffix);
    return (suffix[0] != '\0');
}

static int btname_save_suffix_to_store(const char *suffix)
{
    return bms_cold_kv_store_set_bt_name_suffix(suffix);
}

void btname_init(void)
{
    char suffix[BTNAME_SUFFIX_MAX_LEN + 1];

    if (!btname_load_suffix_from_store(suffix)) {
        btname_set_default_suffix(suffix);
    }

    build_full_name_from_suffix(suffix, g_name);
    btname_ble_apply(g_name);
}

const char *btname_get(void)
{
    return g_name;
}

int btname_modbus_on_write_holding(uint16_t addr, uint16_t qty, const uint16_t *regs)
{
    const uint8_t *bytes = (const uint8_t *)regs;
    uint16_t byte_len = (uint16_t)(qty * 2u);
    char suffix[BTNAME_SUFFIX_MAX_LEN + 1];
    char new_full[BTNAME_TOTAL_MAX_LEN + 1];
    uint16_t bi = 0;
    uint16_t i;

    (void)addr;

    if ((qty == 0u) || (regs == 0)) {
        return 1;
    }

    for (i = 0; i < byte_len && bi < BTNAME_SUFFIX_MAX_LEN; i++) {
        uint8_t c = bytes[i];
        if (c == 0u) break;
        suffix[bi++] = (char)c;
    }
    suffix[bi] = '\0';

    sanitize_suffix(suffix);
    if (suffix[0] == '\0') {
        return 1;
    }

    build_full_name_from_suffix(suffix, new_full);
    if (m_strncmp(new_full, g_name, BTNAME_TOTAL_MAX_LEN) == 0) {
        return 1;
    }

    if (!btname_save_suffix_to_store(suffix)) {
        System_ERROR_UserCallback(ERROR_EEPROM_STORE);
        return 1;
    }

    m_strncpy(g_name, new_full, BTNAME_TOTAL_MAX_LEN);
    g_name[BTNAME_TOTAL_MAX_LEN] = '\0';
    btname_ble_apply(g_name);
    return 1;
}
