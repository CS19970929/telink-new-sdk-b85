#include "bms_cold_kv_store.h"
#include "flash_store_cfg.h"
#include "flash_store_safe.h"
#include <string.h>

#define BMS_COLD_PROTECT_KEY_BASE  0x1000u
#define BMS_COLD_SYSTEM_KEY_BASE   0x2000u
#define BMS_COLD_CTRL_KEY_BASE     0x3000u
#define BMS_COLD_SECTOR_MAGIC      0x324B5646u

typedef struct {
    u32 key;
    u16 offset;
} bms_cold_field_desc_t;

/*
 * tc32 SDK 在 common/types.h 里自定义了 size_t，直接包含 stddef.h 会冲突，
 * 这里改用本地 offsetof 宏，避免把 wrapper 绑定到标准头实现细节。
 */
#define BMS_COLD_OFFSETOF(type, field)  ((u16)((u32)&(((type *)0)->field)))

#define BMS_COLD_PROTECT_FIELD_LIST(X) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x01u, u16VcellOvp_First) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x02u, u16VcellOvp_Second) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x03u, u16VcellOvp_Third) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x04u, u16VcellOvp_Rcv) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x05u, u16VcellOvp_Filter) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x06u, u16VcellUvp_First) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x07u, u16VcellUvp_Second) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x08u, u16VcellUvp_Third) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x09u, u16VcellUvp_Rcv) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x0Au, u16VcellUvp_Filter) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x0Bu, u16VbusOvp_First) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x0Cu, u16VbusOvp_Second) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x0Du, u16VbusOvp_Third) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x0Eu, u16VbusOvp_Rcv) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x0Fu, u16VbusOvp_Filter) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x10u, u16VbusUvp_First) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x11u, u16VbusUvp_Second) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x12u, u16VbusUvp_Third) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x13u, u16VbusUvp_Rcv) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x14u, u16VbusUvp_Filter) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x15u, u16IchgOcp_First) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x16u, u16IchgOcp_Second) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x17u, u16IchgOcp_Third) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x18u, u16IchgOcp_Rcv) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x19u, u16IchgOcp_Filter) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x1Au, u16IdsgOcp_First) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x1Bu, u16IdsgOcp_Second) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x1Cu, u16IdsgOcp_Third) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x1Du, u16IdsgOcp_Rcv) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x1Eu, u16IdsgOcp_Filter) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x1Fu, u16TChgOTp_First) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x20u, u16TChgOTp_Second) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x21u, u16TChgOTp_Third) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x22u, u16TChgOTp_Rcv) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x23u, u16TChgOTp_Filter) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x24u, u16TchgUTp_First) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x25u, u16TchgUTp_Second) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x26u, u16TchgUTp_Third) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x27u, u16TchgUTp_Rcv) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x28u, u16TchgUTp_Filter) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x29u, u16TdischgOTp_First) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x2Au, u16TdischgOTp_Second) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x2Bu, u16TdischgOTp_Third) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x2Cu, u16TdischgOTp_Rcv) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x2Du, u16TdischgOTp_Filter) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x2Eu, u16TdischgUTp_First) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x2Fu, u16TdischgUTp_Second) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x30u, u16TdischgUTp_Third) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x31u, u16TdischgUTp_Rcv) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x32u, u16TdischgUTp_Filter) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x33u, u16TmosOTp_First) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x34u, u16TmosOTp_Second) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x35u, u16TmosOTp_Third) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x36u, u16TmosOTp_Rcv) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x37u, u16TmosOTp_Filter) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x38u, u16VdeltaOvp_First) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x39u, u16VdeltaOvp_Second) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x3Au, u16VdeltaOvp_Third) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x3Bu, u16VdeltaOvp_Rcv) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x3Cu, u16VdeltaOvp_Filter) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x3Du, u16SocUp_First) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x3Eu, u16SocUp_Second) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x3Fu, u16SocUp_Third) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x40u, u16SocUp_Rcv) \
    X(BMS_COLD_PROTECT_KEY_BASE + 0x41u, u16SocUp_Filter)

