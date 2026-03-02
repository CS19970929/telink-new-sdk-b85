#include "btname_modbus.h"
#include "flash_store_cfg.h"

/* �㹤����� Telink SDK ͷ�ļ�������ʵ�� SDK ���ܲ�ͬ�� */
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "sh367309_datadeal.h"

/* =================== �Լ�ʵ����С���������ױܿ� libc�� =================== */
static void *m_memcpy(void *dst, const void *src, unsigned n)
{
    unsigned char *d = (unsigned char*)dst;
    const unsigned char *s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}
static void *m_memset(void *dst, int v, unsigned n)
{
    unsigned char *d = (unsigned char*)dst;
    while (n--) *d++ = (unsigned char)v;
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

/* ================== �ڲ��洢��ʽ��һ�� sector ֻ�� suffix ================== */
#define BTNAME_MAGIC  0x53465831u  /* 'SFX1' */

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  len;                               /* 1..BTNAME_SUFFIX_MAX_LEN */
    uint8_t  cksum;                             /* checksum8 */
    uint8_t  suffix[BTNAME_SUFFIX_MAX_LEN];     /* raw bytes */
} btname_rec_t;

/* ================== RAM ״̬���������� BT_ + suffix ================== */
static char g_name[BTNAME_TOTAL_MAX_LEN + 1] = BTNAME_PREFIX "DEFAULT";

/* ================== checksum8������ɿ��� ================== */
static uint8_t checksum8(const uint8_t *p, uint8_t n)
{
    uint8_t s = 0;
    for (uint8_t i = 0; i < n; i++) s ^= p[i];
    return (uint8_t)(s ^ 0xA5u);
}

/* ================== Flash ��װ��Telink��д/������жϣ� ================== */
static void flash_read_bytes(uint32_t addr, uint8_t *buf, uint32_t len)
{
    flash_read_page(addr, len, buf);
}
static void flash_erase_sector_safe(uint32_t addr)
{
    flash_erase_sector(addr);
}
static void flash_write_bytes_safe(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    flash_write_page(addr, len, (unsigned char*)buf);
}

/* ================== BLE Ӧ�����֣�ֻ�� ScanRsp �� Complete Local Name(0x09) ==================
 * ע�⣺����㹤�� scanRsp ��Ҫ���������ֶΣ��������ݵȣ�����������滻Ϊ�������Լ���ƴ��������
 */
extern u8 my_devName[BTNAME_TOTAL_MAX_LEN] ;
static void btname_ble_apply(const char *name)
{
    /* ĳЩ SDK û����� API������ undefined���Ͱ� enable �������ɾ�� */
    bls_ll_setAdvEnable(0);

    uint8_t scanrsp[31];
    uint8_t j = 0;

    uint8_t nlen = 0;
    while (nlen < BTNAME_TOTAL_MAX_LEN && name[nlen] != '\0') nlen++;

    scanrsp[j++] = (uint8_t)(1u + nlen);
    scanrsp[j++] = 0x09; /* Complete Local Name */
    m_memcpy(&scanrsp[j], name, nlen);
    j += nlen;

    bls_ll_setScanRspData(scanrsp, j);
    m_memcpy(my_devName, name, nlen);

    bls_ll_setAdvEnable(1);
}

/* ================== ��װ�������֣�BT_ + suffix ================== */
static void build_full_name_from_suffix(const char *suffix, char out[BTNAME_TOTAL_MAX_LEN + 1])
{
    out[0] = 'B';
    out[1] = 'T';
    out[2] = '_';

    uint8_t slen = 0;
    while (slen < BTNAME_SUFFIX_MAX_LEN && suffix[slen] != '\0') slen++;

    m_memcpy(out + BTNAME_PREFIX_LEN, suffix, slen);
    out[BTNAME_PREFIX_LEN + slen] = '\0';
}

/* ================== ��׺�ַ����� ================== */
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
    for (uint8_t r = 0; r < BTNAME_SUFFIX_MAX_LEN; r++) {
        unsigned char c = (unsigned char)s[r];
        if (c == 0) break;
        if (!is_allowed_suffix_char(c)) continue;
        s[w++] = (char)c;
    }
    s[w] = '\0';
    return w;
}

/* ================== Flash <-> suffix ================== */
static int suffix_load_from_flash(char out_suffix[BTNAME_SUFFIX_MAX_LEN + 1])
{
    btname_rec_t rec;
    m_memset(&rec, 0, sizeof(rec));
    flash_read_bytes(BTNAME_SECTOR_ADDR, (uint8_t*)&rec, sizeof(rec));

    if (rec.magic != BTNAME_MAGIC) return 0;
    if (rec.len == 0 || rec.len > BTNAME_SUFFIX_MAX_LEN) return 0;
    if (rec.cksum != checksum8(rec.suffix, rec.len)) return 0;

    m_memcpy(out_suffix, rec.suffix, rec.len);
    out_suffix[rec.len] = '\0';
    return 1;
}

