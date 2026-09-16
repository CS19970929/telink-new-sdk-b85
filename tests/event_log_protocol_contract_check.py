#!/usr/bin/env python3
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SDK_ROOT = ROOT / "tc_ble_single_sdk-V3.4.2.8_Patch_0001" / "tc_ble_single_sdk"
MODULE_DIR = SDK_ROOT / "vendor" / "ble_sample"
CATALOG = SDK_ROOT / "docs" / "register_catalog.json"
GENERATED_DIR = SDK_ROOT / "docs" / "generated"
QT_PROTOCOL = ROOT / "tools" / "BMSAssistantQt" / "bmsassistantqt" / "protocol.py"
MAC_PROTOCOL = ROOT / "tools" / "BMSAssistant" / "Sources" / "BMSAssistant" / "Protocol" / "BMSProtocol.swift"
ANDROID_GENERATED = ROOT / "tools" / "BMSAssistantAndroid" / "app" / "src" / "main" / "kotlin" / "bms" / "protocol" / "BmsGeneratedRegisterCatalog.kt"


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


event_h = text(MODULE_DIR / "bms_event_log.h")
event_c = text(MODULE_DIR / "bms_event_log.c")
modbus_c = text(MODULE_DIR / "modbus_rtu.c")
qt_protocol = text(QT_PROTOCOL)
mac_protocol = text(MAC_PROTOCOL)

# Historical tools must keep their exact entry point.
assert "#define BMS_EVENT_LOG_REG_BASE           0xC008u" in event_h
assert "#define BMS_EVENT_LOG_REG_COUNT          BMS_EVENT_LOG_ENTRY_COUNT" in event_h

# New clients get a normal, non-overlapping, pageable 100-word window.
assert "#define BMS_EVENT_LOG_PAGED_REG_BASE     0xD200u" in event_h
assert "#define BMS_EVENT_LOG_PAGED_REG_COUNT    BMS_EVENT_LOG_ENTRY_COUNT" in event_h

# The adapter preserves C008 exact-start compatibility while mapping D200+offset
# to the corresponding newest-first event index.
assert "if (reg == BMS_EVENT_LOG_REG_BASE)" in modbus_c
assert "reg >= BMS_EVENT_LOG_PAGED_REG_BASE" in modbus_c
assert "first_index = (u16)(reg - BMS_EVENT_LOG_PAGED_REG_BASE);" in modbus_c
assert "bms_event_log_read_reg((u16)(first_index + i))" in modbus_c
assert "MB_EX_ILLEGAL_ADDRESS" in modbus_c

# Shipping desktop clients use short pageable reads; legacy start remains named.
assert "eventLogStart = 0xD200" in qt_protocol
assert "eventLogLegacyStart = 0xC008" in qt_protocol
assert "eventLogPreviewCount = 20" in qt_protocol
assert "eventLogCount = 100" in qt_protocol
assert "eventLogStart: UInt16 = 0xD200" in mac_protocol
assert "eventLogLegacyStart: UInt16 = 0xC008" in mac_protocol
assert "eventLogPreviewCount: UInt16 = 20" in mac_protocol
assert "eventLogCount: UInt16 = 100" in mac_protocol

# Shared register source-of-truth and generated client constants must agree.
catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
event_block = next(block for block in catalog["register_blocks"] if block["id"] == "event_log_preview")
assert event_block["start_address"] == "0xD200"
assert event_block["legacy_start_address"] == "0xC008"
assert event_block["word_count"] == 20
assert event_block["full_word_count"] == 100

assert "EVENT_LOG_PREVIEW_START = 0xD200" in text(GENERATED_DIR / "bms_generated_register_catalog.py")
assert "EVENT_LOG_PREVIEW_START: UInt16 = 0xD200" in text(GENERATED_DIR / "BMSGeneratedRegisterCatalog.swift")
assert "EVENT_LOG_PREVIEW_START: Int = 0xD200" in text(GENERATED_DIR / "BmsGeneratedRegisterCatalog.kt")
assert "EVENT_LOG_PREVIEW_START = 0xD200;" in text(GENERATED_DIR / "bms_generated_register_catalog.hpp")
assert "EVENT_LOG_PREVIEW_START: Int = 0xD200" in text(ANDROID_GENERATED)

# This feature is a communication adapter only. Event persistence stays on Storage V1.
assert "storage_record_open(&g_bms_event_log.store" in event_c
assert "storage_record_load(&g_bms_event_log.store" in event_c
assert "storage_record_save(&g_bms_event_log.store" in event_c
assert "flash_write_page" not in event_c
assert "flash_erase_sector" not in event_c

print("Event-log upper-computer contract: PASS")