#define BMS_COLD_SYSTEM_FIELD_LIST(X) \
    X(BMS_COLD_SYSTEM_KEY_BASE + 0x01u, bms_type) \
    X(BMS_COLD_SYSTEM_KEY_BASE + 0x02u, series_num) \
    X(BMS_COLD_SYSTEM_KEY_BASE + 0x03u, capacity_factory) \
    X(BMS_COLD_SYSTEM_KEY_BASE + 0x04u, afe_odc2) \
    X(BMS_COLD_SYSTEM_KEY_BASE + 0x05u, fac_init_soc) \
    X(BMS_COLD_SYSTEM_KEY_BASE + 0x06u, init_soc) \
    X(BMS_COLD_SYSTEM_KEY_BASE + 0x07u, flags) \
    X(BMS_COLD_SYSTEM_KEY_BASE + 0x08u, reserved0)

#define BMS_COLD_CTRL_FIELD_LIST(X) \
    X(BMS_COLD_CTRL_KEY_BASE + 0x01u) \
    X(BMS_COLD_CTRL_KEY_BASE + 0x02u) \
    X(BMS_COLD_CTRL_KEY_BASE + 0x03u) \
    X(BMS_COLD_CTRL_KEY_BASE + 0x04u) \
    X(BMS_COLD_CTRL_KEY_BASE + 0x05u)

#define BMS_COLD_FIELD_DESC_PROTECT(key, field) { key, BMS_COLD_OFFSETOF(struct PRT_E2ROM_PARAS, field) },
#define BMS_COLD_FIELD_DESC_SYSTEM(key, field)  { key, BMS_COLD_OFFSETOF(bms_cold_system_params_t, field) },

static flash_kv32_t g_bms_cold_kv;
static u32 g_bms_cold_sector_addrs[(BMS_COLD_KV_SECTORS > 0) ? BMS_COLD_KV_SECTORS : 1];

static const bms_cold_field_desc_t g_bms_protect_fields[] = {
    BMS_COLD_PROTECT_FIELD_LIST(BMS_COLD_FIELD_DESC_PROTECT)
};

static const bms_cold_field_desc_t g_bms_system_fields[] = {
    BMS_COLD_SYSTEM_FIELD_LIST(BMS_COLD_FIELD_DESC_SYSTEM)
};

#define BMS_COLD_CTRL_KEY_ITEM(key)  key,
static const u32 g_bms_control_keys[] = {
    BMS_COLD_CTRL_FIELD_LIST(BMS_COLD_CTRL_KEY_ITEM)
};

#define BMS_COLD_PROTECT_COUNT  ((u16)(sizeof(g_bms_protect_fields) / sizeof(g_bms_protect_fields[0])))
#define BMS_COLD_SYSTEM_COUNT   ((u16)(sizeof(g_bms_system_fields) / sizeof(g_bms_system_fields[0])))
#define BMS_COLD_CTRL_COUNT     ((u16)(sizeof(g_bms_control_keys) / sizeof(g_bms_control_keys[0])))
#define BMS_COLD_TOTAL_KEYS     ((u16)(BMS_COLD_PROTECT_COUNT + BMS_COLD_SYSTEM_COUNT + BMS_COLD_CTRL_COUNT))

static flash_kv32_cache_entry_t g_bms_cold_cache[BMS_COLD_TOTAL_KEYS];
static flash_kv32_key_def_t g_bms_cold_keys[BMS_COLD_TOTAL_KEYS];

static void bms_cold_flash_read(void *ctx, u32 addr, u8 *buf, u32 len)
{
    (void)ctx;
    flash_read_page(addr, (int)len, buf);
}

static int bms_cold_flash_prog(void *ctx, u32 addr, const u8 *buf, u32 len)
{
    (void)ctx;
    return flash_store_prog_checked(addr, buf, len);
}

static int bms_cold_flash_erase_sector(void *ctx, u32 addr, u32 size)
{
    (void)ctx;
    return flash_store_erase_sector_checked(addr, size);
}

