# BMSAssistantMiniProgram

微信小程序原生 TypeScript 客户端，直接对接当前 `vendor/ble_sample` 的 `Modbus RTU over BLE` 与 Telink Legacy OTA。

## 当前能力

### BMS 业务

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

### OTA

- 独立 OTA 页面，不与普通 Modbus transport 共用 pending request
- OTA Service：`00010203-0405-0607-0809-0A0B0C0D1912`
- OTA Characteristic：`00010203-0405-0607-0809-0A0B0C0D2B12`
- Telink Legacy OTA / 16-byte PDU
- 固定 20-byte data packet
- `.bin` 的 `KNLT` firmware mark 检查
- header firmware size 检查
- `CMD_OTA_START / DATA / CMD_OTA_END / CMD_OTA_RESULT`
- OTA CRC16
- 逐包串行 `writeType: 'write'`
- 升级进度与 OTA result 日志
- 升级前二次确认

## 开发基线

开发前必须阅读：

- `docs/BMS_BLE_通信对接规范_V1.0.md`
- `docs/BMS_OTA_通信规范_V1.0.md`
- `docs/register_catalog.json`
- `docs/protocol_test_vectors.json`
- `docs/ota_test_vectors.json`

## 普通 BLE 运行

1. 微信开发者工具导入本目录。
2. 替换 `project.config.json` 中的 `appid`；真机 BLE 使用实际小程序 AppID。
3. 点击“预览”或“真机调试”，手机微信扫码进入。
4. 手机打开蓝牙并允许微信/小程序使用蓝牙。
5. 扫描 `BT_*` 设备。
6. 连接后状态必须进入 `ready` 再发业务命令。

不需要先提交审核或正式发布才能做 BLE 真机测试。

## OTA 真机测试

1. 编译并准备待升级的 Telink `firmware.bin`。
2. 把 `firmware.bin` 发送到“文件传输助手”或任意微信会话。
3. 在小程序主页点击“固件 OTA”。
4. 扫描目标 `BT_*` BMS，选择后点击“连接 OTA”。
5. 等状态进入 `OTA characteristic READY`。
6. 点击“选择 firmware.bin”，从微信会话中选择刚才发送的 bin。
7. 页面必须显示 firmware size 与 packet count，说明 Telink header 校验通过。
8. 点击“开始 OTA”并二次确认。
9. 观察进度、TX 日志和最终 `OTA_RESULT`。
10. 成功后等待 BMS 重启，回主页重新扫描、连接并读取软件版本确认新固件已运行。

OTA 必须使用真机验证，桌面模拟器不能替代 BLE OTA 验收。

## 协议回归

普通协议：

```bash
# 由项目当前 TypeScript 工具链执行
# tests/protocol_smoke.ts
```

OTA 固定向量：

```text
START:
01 FF

DATA index=0:
00 00 26 80 00 00 00 00 5D 02 4B 4E 4C 54 30 04 88 00 1C A3

END max_index=0x14FA:
02 FF FA 14 05 EB
```

对应测试：`tests/ota_smoke.ts`。

## 约束

普通 BMS 通信请求侧不支持跨包重组，因此所有请求必须 `<=20 byte`。业务请求严格串行，同一时刻只能存在一条 pending request。

OTA V1 同样固定为 20-byte data packet，不依赖大 MTU。微信文档也建议 BLE 4.0 写入单次不要超过 20 byte，并提示并行写可能失败，因此 OTA transport 使用逐包 `await` 串行发送。

当前固件 `BLE_APP_SECURITY_ENABLE=0`。写 SOC、写蓝牙名以及 OTA 都是工程联调入口；正式客户版本必须增加权限/鉴权与固件可信校验。
