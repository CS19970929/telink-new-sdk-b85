"""同领域检查直接执行各场景；场景状态独立，断言与故障注入保留。"""
from __future__ import annotations

def check_afe_hw_profile_contract_check():
    print("CHECK afe_hw_profile_contract_check", flush=True)
    #!/usr/bin/env python3
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    import re

    ROOT = Path(__file__).resolve().parents[1]
    HERE = Sources(ROOT)

    def text(name):
        return (HERE / name).read_text(encoding='utf-8')

    def macro_int(src, name):
        m = re.search(rf'(?m)^\s*#define\s+{re.escape(name)}\s+\(?([0-9]+)\)?\s*[uUlL]*\s*(?:/\*.*\*/)?$', src)
        if not m:
            raise AssertionError(f'missing integer macro: {name}')
        return int(m.group(1), 10)

    p = text('bms_afe_hw_profile.c')
    h = text('bms_afe_hw_profile.h')
    m = text('modbus_rtu.c')
    c = text('sh3673510_control.c')
    b = text('sh3673510_bms.c')
    config = text('bms_config_store.c')
    product = text('sh3673510_project_config.h') + text('bms_sh3673510_config.h') + (ROOT / "bms/products/sh3673510_defaults.h").read_text(encoding="utf8")

    assert 'BMS_CONFIG_AFE_WORDS             35u' in config
    assert 'storage_record_save(&g_bms_config_store' in config
    assert 'flash_kv32' not in config
    assert 'BMS_AFE_HW_PROFILE_SCHEMA_VERSION' in p
    assert 'bms_afe_hw_profile_build_default(&cfg->afe_hw)' in config
    assert 'bms_afe_hw_profile_build_migration_default' not in p
    assert 'bms_afe_hw_profile_build_migration_default' not in h
    assert 'void bms_afe_hw_profile_build_default' in p
    assert 'SH3673510_HW_DEFAULT_COV_MV' in p
    assert 'SH3673510_HW_DEFAULT_OCD1_A10' in p
    assert 'SH3673510_HW_DEFAULT_OCC1_A10' in p
    assert 'sh3673510_quantize_current_a10' in p
    assert 'qty != BMS_AFE_HW_PROFILE_WORD_COUNT' in p
    assert 'bms_afe_hw_profile_set(&candidate)' in p
    commit = m[m.index('static u8 commit_protection_update'):m.index('static u16 u16be(', m.index('static u8 commit_protection_update'))]
    assert 'bms_afe_apply_protection_config' not in commit
    apply = c[c.index('uint8_t sh3673510_control_apply_protection'):c.index('uint8_t sh3673510_control_get_protection_actual')]
    assert 'g_tParam.protect' not in apply
    assert 'bms_afe_hw_profile_get(&hw)' in b

    # D014 current sense: 667uOhm. Requested values are rounded upward by AFE
    # hardware. Recovery must be below the effective encoded threshold, not
    # necessarily below the user's requested threshold.
    shunt = macro_int(product, 'SH3673510_BOARD_SHUNT_UOHM')
    ocd1_req = macro_int(product, 'SH3673510_HW_DEFAULT_OCD1_A10')
    ocd_rec = macro_int(product, 'SH3673510_HW_DEFAULT_OCD_RECOVER_A10')
    occ1_req = macro_int(product, 'SH3673510_HW_DEFAULT_OCC1_A10')
    occ_rec = macro_int(product, 'SH3673510_HW_DEFAULT_OCC_RECOVER_A10')

    def effective_a10(requested_a10, step_uv, max_code):
        sense_uv = (requested_a10 * shunt + 5) // 10
        steps = (sense_uv + step_uv - 1) // step_uv
        steps = max(1, min(steps, max_code + 1))
        actual_uv = steps * step_uv
        return (actual_uv * 10 + shunt - 1) // shunt

    assert ocd1_req == ocd_rec == 100
    assert occ1_req == occ_rec == 100
    assert effective_a10(ocd1_req, 5000, 15) == 150
    assert effective_a10(occ1_req, 1375, 31) == 104
    assert ocd_rec < effective_a10(ocd1_req, 5000, 15)
    assert occ_rec < effective_a10(occ1_req, 1375, 31)

    print('Independent D014 AFE hardware protection defaults + effective-threshold validation: PASS')