static const flash_kv32_port_t *bms_cold_kv_port(void)
{
    static const flash_kv32_port_t port = {
        0,
        bms_cold_flash_read,
        bms_cold_flash_prog,
        bms_cold_flash_erase_sector,
        0,
        0,
    };

    return &port;
}

static u32 bms_cold_get_u16_value(const struct PRT_E2ROM_PARAS *data, u16 offset)
{
    const u16 *field = (const u16 *)((const u8 *)data + offset);
    return *field;
}

static void bms_cold_set_u16_value(struct PRT_E2ROM_PARAS *data, u16 offset, u32 value)
{
    u16 *field = (u16 *)((u8 *)data + offset);
    *field = (u16)value;
}

static u32 bms_cold_get_u32_value(const bms_cold_system_params_t *data, u16 offset)
{
    const u32 *field = (const u32 *)((const u8 *)data + offset);
    return *field;
}

static void bms_cold_set_u32_value(bms_cold_system_params_t *data, u16 offset, u32 value)
{
    u32 *field = (u32 *)((u8 *)data + offset);
    *field = value;
}

void bms_cold_kv_store_get_default_protect(struct PRT_E2ROM_PARAS *protect)
{
    struct PRT_E2ROM_PARAS defaults = E2P_PROTECT_DEFAULT_PRT;

    if (protect != NULL) {
        *protect = defaults;
    }
}

void bms_cold_kv_store_get_default_system(bms_cold_system_params_t *system)
{
    if (system == NULL) {
        return;
    }

    memset(system, 0, sizeof(*system));
    system->bms_type = FD_BMS_TYPE;
    system->series_num = SeriesNum;
    system->capacity_factory = CapacityFactory;
#ifdef AFE_ODC2
    system->afe_odc2 = AFE_ODC2;
#else
    system->afe_odc2 = 0u;
#endif
    system->fac_init_soc = FAC_INIT_soc;
    system->init_soc = FAC_INIT_soc;
    system->flags = 0u;
    system->reserved0 = 0u;
}

static void bms_cold_fill_sector_addrs(void)
{
    u16 i;
    u32 base = flash_store_cfg_get_cold_kv_base();

    for (i = 0; i < BMS_COLD_KV_SECTORS; ++i) {
        g_bms_cold_sector_addrs[i] = base + ((u32)i * BMS_COLD_KV_SECTOR_SIZE);
    }
}

static u32 bms_cold_get_le32(const u8 *buf)
{
    return ((u32)buf[0]) |
           ((u32)buf[1] << 8) |
           ((u32)buf[2] << 16) |
           ((u32)buf[3] << 24);
}

static int bms_cold_layout_header_present(u32 base_addr)
{
    u8 header[4];

    if (base_addr == 0u) {
        return 0;
    }

    flash_read_page(base_addr, sizeof(header), header);
    return (bms_cold_get_le32(header) == BMS_COLD_SECTOR_MAGIC);
}

static int bms_cold_store_matches_defaults(void)
{
    u16 i;
    u32 value;

    for (i = 0u; i < BMS_COLD_TOTAL_KEYS; ++i) {
        value = 0u;
        if (!flash_kv32_get(&g_bms_cold_kv, g_bms_cold_keys[i].key, &value)) {
            return 0;
        }
        if (value != g_bms_cold_keys[i].default_value) {
            return 0;
        }
    }

    return 1;
}

static int bms_cold_pairs_match_current(const flash_kv32_pair_t *pairs, u16 pair_count)
{
    u16 i;
    u32 value;

    for (i = 0u; i < pair_count; ++i) {
        value = 0u;
        if (!flash_kv32_get(&g_bms_cold_kv, pairs[i].key, &value)) {
            return 0;
        }
        if (value != pairs[i].value) {
            return 0;
        }
    }

    return 1;
}

static void bms_cold_erase_previous_layout(u32 base_addr, u16 sector_count)
{
    u16 i;

    for (i = 0u; i < sector_count; ++i) {
        (void)bms_cold_flash_erase_sector(NULL,
                                          base_addr + ((u32)i * BMS_COLD_KV_SECTOR_SIZE),
                                          BMS_COLD_KV_SECTOR_SIZE);
    }
}

