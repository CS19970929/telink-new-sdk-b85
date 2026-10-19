/* 与 loop.c 的真实 SPI/Flash 端口替身联合；每个固件配置在新进程启动。 */
#include "bms_config_store.h"
#include "bms_state_store.h"

int main(void)
{
    const char *scene = getenv("BMS_CHEM_SCENE");
    const char *image = getenv("BMS_LOOP_FLASH");
    bms_user_params_t user;
    bms_config_system_params_t system;
    bms_afe_aux_measurements_t measurements;
    bms_state_store_data_t state;
    FILE *file;
    assert(scene && image);
    memset(flash, 255, sizeof(flash));
    if (strcmp(scene, "seed") != 0) {
        file = fopen(image, "rb"); assert(file);
        assert(fread(flash, 1, sizeof(flash), file) == sizeof(flash));
        assert(fgetc(file) == EOF); assert(!fclose(file));
    }
    for (unsigned i = 0u; i < 20u; ++i) raw16((uint8_t)(0x69u + 2u*i), 21120u);
    for (unsigned i = 0u; i < 4u; ++i) raw16((uint8_t)(0x5du + 2u*i), 16384u);
    raw16(0x93u, 6758u);
    bms_diag_init(); bms_parameters_init(); bms_afe_init(); request_outputs(); steps(12u);
    memset(&measurements, 0, sizeof(measurements));
    if (!strcmp(scene, "incomplete")) {
        assert(!bms_protection_params_valid());
        assert(!sh3673510_control_ready());
        assert(!bms_afe_driver_get_aux_measurements(&measurements));
        assert(bms_error_get(BMS_ERROR_AFE1));
        require_both_off();
        puts("PASS same symptom: partial chemistry update rejects Config, AFE not ready, sampling invalid");
        return 0;
    }
    assert(bms_protection_params_valid()); assert(sh3673510_control_ready());
    assert(bms_afe_driver_get_aux_measurements(&measurements));
    assert(!bms_error_get(BMS_ERROR_AFE1));
    assert(bms_config_store_get_system(&system));
    assert(system.battery_chemistry == BMS_PRODUCT_CHEMISTRY);
    assert(system.capacity_factory == BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH);
    assert(bms_config_get_user(&user));
    if (!strcmp(scene, "seed")) {
        user.current_offset_ma = 123; user.current_gain_ppm = 1001000u;
        user.balance_enable = 0u;
        memset(user.serial, 0, sizeof(user.serial));
        strcpy(user.serial, "OTA-CHEMISTRY-KEEP");
        assert(bms_config_set_user(&user));
        assert(bms_state_store_save(61u, 5u, 37u));
    } else {
        assert(user.current_offset_ma == 123 && user.current_gain_ppm == 1001000u);
        assert(user.balance_enable == 0u && !strcmp(user.serial, "OTA-CHEMISTRY-KEEP"));
        state = bms_state_store_get();
        assert(state.soc == 61u && state.dsg == 5u && state.cycle == 37u);
        /* 完整保护配置就绪后，原休眠/唤醒控制可以完成；不模拟真实 PAD。 */
        assert(sh3673510_control_sleep());
        assert(sh3673510_control_wake());
    }
    file = fopen(image, "wb"); assert(file);
    assert(fwrite(flash, 1, sizeof(flash), file) == sizeof(flash)); assert(!fclose(file));
    printf("PASS chemistry=%u mask=%u: Config, AFE sampling, calibration/SN/balance/SOC-state preservation\n",
           (unsigned)BMS_PRODUCT_CHEMISTRY, (unsigned)BMS_OTA_UPDATE_MASK);
    return 0;
}