static int suffix_save_to_flash(const char *suffix)
{
    uint8_t len = 0;
    while (len < BTNAME_SUFFIX_MAX_LEN && suffix[len] != '\0') len++;
    if (len == 0) return 0;

    btname_rec_t rec;
    m_memset(&rec, 0xFF, sizeof(rec));
    rec.magic = BTNAME_MAGIC;
    rec.len   = len;
    m_memcpy(rec.suffix, suffix, len);
    rec.cksum = checksum8(rec.suffix, rec.len);

    flash_erase_sector_safe(BTNAME_SECTOR_ADDR);
    flash_write_bytes_safe(BTNAME_SECTOR_ADDR, (const uint8_t*)&rec, sizeof(rec));

    /* �ض�У�� */
    btname_rec_t chk;
    m_memset(&chk, 0, sizeof(chk));
    //todo
    flash_read_bytes(BTNAME_SECTOR_ADDR, (uint8_t*)&chk, sizeof(chk));

    if (chk.magic != BTNAME_MAGIC) return 0;
    if (chk.len != rec.len) return 0;
    if (chk.cksum != rec.cksum) return 0;
    for (uint8_t i = 0; i < rec.len; i++) {
        if (chk.suffix[i] != rec.suffix[i]) return 0;
    }
    return 1;
}

/* ================== API ================== */
void btname_init(void)
{
    char suffix[BTNAME_SUFFIX_MAX_LEN + 1];

    if (suffix_load_from_flash(suffix)) {
        sanitize_suffix(suffix);
        if (suffix[0] == '\0') {
            m_strncpy(suffix, "DEFAULT", BTNAME_SUFFIX_MAX_LEN);
            suffix[BTNAME_SUFFIX_MAX_LEN] = '\0';
        }
    } else {
        m_strncpy(suffix, "DEFAULT", BTNAME_SUFFIX_MAX_LEN);
        suffix[BTNAME_SUFFIX_MAX_LEN] = '\0';
    }

    build_full_name_from_suffix(suffix, g_name);
    btname_ble_apply(g_name);
}

const char* btname_get(void)
{
    return g_name;
}

#if 0
int btname_modbus_on_write_holding(uint16_t addr, uint16_t qty, const uint16_t *regs)
{
    // if (addr != (uint16_t)BTNAME_REG_BASE) return 0;
    // if (!regs) return 1;

    // if (qty == 0 || qty > (uint16_t)BTNAME_REG_WORDS) return 1;

    /* regs(���) -> suffix bytes */
    char suffix[BTNAME_SUFFIX_MAX_LEN + 1];
    m_memset(suffix, 0, sizeof(suffix));

    uint16_t bi = 0;
    for (uint16_t i = 0; i < qty && bi < BTNAME_SUFFIX_MAX_LEN; i++) {
            System_ERROR_UserCallback(ERROR_AFE2);
        uint16_t v = regs[i];
        uint8_t hi = (uint8_t)((v >> 8) & 0xFF);
        uint8_t lo = (uint8_t)(v & 0xFF);

        if (hi == 0) break;
        suffix[bi++] = (char)hi;
        if (bi >= BTNAME_SUFFIX_MAX_LEN) break;

        if (lo == 0) break;
        suffix[bi++] = (char)lo;
    }
    suffix[BTNAME_SUFFIX_MAX_LEN] = '\0';

    sanitize_suffix(suffix);
    if (suffix[0] == '\0') return 1;

    char new_full[BTNAME_TOTAL_MAX_LEN + 1];
    build_full_name_from_suffix(suffix, new_full);

    if (m_strncmp(new_full, g_name, BTNAME_TOTAL_MAX_LEN) == 0) return 1;

    if (!suffix_save_to_flash(suffix)) 
    {
        System_ERROR_UserCallback(ERROR_CAN);
        return 1;
    }

    m_strncpy(g_name, new_full, BTNAME_TOTAL_MAX_LEN);
    g_name[BTNAME_TOTAL_MAX_LEN] = '\0';
    btname_ble_apply(g_name);

    return 1;
}
#endif

int btname_modbus_on_write_holding(uint16_t addr, uint16_t qty, const uint16_t *regs)
{
    // �� regs �����ֽ�����
    const uint8_t *bytes = (const uint8_t *)regs;
    uint16_t byte_len = qty * 2;  // ���ֽ���

    char suffix[BTNAME_SUFFIX_MAX_LEN + 1];
    uint16_t bi = 0;
    for (uint16_t i = 0; i < byte_len && bi < BTNAME_SUFFIX_MAX_LEN; i++) {
        uint8_t c = bytes[i];
        if (c == 0) break;          // ���� 0 ��ֹ
        suffix[bi++] = (char)c;
    }
    suffix[bi] = '\0';

    sanitize_suffix(suffix);
    if (suffix[0] == '\0') return 1;

    char new_full[BTNAME_TOTAL_MAX_LEN + 1];
    build_full_name_from_suffix(suffix, new_full);

    if (m_strncmp(new_full, g_name, BTNAME_TOTAL_MAX_LEN) == 0) return 1;

    if (!suffix_save_to_flash(suffix)) {
        System_ERROR_UserCallback(ERROR_CAN);
        return 1;
    }

    m_strncpy(g_name, new_full, BTNAME_TOTAL_MAX_LEN);
    g_name[BTNAME_TOTAL_MAX_LEN] = '\0';
    btname_ble_apply(g_name);
    return 1;
}