static void bms_cold_fill_key_defs(void)
{
    struct PRT_E2ROM_PARAS default_protect;
    bms_cold_system_params_t default_system;
    u16 i;

    bms_cold_kv_store_get_default_protect(&default_protect);
    bms_cold_kv_store_get_default_system(&default_system);

    for (i = 0; i < BMS_COLD_PROTECT_COUNT; ++i) {
        g_bms_cold_keys[i].key = g_bms_protect_fields[i].key;
        g_bms_cold_keys[i].default_value = bms_cold_get_u16_value(&default_protect, g_bms_protect_fields[i].offset);
    }

    for (i = 0; i < BMS_COLD_SYSTEM_COUNT; ++i) {
        g_bms_cold_keys[BMS_COLD_PROTECT_COUNT + i].key = g_bms_system_fields[i].key;
        g_bms_cold_keys[BMS_COLD_PROTECT_COUNT + i].default_value = bms_cold_get_u32_value(&default_system, g_bms_system_fields[i].offset);
    }

    for (i = 0; i < BMS_COLD_CTRL_COUNT; ++i) {
        g_bms_cold_keys[BMS_COLD_PROTECT_COUNT + BMS_COLD_SYSTEM_COUNT + i].key = g_bms_control_keys[i];
        g_bms_cold_keys[BMS_COLD_PROTECT_COUNT + BMS_COLD_SYSTEM_COUNT + i].default_value = 0u;
    }
}

static void bms_cold_try_migrate_previous_layout(void)
{
    flash_kv32_t legacy_kv;
    flash_kv32_cache_entry_t legacy_cache[BMS_COLD_TOTAL_KEYS];
    flash_kv32_cfg_t legacy_cfg;
    u32 legacy_sector_addrs[FLASH_ADDR_PREV_COLD_KV_SECTORS];
    flash_kv32_pair_t pairs[BMS_COLD_TOTAL_KEYS];
    u32 legacy_base = flash_store_cfg_get_previous_cold_kv_base();
    u16 legacy_sector_count = flash_store_cfg_get_previous_cold_kv_sectors();
    u16 i;

    if ((legacy_base == 0u) ||
        (legacy_sector_count < 2u) ||
        (legacy_base == flash_store_cfg_get_cold_kv_base())) {
        return;
    }

    if (!bms_cold_layout_header_present(legacy_base) &&
        !bms_cold_layout_header_present(legacy_base + BMS_COLD_KV_SECTOR_SIZE)) {
        return;
    }

    if (!bms_cold_store_matches_defaults()) {
        return;
    }

    memset(&legacy_cfg, 0, sizeof(legacy_cfg));
    for (i = 0u; i < legacy_sector_count; ++i) {
        legacy_sector_addrs[i] = legacy_base + ((u32)i * BMS_COLD_KV_SECTOR_SIZE);
    }
    legacy_cfg.port = bms_cold_kv_port();
    legacy_cfg.sector_addrs = legacy_sector_addrs;
    legacy_cfg.keys = g_bms_cold_keys;
    legacy_cfg.sector_count = legacy_sector_count;
    legacy_cfg.sector_size = BMS_COLD_KV_SECTOR_SIZE;
    legacy_cfg.write_align = 4u;
    legacy_cfg.key_count = BMS_COLD_TOTAL_KEYS;
    if (!flash_kv32_init(&legacy_kv, &legacy_cfg, legacy_cache)) {
        return;
    }

    for (i = 0u; i < BMS_COLD_TOTAL_KEYS; ++i) {
        pairs[i].key = g_bms_cold_keys[i].key;
        pairs[i].value = g_bms_cold_keys[i].default_value;
        (void)flash_kv32_get(&legacy_kv, pairs[i].key, &pairs[i].value);
    }

    if (flash_kv32_write_pairs(&g_bms_cold_kv, pairs, BMS_COLD_TOTAL_KEYS)) {
        bms_cold_erase_previous_layout(legacy_base, legacy_sector_count);
    }
}

