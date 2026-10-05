/*
 * 文件功能：BLE 名称后缀校验、存储和 Modbus 写入；构造广播名称并保持既有名称长度和字
 * 符约束。
 * bms/core/btname_modbus.c；实际编译归属见各产品 sources.txt。
 */
#include "bms_afe_backend.h"
#include "btname_modbus.h"
#include "bms_error.h"
#include "bms_config_store.h"
#include <string.h>
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"

/* Vendor common/string.h 提供 memcpy，但无有界字符串 API。 */
/* 在指定长度内比较名称字符串。 */
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

/* 在长度边界内复制 BLE 名称字符。 */
static char *m_strncpy(char *dst, const char *src, unsigned n)
{
    unsigned i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

static char g_name[BTNAME_TOTAL_MAX_LEN + 1] = BTNAME_PREFIX "DEFAULT";

extern u8 my_devName[BTNAME_TOTAL_MAX_LEN];

/* 将当前名称更新到 BLE 广播相关数据。 */
static void btname_ble_apply(const char *name)
{
    uint8_t scanrsp[31];
    uint8_t j = 0;
    uint8_t nlen = 0;

    bls_ll_setAdvEnable(0);

    while (nlen < BTNAME_TOTAL_MAX_LEN && name[nlen] != '\0') nlen++;

    scanrsp[j++] = (uint8_t)(1u + nlen);
    scanrsp[j++] = 0x09;
    memcpy(&scanrsp[j], name, nlen);
    j += nlen;

    bls_ll_setScanRspData(scanrsp, j);
    memcpy(my_devName, name, nlen);
    if (nlen < BTNAME_TOTAL_MAX_LEN) {
        my_devName[nlen] = '\0';
    }

    bls_ll_setAdvEnable(1);
}

/* 将产品前缀与合法后缀组合成完整 BLE 名称。 */
static void build_full_name_from_suffix(const char *suffix, char out[BTNAME_TOTAL_MAX_LEN + 1])
{
    uint8_t slen = 0;

    out[0] = 'B';
    out[1] = 'T';
    out[2] = '_';

    while (slen < BTNAME_SUFFIX_MAX_LEN && suffix[slen] != '\0') slen++;
    memcpy(out + BTNAME_PREFIX_LEN, suffix, slen);
    out[BTNAME_PREFIX_LEN + slen] = '\0';
}

/* 判断字符是否属于允许的名称后缀集合。 */
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

/* 在长度与字符约束内整理名称后缀。 */
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

/* 恢复产品默认 BLE 名称后缀。 */
static void btname_set_default_suffix(char suffix[BTNAME_SUFFIX_MAX_LEN + 1])
{
    m_strncpy(suffix, "DEFAULT", BTNAME_SUFFIX_MAX_LEN);
    suffix[BTNAME_SUFFIX_MAX_LEN] = '\0';
}

/* 加载持久化 BLE 名称后缀。 */
static int btname_load_suffix_from_store(char suffix[BTNAME_SUFFIX_MAX_LEN + 1])
{
    if (!bms_config_store_get_bt_name_suffix(suffix, BTNAME_SUFFIX_MAX_LEN + 1u)) {
        return 0;
    }

    sanitize_suffix(suffix);
    return (suffix[0] != '\0');
}

/* 保存经过校验的 BLE 名称后缀。 */
static int btname_save_suffix_to_store(const char *suffix)
{
    return bms_config_store_set_bt_name_suffix(suffix);
}

/* 加载名称后缀并构造应用 BLE 名称。 */
void btname_init(void)
{
    char suffix[BTNAME_SUFFIX_MAX_LEN + 1];

    if (!btname_load_suffix_from_store(suffix)) {
        btname_set_default_suffix(suffix);
    }

    build_full_name_from_suffix(suffix, g_name);
    btname_ble_apply(g_name);
}

/* 取得当前 BLE 名称及其长度信息。 */
const char *btname_get(void)
{
    return g_name;
}

/* 校验名称寄存器写入并保存、更新广播名称。 */
int btname_modbus_on_write_holding(uint16_t addr, uint16_t qty, const uint16_t *regs)
{
    const uint8_t *bytes = (const uint8_t *)regs;
    uint16_t byte_len = (uint16_t)(qty * 2u);
    char suffix[BTNAME_SUFFIX_MAX_LEN + 1];
    char new_full[BTNAME_TOTAL_MAX_LEN + 1];
    uint16_t bi = 0;
    uint16_t i;

    /* 保留历史空写入返回结果，存储失败始终报告失败。 */
    const int empty_result = (BMS_AFE_BACKEND == BMS_AFE_BACKEND_SH3673510) ? 1 : 0;
    (void)addr;

    if ((qty == 0u) || (regs == 0)) {
        return empty_result;
    }

    for (i = 0; i < byte_len && bi < BTNAME_SUFFIX_MAX_LEN; i++) {
        uint8_t c = bytes[i];
        if (c == 0u) break;
        suffix[bi++] = (char)c;
    }
    suffix[bi] = '\0';

    sanitize_suffix(suffix);
    if (suffix[0] == '\0') {
        return empty_result;
    }

    build_full_name_from_suffix(suffix, new_full);
    if (m_strncmp(new_full, g_name, BTNAME_TOTAL_MAX_LEN) == 0) {
        return 1;
    }

    if (!btname_save_suffix_to_store(suffix)) {
        bms_error_raise(BMS_ERROR_EEPROM_STORE);
        return 0;
    }

    m_strncpy(g_name, new_full, BTNAME_TOTAL_MAX_LEN);
    g_name[BTNAME_TOTAL_MAX_LEN] = '\0';
    btname_ble_apply(g_name);
    return 1;
}
