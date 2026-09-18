# Telink 官方 OTA Master、D008 OTA Server 与 Windows 上位机 OTA

> 更新时间：2026-09-19  
> 固件基线：`refactor/d008-common-bms-features`  
> Windows 上位机：`feature/windows-afe-hw-protection-editor-v2/bms-tool-windows/`

## 1. 结论

D008 common 中同时具备 Telink OTA Server 和 Telink 官方 OTA Master 参考实现；Windows 上位机也已经具备可用的 OTA 页面和传输框架。

本次核对后的定位：

- D008 本机是 **OTA Server / 被升级端**。
- Windows 上位机是 **OTA Client / 升级发起端**。
- SDK 中 `vendor/ble_master_kma_dongle/` 是 Telink MCU 作为 BLE Central 发起 OTA 的官方参考，可用于核对 Windows 实现。
- D008 当前 `MTU_SIZE_SETTING` 未覆盖 SDK 默认值，因此 MTU=23，Auto 模式实际应走 Legacy 16-byte OTA；Extend64 只在未来 MTU 足够时使用。
- Windows 原实现已经有 START/DATA/END、OTA_RESULT、Auto/Legacy/Extend、取消、进度、升级后重连和 STM32 IAP，但存在 Extended START 长度错误和成功判定过宽问题。本次已修正。

## 2. Telink 官方参考位置

### OTA Master

```text
tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_master_kma_dongle/
├─ blm_ota.c
├─ blm_ota.h
├─ blm_host.c
├─ blm_host.h
├─ blm_att.c
├─ blm_att.h
├─ app.c
├─ app_config.h
└─ main.c
```

`blm_ota.c` 的 Legacy 主流程：

```text
连接 Slave
  -> 发现 OTA characteristic
  -> CMD_OTA_START
  -> 等待约 50 ms
  -> index + 16-byte data + CRC16
  -> CMD_OTA_END
```

官方 Master 还会限制 TX FIFO 深度，不是无边界地把所有 Write Command 一次性塞入控制器。

### OTA 协议定义

```text
stack/ble/service/ota/ota.h
stack/ble/service/ota/ota_server.h
stack/ble/service/uuid.h
```

关键 opcode：

| 命令 | 值 |
|---|---:|
| CMD_OTA_START | 0xFF01 |
| CMD_OTA_END | 0xFF02 |
| CMD_OTA_START_EXT | 0xFF03 |
| CMD_OTA_FW_VERSION_REQ | 0xFF04 |
| CMD_OTA_FW_VERSION_RSP | 0xFF05 |
| CMD_OTA_RESULT | 0xFF06 |

## 3. Extended START 的关键格式

SDK 定义：

```c
typedef struct {
    u16 ota_cmd;
    u8  pdu_length;
    u8  version_compare;
    u8  rsvd[16];
} ota_startExt_t;
```

因此 `CMD_OTA_START_EXT` 在线上必须是 **20 bytes**：

```text
03 FF
40          // 64-byte PDU
00          // version_compare disabled
00 ... 00   // 16 reserved bytes
```

旧 Windows 实现只发送 `03 FF 40 00` 四字节，和 SDK 结构不一致。本次已经改为完整 20-byte 命令并加入自动测试。

## 4. DATA / CRC / END

### Legacy

```text
index[2] + data[16] + CRC16[2]
```

- index 从 0 开始。
- 最后一包不足 16 bytes 时补 `0xFF`。
- CRC16 覆盖 index + data。
- CRC 算法和现有 `ModbusRtu.Crc16` 一致：初值 0xFFFF，多项式 0xA001。

### Extended

```text
index[2] + data[16*n] + CRC16[2]
```

- index 从 1 开始。
- 声明长度必须为 16*n，范围 16..240。
- Windows 当前 Extend64 使用 64-byte data。
- 尾包补到下一个 16-byte 边界。

END：

```text
02 FF + last_index[2] + ~last_index[2]
```

## 5. D008 OTA Server

D008 `vendor/ble_sample/app_config.h`：

```c
#define BLE_OTA_SERVER_ENABLE 1
#define APP_OTA_PROCESS_TIMEOUT_S 180
#define APP_OTA_DATA_PACKET_TIMEOUT_S 15
```

初始化代码：

```c
blc_ota_initOtaServer_module();
blc_ota_setOtaProcessTimeout(...);
blc_ota_setOtaDataPacketTimeout(...);
blc_ota_registerOtaStartCmdCb(app_enter_ota_mode);
blc_ota_registerOtaResultIndicationCb(app_ota_end_result);
```

OTA 期间 D008：

- 设置 `ota_is_working`；
- 请求更适合 OTA 的连接参数；
- latency 强制为 0；
- 阻止相关低功耗路径；
- Flash Protection 在 SDK OTA 写/擦阶段临时 unlock，结束后恢复 lock。

当前 D008 没有调用 `blc_ota_setFirmwareSizeAndBootAddress()`，因此按 SDK 默认值，最大固件约 124 KiB、默认 OTA boot address 0x20000。Windows 对明确识别到的 D008 提前执行 124 KiB 边界检查。

## 6. UUID 对应

Windows：

```text
OTA Service
00010203-0405-0607-0809-0a0b0c0d1912

OTA Characteristic
00010203-0405-0607-0809-0a0b0c0d2b12
```

与当前 SDK `TELINK_OTA_UUID_SERVICE` / `TELINK_SPP_DATA_OTA` 的字节序转换一致。

## 7. Windows 上位机当前能力

```text
BmsTool.Windows/Ota.cs
BmsTool.Windows/TelinkOtaProtocol.cs
BmsTool.Windows/Stm32SerialBleOta.cs
BmsTool.Windows/MainWindow.xaml
BmsTool.Windows/MainWindow.xaml.cs
```

