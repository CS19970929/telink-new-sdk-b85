// Generated client constants from docs/register_catalog.json.
// Do not edit manually; update the JSON source and regenerate.

export const BMSGeneratedUUIDs = {
  SERVICE_UUID: '6E400001-B5A3-F393-E0A9-E50E24DCCA9E',
  REQUEST_CHARACTERISTIC_UUID: '6E400002-B5A3-F393-E0A9-E50E24DCCA9E',
  RESPONSE_CHARACTERISTIC_UUID: '6E400003-B5A3-F393-E0A9-E50E24DCCA9E',
} as const;

export const BMSGeneratedBLEConstraints = {
  DEFAULT_ATT_MTU: 23,
  SAFE_SINGLE_REQUEST_PAYLOAD_BYTES: 20,
  RESPONSE_NOTIFY_FRAGMENT_BYTES: 20,
} as const;

export const BMSGeneratedRegisterCatalog = {
  MAC_ADDRESS_START: 0x0000,
  MAC_ADDRESS_WORD_COUNT: 3,
  BT_NAME_START: 0x0100,
  BT_NAME_READ_WORD_COUNT: 12,
  PRODUCT_SERIAL_START: 0xC002,
  PRODUCT_SERIAL_WORD_COUNT: 16,
  PRODUCT_HARDWARE_VERSION_START: 0xC012,
  PRODUCT_HARDWARE_VERSION_WORD_COUNT: 16,
  PRODUCT_SOFTWARE_VERSION_START: 0xC022,
  PRODUCT_SOFTWARE_VERSION_WORD_COUNT: 16,
  EVENT_LOG_PREVIEW_START: 0xC008,
  EVENT_LOG_PREVIEW_WORD_COUNT: 20,
  LEGACY_CELL_ARRAY_START: 0xD000,
  LEGACY_CELL_ARRAY_WORD_COUNT: 63,
  SYSTEM_STATUS_START: 0xD115,
  SYSTEM_STATUS_WORD_COUNT: 2,
  REALTIME_STATUS_START: 0xD120,
  REALTIME_STATUS_WORD_COUNT: 11,
  REALTIME_STATUS_MAGIC: 0x4253,
  PROTECT_PREVIEW_START: 0x2100,
  PROTECT_PREVIEW_WORD_COUNT: 15,
  SOC_WRITE_REGISTER_START: 0x1005,
  DEBUG_REGISTER_1102_START: 0x1102,
  DEBUG_REGISTER_1103_START: 0x1103,
} as const;

export const BMSSystemStatusBits = [
  { bit: 0, key: 'startup', title: '启动完成' },
  { bit: 1, key: 'mos_pre', title: '预充 MOS' },
  { bit: 2, key: 'mos_chg', title: '充电 MOS' },
  { bit: 3, key: 'mos_dsg', title: '放电 MOS' },
  { bit: 4, key: 'relay_pre', title: '预充继电器' },
  { bit: 5, key: 'relay_chg', title: '充电继电器' },
  { bit: 6, key: 'relay_dsg', title: '放电继电器' },
  { bit: 7, key: 'relay_main', title: '主继电器' },
  { bit: 8, key: 'heat', title: '加热' },
  { bit: 9, key: 'cool', title: '冷却' },
  { bit: 10, key: 'afe1', title: 'AFE1' },
  { bit: 11, key: 'afe2', title: 'AFE2' },
  { bit: 12, key: 'balance', title: '均衡' },
  { bit: 13, key: 'sleep', title: '待休眠' },
  { bit: 14, key: 'bn_close', title: 'BMS 关断输出' },
  { bit: 15, key: 'heat_close', title: '加热关闭输出' },
  { bit: 18, key: 'driver_ext', title: '外部驱动控制' },
] as const;
