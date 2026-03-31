#include "config_store.h"

#include "drivers.h"
#include "flash_blob_store.h"
#include "flash_layout.h"
#include "flash_store_cfg.h"

#define CONFIG_STORE_VERSION                    1u
#define CONFIG_STORE_LEGACY_PARAM_ADDR          FLASH_ADDR_SOFT_PROTECT_BASE
#define CONFIG_STORE_LEGACY_BTNAME_ADDR         FLASH_ADDR_BLE_NAME_BASE
#define CONFIG_STORE_BTNAME_MAGIC               0x53465831u

typedef struct __attribute__((packed))
{
    u32 magic;
    u8  len;
    u8  cksum;
    u8  suffix[FLASH_BTNAME_SUFFIX_MAX_LEN];
} config_store_legacy_btname_t;

static const flash_blob_store_cfg_t g_config_store_cfg = {
    FLASH_ADR_CFG_SLOT_A,
    FLASH_ADR_CFG_SLOT_B,
    FLASH_APP_SECTOR_SIZE,
    FLASH_CFG_MAGIC,
    CONFIG_STORE_VERSION,
    sizeof(config_store_blob_t),
};

static flash_blob_store_state_t g_config_store_state;
static u8 g_config_store_ready;

static void config_store_memset(void *dst, u8 value, u32 len)
{
    u8 *p = (u8 *)dst;
    while (len--) {
        *p++ = value;
    }
}

static void config_store_memcpy(void *dst, const void *src, u32 len)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (len--) {
        *d++ = *s++;
    }
}

static u8 config_store_strnlen(const char *s, u8 max_len)
{
    u8 len = 0;
    while (len < max_len && s[len] != '\0') {
        ++len;
    }
    return len;
}

static u8 config_store_checksum8(const u8 *p, u8 n)
{
    u8 sum = 0;
    while (n--) {
        sum ^= *p++;
    }
    return (u8)(sum ^ 0xA5u);
}

static void config_store_fill_defaults(config_store_blob_t *blob)
{
    struct PRT_E2ROM_PARAS default_param = E2P_PROTECT_DEFAULT_PRT;

    config_store_memset(blob, 0, sizeof(*blob));
    blob->meta.layout_version = FLASH_STORAGE_LAYOUT_VERSION;
    blob->meta.param_size = sizeof(PARAM_T);
    blob->param.ParamVer = PARAM_VER;
    blob->param.protect = default_param;
    blob->meta.bt_name_len = 0;
}

static void config_store_fix_meta(config_store_blob_t *blob)
{
    u8 len;

    blob->meta.layout_version = FLASH_STORAGE_LAYOUT_VERSION;
    blob->meta.param_size = sizeof(PARAM_T);
    blob->bt_name_suffix[FLASH_BTNAME_SUFFIX_MAX_LEN] = '\0';
    len = config_store_strnlen(blob->bt_name_suffix, FLASH_BTNAME_SUFFIX_MAX_LEN);
    blob->meta.bt_name_len = len;
}

static int config_store_load_legacy_param(config_store_blob_t *blob)
{
    PARAM_T legacy_param;

    flash_read_page(CONFIG_STORE_LEGACY_PARAM_ADDR, sizeof(legacy_param), (u8 *)&legacy_param);
    if (legacy_param.ParamVer != PARAM_VER) {
        return 0;
    }

    blob->param = legacy_param;
    return 1;
}

static int config_store_load_legacy_btname(config_store_blob_t *blob)
{
    config_store_legacy_btname_t legacy_name;

    flash_read_page(CONFIG_STORE_LEGACY_BTNAME_ADDR, sizeof(legacy_name), (u8 *)&legacy_name);

    if (legacy_name.magic != CONFIG_STORE_BTNAME_MAGIC) {
        return 0;
    }
    if (legacy_name.len == 0 || legacy_name.len > FLASH_BTNAME_SUFFIX_MAX_LEN) {
        return 0;
    }
    if (legacy_name.cksum != config_store_checksum8(legacy_name.suffix, legacy_name.len)) {
        return 0;
    }

    config_store_memcpy(blob->bt_name_suffix, legacy_name.suffix, legacy_name.len);
    blob->bt_name_suffix[legacy_name.len] = '\0';
    blob->meta.bt_name_len = legacy_name.len;
    return 1;
}

static int config_store_try_migrate(config_store_blob_t *blob)
{
    int has_legacy = 0;

    config_store_fill_defaults(blob);

    if (config_store_load_legacy_param(blob)) {
        has_legacy = 1;
    }

    if (config_store_load_legacy_btname(blob)) {
        has_legacy = 1;
    }

    if (!has_legacy) {
        return 0;
    }

    config_store_fix_meta(blob);
    return config_store_save(blob);
}

int config_store_init(void)
{
    if (!g_config_store_ready)
    {
        g_config_store_state.active_addr = 0;
        g_config_store_state.seq = 0;
        g_config_store_state.valid = 0;
        g_config_store_ready = 1;
    }

    return 1;
}

int config_store_load(config_store_blob_t *blob)
{
    if (!blob) {
        return 0;
    }

    config_store_init();

    if (flash_blob_store_load(&g_config_store_cfg, blob, &g_config_store_state))
    {
        config_store_fix_meta(blob);
        return 1;
    }

    if (config_store_try_migrate(blob)) {
        return 1;
    }

    config_store_fill_defaults(blob);
    return 0;
}

int config_store_save(const config_store_blob_t *blob)
{
    config_store_blob_t local_blob;

    if (!blob) {
        return 0;
    }

    config_store_init();

    local_blob = *blob;
    config_store_fix_meta(&local_blob);

    return flash_blob_store_save(&g_config_store_cfg, &local_blob, &g_config_store_state);
}

int config_store_factory_reset(void)
{
    config_store_blob_t blob;

    config_store_init();
    if (!flash_blob_store_reset(&g_config_store_cfg)) {
        return 0;
    }

    g_config_store_state.active_addr = 0;
    g_config_store_state.seq = 0;
    g_config_store_state.valid = 0;

    config_store_fill_defaults(&blob);
    return config_store_save(&blob);
}