static int bms_cold_get_system_key(bms_cold_system_param_id_t item, u32 *key)
{
    if ((u32)item >= BMS_COLD_SYSTEM_COUNT) {
        return FLASH_KV32_FAILED;
    }

    *key = g_bms_system_fields[(u16)item].key;
    return FLASH_KV32_SUCCESS;
}

static int bms_cold_get_control_key(bms_cold_control_param_id_t item, u32 *key)
{
    if ((u32)item >= BMS_COLD_CTRL_COUNT) {
        return FLASH_KV32_FAILED;
    }

    *key = g_bms_control_keys[(u16)item];
    return FLASH_KV32_SUCCESS;
}

static int bms_cold_ensure_ready(void)
{
    if (g_bms_cold_kv.cfg.port != NULL) {
        return FLASH_KV32_SUCCESS;
    }

    return bms_cold_kv_store_init();
}

int bms_cold_kv_store_init(void)
{
    flash_kv32_cfg_t cfg;

    if ((flash_store_cfg_get_cold_kv_base() == 0u) || (BMS_COLD_KV_SECTORS < 2u)) {
        return FLASH_KV32_FAILED;
    }

    bms_cold_fill_key_defs();
    bms_cold_fill_sector_addrs();

    memset(&cfg, 0, sizeof(cfg));
    cfg.port = bms_cold_kv_port();
    cfg.sector_addrs = g_bms_cold_sector_addrs;
    cfg.keys = g_bms_cold_keys;
    cfg.sector_count = BMS_COLD_KV_SECTORS;
    cfg.sector_size = BMS_COLD_KV_SECTOR_SIZE;
    cfg.write_align = 4u;
    cfg.key_count = BMS_COLD_TOTAL_KEYS;
    if (!flash_kv32_init(&g_bms_cold_kv, &cfg, g_bms_cold_cache)) {
        return FLASH_KV32_FAILED;
    }

    bms_cold_try_migrate_previous_layout();
    return FLASH_KV32_SUCCESS;
}

int bms_cold_kv_store_get_protect(struct PRT_E2ROM_PARAS *protect)
{
    u32 value;
    u16 i;

    if (protect == NULL) {
        return FLASH_KV32_FAILED;
    }

    if (!bms_cold_ensure_ready()) {
        return FLASH_KV32_FAILED;
    }

    bms_cold_kv_store_get_default_protect(protect);
    for (i = 0; i < BMS_COLD_PROTECT_COUNT; ++i) {
        value = 0u;
        if (!flash_kv32_get(&g_bms_cold_kv, g_bms_protect_fields[i].key, &value)) {
            return FLASH_KV32_FAILED;
        }
        bms_cold_set_u16_value(protect, g_bms_protect_fields[i].offset, value);
    }

    return FLASH_KV32_SUCCESS;
}

int bms_cold_kv_store_set_protect(const struct PRT_E2ROM_PARAS *protect)
{
    flash_kv32_pair_t pairs[BMS_COLD_PROTECT_COUNT];
    u16 i;

    if (protect == NULL) {
        return FLASH_KV32_FAILED;
    }

    if (!bms_cold_ensure_ready()) {
        return FLASH_KV32_FAILED;
    }

    for (i = 0; i < BMS_COLD_PROTECT_COUNT; ++i) {
        pairs[i].key = g_bms_protect_fields[i].key;
        pairs[i].value = bms_cold_get_u16_value(protect, g_bms_protect_fields[i].offset);
    }

    if (bms_cold_pairs_match_current(pairs, BMS_COLD_PROTECT_COUNT)) {
        return FLASH_KV32_SUCCESS;
    }

    return flash_kv32_write_pairs(&g_bms_cold_kv, pairs, BMS_COLD_PROTECT_COUNT);
}