支持：

- Telink OTA 自动发现。
- Legacy 16-byte OTA。
- Extended 64-byte OTA。
- OTA RESULT 解析和错误码显示。
- OTA 进度、速率、ETA、取消。
- STM32 BLE-UART IAP。
- STM32 COM IAP。
- OTA 后自动重连、身份和实时数据读取。

客户版 OTA 页面仍受“高级功能”入口控制；内部完整版复用客户版 OTA 真源。

## 8. 本次完善

### 8.1 修正 START_EXT

从错误的 4 bytes 改为官方 20 bytes。

### 8.2 恢复官方 Master 的准备时序

发送 START 后，Legacy 至少等待约 60 ms 再发第一包 DATA；Extended 在等待早期 OTA_RESULT 时已经获得更长准备窗口。所有 DATA 发完后再留 50 ms drain 时间再发 END。

### 8.3 降低 Windows WriteWithoutResponse 突发压力

Telink 官方 Master 会看 TX FIFO 深度。Windows API 无法直接读取控制器 TX FIFO，因此采用轻量 pacing：固定批次后让出极短时间，避免长固件在不同蓝牙适配器/驱动上形成无限突发。

### 8.4 D008 固件预检

当前 D008 startup 在 0x08 固定放置：

```text
0x544C4E4B = "TLNK"
```

明确识别为 D008 时，新固件必须带该 marker，并且不得超过当前 SDK 默认 124 KiB OTA 边界。其他 Telink 项目不强行套用 D008 专属门禁。

### 8.5 防止“假成功”

旧逻辑在最终 OTA_RESULT 未收到时，只要旧程序还能重新连接和读取数据，也可能显示“升级成功”。

现在成功必须至少有一种正向证据：

1. OTA Server 明确返回成功；或
2. 软件版本在升级前后确实发生变化；或
3. D008 Firmware Build ID 在升级前后确实发生变化。

用户填写的“目标软件版本”用于做**不匹配失败门禁**，但不会单独作为成功证据；否则重复刷同版本固件时，旧固件仍正常运行也会造成假阳性。

如果只是“设备还能通信”，但没有任何新固件证据，界面显示“升级结果未完全确认”，不再显示成功。

### 8.6 加入 Build ID 证据

D008 诊断窗口 `0x2A00` 中包含 Firmware Build ID。Windows 会在 OTA 前后尽量读取 Build ID；旧固件不支持该窗口时自动降级，不影响 OTA。

### 8.7 连接取消

OTA GATT discovery 接入 CancellationToken，用户点“取消”时不会只能等 discovery 自己超时。

### 8.8 自动测试与 CI

新增：

```text
Tests/OtaProtocolTest.cs
test-ota-protocol.ps1
```

固定验证：

- Legacy START。
- 20-byte START_EXT。
- Extended PDU 长度规则。
- Legacy index=0。
- Extended index=1。
- CRC16 固定向量。
- 0xFF 尾包补齐。
- END index / inverse。
- D008 TLNK marker。

## 9. 仍未解决、不能被描述为量产安全的边界

### 产品型号绑定

TLNK marker 只能证明是当前 BMS Telink 启动格式，不能单独证明“这个 BIN 一定属于 D008”。理想方案仍然是正式 firmware manifest：

```text
product_id
hardware_revision
mcu
afe
firmware_version
build_id
image_size
sha256
signature
```

由上位机和 Boot/OTA 两侧共同验证。

### 固件真实性

CRC16 是传输错误检测，不是密码学认证。D008 当前 BLE Security 关闭，现有 OTA 也没有完整的签名验证链，因此不能宣称防恶意固件、防回滚或防未授权升级。

### 低压 Flash

D008 当前 `APP_BATT_CHECK_ENABLE=0`。Windows 会在确认框显示当前包压和最低单体，方便操作人员判断，但本次没有擅自写死 LFP/NCM 通用低压阈值。量产前应按实际硬件电源保持能力确定 OTA 最低电压门槛，并在固件侧做最终保护。

### Extended64

D008 当前 MTU=23，所以 Auto 实际走 Legacy。Extend64 必须在提高 D008 MTU、FIFO/L2CAP buffer 后重新做真实硬件压力回归，不能仅因 Windows 能协商大 MTU就宣布可量产。

## 10. 推荐默认策略

当前 D008：

```text
用户默认：Auto
实际路径：Legacy 16B（MTU 23）
Extend64：保留为后续性能验证能力
```

不要为了速度先改 D008 MTU；先完成 Legacy 的断电、弱信号、多适配器和重复 OTA 回归。

## 11. 实板验收

至少覆盖：

1. Legacy 正常升级。
2. Auto 正常升级。
3. 相同软件版本、不同 Build ID 的升级。
4. OTA 中用户取消。
5. DATA 中途目标断电。
6. DATA 中途 PC 蓝牙关闭。
7. END 前后断链。
8. 弱 RSSI。
9. 非法 BIN。
10. 超 124 KiB D008 BIN。
11. D008 缺 TLNK marker BIN。
12. OTA_RESULT 丢失但新 Build ID 已启动。
13. 重刷同版本固件且 OTA_RESULT 丢失时，即使目标版本文本匹配，也不能单独判成功。
14. OTA_RESULT 丢失且版本/Build ID 都未改变，必须显示“未完全确认”，不能显示成功。
15. 升级后实时数据、保护参数、SOC、事件日志仍正常。
16. 冷启动多次。
17. 至少两种 Windows BLE 适配器。

## 12. 自动验证入口

```powershell
./bms-tool-windows/test-ota-protocol.ps1
```

然后运行现有 Windows 双版本构建/CI，确认客户版和内部完整版都能通过。