def check_sh3673510_d014_integration_check():
    print("CHECK sh3673510_d014_integration_check", flush=True)
    #!/usr/bin/env python3
    """HS-D014 / SH3673510 integration contract checks."""
    from pathlib import Path
    from project_paths import Sources, host_includes, selected_source
    import re

    ROOT = Path(__file__).resolve().parents[1]
    HERE = Sources(ROOT)


    def text(name: str) -> str:
        return (HERE / name).read_text(encoding="utf-8", errors="ignore")


    def literal(src: str, name: str) -> int:
        # 注释语言及换行不属于宏值契约，先去掉块注释再匹配数值。
        src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
        m = re.search(rf"(?m)^\s*#define\s+{re.escape(name)}\s+(0x[0-9A-Fa-f]+|[0-9]+)(?:[uUlL]*)\s*(?:/\*.*\*/)?\s*$", src)
        if not m:
            raise AssertionError(f"missing literal macro {name}")
        return int(m.group(1), 0)


    def require(src: str, needle: str) -> None:
        if needle not in src:
            raise AssertionError(f"missing D014 contract text: {needle}")


    cfg = text("sh3673510_project_config.h") + text("bms_sh3673510_config.h") + (ROOT / "bms/products/sh3673510_defaults.h").read_text(encoding="utf8")
    conf = text("bms_product_conf.h")
    app = text("app.c")
    main = text("main.c")
    control = text("sh3673510_control.c")
    bms = text("sh3673510_bms.c")
    sw_protection = text("bms_sw_protection.c")
    board = text("bms_board.c")
    uart = text("modbus_uart.c")
    port = text("sh3673520_port.c")
    port_h = text("sh3673520_port.h")
    driver = text("sh3673520.c")

    # Product electrical profile from the D014 schematic.
    assert literal(cfg, "SH3673510_BOARD_CELL_COUNT") == 8
    assert literal(cfg, "SH3673510_BOARD_SHUNT_UOHM") == 667
    assert literal(cfg, "SH3673510_BOARD_NTC_NOMINAL_OHM") == 10000
    assert literal(cfg, "SH3673510_PRODUCT_HEATER_SUPPORTED") == 0
    assert literal(cfg, "SH3673510_PRODUCT_BALANCE_SUPPORTED") == 1
    assert literal(cfg, "SH3673510_PRODUCT_HEATER_NTC_SUPPORTED") == 0
    assert literal(cfg, "SH3673510_PRODUCT_MOS_NTC_SUPPORTED") == 1
    assert literal(cfg, "SH3673510_BOARD_TS4_HW_PROTECT_EN") == 0

    # Canonical D014 board nets.
    for pin in (
        "BMS_BOARD_CMNT_EN_PIN                        GPIO_PD4",
        "BMS_BOARD_AFE_SCLK_PIN                       GPIO_PD7",
        "BMS_BOARD_SWITCH_PIN                         GPIO_PA0",
        "BMS_BOARD_RS485_EN_PIN                       GPIO_PA1",
        "BMS_BOARD_SWS_PIN                            GPIO_PA7",
        "BMS_BOARD_INT_WK_MCU_PIN                     GPIO_PB1",
        "BMS_BOARD_AFE_MISO_PIN                       GPIO_PB6",
        "BMS_BOARD_AFE_MOSI_PIN                       GPIO_PB7",
        "BMS_BOARD_AFE_ALARM_PIN                      GPIO_PC0",
        "BMS_BOARD_AFE_RESET_OUT_PIN                  GPIO_PC1",
        "BMS_BOARD_SCI1_TX_PIN                        GPIO_PC2",
        "BMS_BOARD_SCI1_RX_PIN                        GPIO_PC3",
        "BMS_BOARD_DEBUG_LED_PIN                      GPIO_PC4",
        "BMS_BOARD_CMNT_WK_PIN                        GPIO_PD3",
        "BMS_BOARD_AFE_CS_PIN                         GPIO_PD2",
    ):
        require(cfg, pin)

    # Product identity and communications.
    require(conf, "#define FD_BMS_TYPE 2u")
    require(conf, "#define SeriesNum                      SH3673510_BOARD_CELL_COUNT")
    require(conf, "#define MODBUS_RS485_ENABLE              1")
    require(conf, 'BMS_HARDWARE_VERDION_DEFAULT   "D014"')
    if not re.search(r'BMS_SERIAL_NUMBER_DEFAULT\s+"D014-[^"]+"', conf):
        raise AssertionError("D014 default serial number must retain the D014- prefix")
    require(conf, '#define DEV_NAME_STR  "BT_D014"')
    require(text("bms_product_config.h"), "#define BMS_PRODUCT_ID 14u")

    # AFE SPI remains the verified SH36735xx Mode-3, 500-kHz implementation.
    require(cfg, "SH3673520_SPI_GROUP_B6_B7_D2_D7")
    require(port, "SPI_GPIO_GROUP_B6B7D2D7")
    require(port, "SPI_MODE3")
    require(port, "*cs_pin = GPIO_PD2")
    require(port_h, "#define SH3673520_PORT_SPI_CLOCK_HZ              500000UL")

    # The 8S profile must actually drive acquisition/balance sizing.
    require(bms, "int32_t cell[SH3673510_BOARD_CELL_COUNT];")
    require(bms, "SH3673520_ReadCellVoltages(cell, SH3673510_BOARD_CELL_COUNT)")
    require(bms, "SH3673510_BOARD_SHUNT_UOHM")
    require(control, "SH3673520_SetBalanceMask")
    require(control, "SH3673510_BOARD_CELL_COUNT")
    require(text("sh3673510_project_config.h"), "SH3673510_BOARD_CELL_COUNT")

    # D014 intentionally has no qualified heater output or TS3 heater NTC.
    require(board, "if (SH3673510_PRODUCT_HEATER_SUPPORTED)")
    require(control, "#if SH3673510_PRODUCT_HEATER_SUPPORTED")
    require(bms, "#if SH3673510_PRODUCT_HEATER_NTC_SUPPORTED")
    require(bms, "#if SH3673510_PRODUCT_MOS_NTC_SUPPORTED")
    require(bms, "sw.mos_temp_valid = s_ntc_valid[SH3673510_BOARD_MOS_NTC_INDEX]")
    require(sw_protection, "!inputs->mos_temp_required || inputs->mos_temp_valid")
    require(sw_protection, "p->u16TmosOTp_Third")
    if "BMS_BOARD_HEATER_FUSE_TRIGGER_PIN" in app:
        raise AssertionError("D014 app must not drive the inherited D011 heater-fuse pin")
    if "HT-RF-EN" in app or "HT-CHG" in app:
        raise AssertionError("D014 app reintroduced a D011-only heater net")

    # D014 app/RS485 paths use canonical D014 names, not board-name inheritance.
    for token in (
        "BMS_BOARD_SWITCH_PIN",
        "BMS_BOARD_INT_WK_MCU_PIN",
        "BMS_BOARD_AFE_ALARM_PIN",
        "BMS_BOARD_AFE_RESET_OUT_PIN",
        "BMS_BOARD_CMNT_EN_PIN",
        "BMS_BOARD_CMNT_WK_PIN",
    ):
        require(app, token)
    require(main, "BMS_BOARD_DEBUG_LED_ENABLE")
    require(main, "BMS_BOARD_DEBUG_LED_PIN")
    require(uart, "BMS_BOARD_RS485_EN_PIN")
    require(uart, "modbus_rs485_receive_mode")
    require(uart, "modbus_rs485_transmit_mode")
    require(uart, "uart_tx_is_busy()")

    # Common-port FET arbitration and independent SW/HW protection remain inherited.
    # 共用 MOS 请求的实际 CHG/DSG 输出由 bms_simplification_host_check 执行验证。
    require(bms, "service_hw_flag_recovery")
    require(bms, "service_short_recovery")
    require(bms, "service_afe_reconfiguration")
    require(bms, "bms_afe_samples_qualified()")
    require(bms, "s_requested_charge_on")
    require(bms, "s_requested_discharge_on")

    print("HS-D014 SH3673510 integration contract: PASS")

if __name__ == "__main__":
    check_afe_hw_profile_contract_check()
    check_sh3673510_d014_integration_check()
