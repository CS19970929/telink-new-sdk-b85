# BMSAssistantMiniProgram

微信小程序原生 TypeScript 客户端，直接对接当前 `vendor/ble_sample` 的 `Modbus RTU over BLE` 协议。

## 当前能力

- 扫描 `BT_*` BMS
- BLE 连接 / Service / Characteristic 发现
- 订阅 `6E400003...` Notify
- 向 `6E400002...` 写 Modbus 请求
- 20 byte 请求长度保护
- 多 Notify 响应重组
- CRC16/MODBUS
- Echo
- 设备身份读取
- 实时状态 + Legacy fallback
- 单体电压
- SystemStatus 解码
- 写 SOC
- 写蓝牙名 suffix
- TX/RX 日志

## 开发基线

开发前必须阅读：

- `docs/BMS_BLE_通信对接规范_V1.0.md`
- `docs/register_catalog.json`
- `docs/protocol_test_vectors.json`

## 运行

1. 微信开发者工具导入本目录。
2. 替换 `project.config.json` 中的 `appid`（需要真机 BLE 时使用实际小程序 AppID）。
3. 真机打开蓝牙并授权。
4. 扫描 `BT_*` 设备。
5. 连接后状态必须进入 `ready` 再发业务命令。

## 约束

当前固件请求侧不支持跨包重组，因此所有请求必须 `<=20 byte`。业务请求严格串行，同一时刻只能存在一条 pending request。

当前固件 `BLE_APP_SECURITY_ENABLE=0`，本工程中的写 SOC/写蓝牙名仅作为工程联调入口，正式客户版本需做权限收口。