int bms_cold_kv_store_get_system(bms_cold_system_params_t *system)
{
    u32 value;
    u16 i;

    if (system == NULL) {
        return FLASH_KV32_FAILED;
    }

    if (!bms_cold_ensure_ready()) {
        return FLASH_KV32_FAILED;
    }

    bms_cold_kv_store_get_default_system(system);
    for (i = 0; i < BMS_COLD_SYSTEM_COUNT; ++i) {
        value = 0u;
        if (!flash_kv32_get(&g_bms_cold_kv, g_bms_system_fields[i].key, &value)) {
            return FLASH_KV32_FAILED;
        }
        bms_cold_set_u32_value(system, g_bms_system_fields[i].offset, value);
    }

    return FLASH_KV32_SUCCESS;
}

int bms_cold_kv_store_set_system(const bms_cold_system_params_t *system)
{
    flash_kv32_pair_t pairs[BMS_COLD_SYSTEM_COUNT];
    u16 i;

    if (system == NULL) {
        return FLASH_KV32_FAILED;
    }

    if (!bms_cold_ensure_ready()) {
        return FLASH_KV32_FAILED;
    }

    for (i = 0; i < BMS_COLD_SYSTEM_COUNT; ++i) {
        pairs[i].key = g_bms_system_fields[i].key;
        pairs[i].value = bms_cold_get_u32_value(system, g_bms_system_fields[i].offset);
    }

    if (bms_cold_pairs_match_current(pairs, BMS_COLD_SYSTEM_COUNT)) {
        return FLASH_KV32_SUCCESS;
    }

    return flash_kv32_write_pairs(&g_bms_cold_kv, pairs, BMS_COLD_SYSTEM_COUNT);
}

int bms_cold_kv_store_get_system_value(bms_cold_system_param_id_t item, u32 *value)
{
    u32 key;

    if (value == NULL) {
        return FLASH_KV32_FAILED;
    }

    if (!bms_cold_ensure_ready()) {
        return FLASH_KV32_FAILED;
    }

    if (!bms_cold_get_system_key(item, &key)) {
        return FLASH_KV32_FAILED;
    }

    return flash_kv32_get(&g_bms_cold_kv, key, value);
}

int bms_cold_kv_store_set_system_value(bms_cold_system_param_id_t item, u32 value)
{
    u32 key;
    u32 current_value = 0u;

    if (!bms_cold_ensure_ready()) {
        return FLASH_KV32_FAILED;
    }

    if (!bms_cold_get_system_key(item, &key)) {
        return FLASH_KV32_FAILED;
    }

    if (flash_kv32_get(&g_bms_cold_kv, key, &current_value) && (current_value == value)) {
        return FLASH_KV32_SUCCESS;
    }

    return flash_kv32_set(&g_bms_cold_kv, key, value);
}

int bms_cold_kv_store_get_control_value(bms_cold_control_param_id_t item, u32 *value)
{
    u32 key;

    if (value == NULL) {
        return FLASH_KV32_FAILED;
    }

    if (!bms_cold_ensure_ready()) {
        return FLASH_KV32_FAILED;
    }

    if (!bms_cold_get_control_key(item, &key)) {
        return FLASH_KV32_FAILED;
    }

    return flash_kv32_get(&g_bms_cold_kv, key, value);
}

int bms_cold_kv_store_set_control_value(bms_cold_control_param_id_t item, u32 value)
{
    u32 key;
    u32 current_value = 0u;

    if (!bms_cold_ensure_ready()) {
        return FLASH_KV32_FAILED;
    }

    if (!bms_cold_get_control_key(item, &key)) {
        return FLASH_KV32_FAILED;
    }

    if (flash_kv32_get(&g_bms_cold_kv, key, &current_value) && (current_value == value)) {
        return FLASH_KV32_SUCCESS;
    }

    return flash_kv32_set(&g_bms_cold_kv, key, value);
}

void bms_cold_kv_store_factory_reset(void)
{
    if (!bms_cold_ensure_ready()) {
        return;
    }
    (void)flash_kv32_format(&g_bms_cold_kv);
}

flash_kv32_dbg_t bms_cold_kv_store_get_dbg(void)
{
    if (!bms_cold_ensure_ready()) {
        flash_kv32_dbg_t dbg;

        memset(&dbg, 0, sizeof(dbg));
        return dbg;
    }
    return flash_kv32_get_dbg(&g_bms_cold_kv);
}
