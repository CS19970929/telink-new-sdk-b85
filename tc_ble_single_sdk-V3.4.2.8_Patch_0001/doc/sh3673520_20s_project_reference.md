# SH3673520 20S 蓝牙保护板项目完整参考文档

日期: 2026-06-08

本文以当前分支 `codex-sh3673520-20s-bms` 的源码实现为准，覆盖 20 串 SH3673520 蓝牙保护板的工程配置、Modbus 地址、BLE 通信参数、串口通信参数、AFE 驱动、AFE SPI 通信、AFE 寄存器、Flash 分区、AFE 参数读写和上位机对接。厂家 PDF `SH36735XX CV0.2B.PDF` 是芯片完整寄存器参考，本文只记录当前固件已经实现和对外暴露的内容。

## 1. 真相源文件

| 内容 | 文件 |
|---|---|
| 项目型号、串数、容量、默认版本、引脚 | `tc_ble_single_sdk/vendor/ble_sample/conf.h` |
| BLE 主流程、广播、连接参数、MTU 初始化 | `tc_ble_single_sdk/vendor/ble_sample/app.c` |
| BLE GATT/SPP 属性表与 Modbus over BLE 回调 | `tc_ble_single_sdk/vendor/ble_sample/app_att.c`, `app_att.h` |
| BLE UUID 定义 | `tc_ble_single_sdk/stack/ble/service/uuid.h` |
| Modbus RTU 协议和地址表 | `tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c`, `modbus_rtu.h` |
| 串口 Modbus 入口 | `tc_ble_single_sdk/vendor/ble_sample/modbus_uart.c`, `modbus_uart.h` |
| SH3673520 AFE 驱动 | `tc_ble_single_sdk/vendor/ble_sample/sh3673520_afe.c`, `sh3673520_afe.h` |
| AFE 抽象层 | `tc_ble_single_sdk/vendor/ble_sample/bms_afe.c`, `bms_afe.h` |
| Flash 分区 | `tc_ble_single_sdk/vendor/ble_sample/flash_store_cfg.h` |
| 冷区 KV、保护参数、蓝牙名、AFE 参数持久化 | `tc_ble_single_sdk/vendor/ble_sample/bms_cold_kv_store.c`, `bms_cold_kv_store.h` |
| SOC/运行 KV | `tc_ble_single_sdk/vendor/ble_sample/soc_kv_store.c`, `flash_kv32.c` |
| 事件日志 | `tc_ble_single_sdk/vendor/ble_sample/bms_event_log.c`, `bms_event_log.h` |
| Qt 上位机协议目录 | `tc_ble_single_sdk/vendor/ble_sample/BMSAssistantQt/bmsassistantqt/protocol.py` |
| Qt 上位机控制逻辑 | `tc_ble_single_sdk/vendor/ble_sample/BMSAssistantQt/bmsassistantqt/app_controller.py` |
| Qt 上位机 BLE/串口传输 | `tc_ble_single_sdk/vendor/ble_sample/BMSAssistantQt/bmsassistantqt/ble_transport.py`, `serial_transport.py` |

## 2. 当前项目配置

| 项 | 当前值 | 说明 |
|---|---:|---|
| `FD_BMS_TYPE` | `SH3673520_20S` | 20 串 SH3673520 项目 |
| `SeriesNum` | `20` | 电芯串数 |
| `CapacityFactory` | `100` | 出厂容量配置，沿用工程容量单位 |
| `BMS_AFE_TYPE` | `BMS_AFE_TYPE_SH3673520` | AFE 抽象层选择 SPI 新 AFE |
| 默认硬件版本 | `SH3673520-20S` | Modbus 产品信息默认值 |
| 默认软件版本 | `D100` | Modbus 产品信息默认值 |
| 默认序列号 | `20260604` | Modbus 产品信息默认值 |
| `AFE_ODC1` | `400` | AFE 相关默认阈值 |
| `AFE_ODC2` | `800` | AFE 相关默认阈值 |
| `CS_Res` | `2` | 电流采样电阻配置 |
| `CS_Res_Num` | `2` | 电流采样电阻数量配置 |

主要引脚:

| 信号 | GPIO | 说明 |
|---|---|---|
| `AFE_CTL_PIN` | `GPIO_PB6` | SH3673520 RESET/控制脚，驱动中用于复位 AFE |
| SH3673520 SPI CS | `GPIO_PD6` | 在 `sh3673520_afe.c` 内部定义 |
| SH3673520 SPI 组 | `SPI_GPIO_GROUP_A2A3A4D6` | SPI 引脚组，含 CS D6 |
| `OWC_TX_PIN` | `GPIO_PC2` | 当前串口 Modbus TX |
| `OWC_RX_PIN` | `GPIO_PC3` | 当前串口 Modbus RX |
| `RF_EN_PIN` | `GPIO_PD4` | RF 使能 |
| `AFE1_PRO_EN_PIN` | `GPIO_PD7` | AFE/保护相关使能 |
| `ADC_NTC_PIN` | `GPIO_PB4` | NTC ADC |
| `ADC_VBUS_PIN` | `GPIO_PB5` | 母线 ADC |
| `ADC_NMOS_PIN` | `GPIO_PC4` | MOS ADC |
| `ADC_BUSEN_PIN` | `GPIO_PD2` | 母线采样使能 |
| `ADC_EN_PIN` | `GPIO_PD3` | ADC 使能 |

蓝牙名称:

- 编译期默认 `DEV_NAME_STR = "BT_FD190126F03200046_007"`。
- 当前运行时会调用 `btname_init()`，优先使用冷区 KV 保存的蓝牙名后缀；没有保存值时使用默认后缀。
- 对外名称格式固定为 `BT_` + 后缀，最大总长度 25 字节，后缀最大 22 字节。

## 3. 通信总览

当前固件对外统一使用 Modbus RTU 帧格式，底层有两种传输方式:

| 传输 | 入口 | 说明 |
|---|---|---|
| BLE | Telink SPP/Nordic UART 兼容 UUID | 手机/PC 写入 BLE 特征，固件把 value 作为 Modbus RTU 帧处理，响应通过 notify 返回 |
| 串口 | UART DMA | 115200 8N1，收到完整 Modbus RTU 帧后调用同一个 `modbus_on_frame()` |

Modbus RTU 帧:

- 从站地址: `0x01`。
- 广播地址: `0x00`，固件接受写命令但不返回响应。
- CRC: Modbus CRC16，多项式 `0xA001`，初值 `0xFFFF`，低字节先发送。
- 寄存器字节序: 地址、数量、寄存器值均为大端。

支持功能码:

| 功能码 | 名称 | 支持范围 | 响应 |
|---:|---|---|---|
| `0x03` | Read Holding Registers | 数量 1..125 | 地址、功能码、字节数、寄存器值、CRC |
| `0x06` | Write Single Register | 单寄存器写 | 原请求回显 |
| `0x10` | Write Multiple Registers | 数量 1..123 | 地址、功能码、起始地址、数量、CRC |
| `0x7F` | Echo | 测试回显 | 原始帧回显 |
| 其它 | 不支持 | - | 异常响应 `func | 0x80`, code `0x01` |

## 4. BLE 通信参数

### 4.1 UUID 和方向

当前固件使用 128-bit UUID，外部对接按如下方向使用:

| 用途 | UUID | 操作 |
|---|---|---|
| SPP Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | 发现服务 |
| 请求特征 | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | 上位机写入 Modbus RTU 帧 |
| 响应特征 | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | 上位机开启 notify，接收 Modbus RTU 响应 |

实现注意:

- `uuid.h` 中宏名仍保留 Telink SPP 历史命名，`TELINK_SPP_DATA_SERVER2CLIENT` 实际值是 `...0002`，当前属性表把它配置成可写并绑定 `module_onReceiveData()`。
- `TELINK_SPP_DATA_CLIENT2SERVER` 实际值是 `...0003`，当前属性表把它配置成 notify，用于响应。
- 上位机应以 UUID 和实际属性权限为准，不要按宏名判断方向。

### 4.2 GATT 和链路参数

| 参数 | 当前值 |
|---|---|
| 广播间隔 | `ADV_INTERVAL_800MS` |
| 广播信道 | `BLT_ENABLE_ADV_ALL` |
| 发射功率 | `RF_POWER_P3dBm` |
| 地址类型 | Public address |
| BLE 安全 | `BLE_APP_SECURITY_ENABLE = 0` |
| OTA 服务 | `BLE_OTA_SERVER_ENABLE = 1` |
| 低功耗 | `BLE_APP_PM_ENABLE = 1` |
| ATT MTU | `MTU_SIZE_SETTING = 23`，有效单包 payload 20 字节 |
| 普通连接参数 | interval 10 ms, latency 99, timeout 4 s |
| OTA 连接参数 | interval 10 ms, latency 0, timeout 4 s |
| 响应 notify 分片 | 固件固定按 20 字节分片 |

BLE 请求限制:

- Qt 上位机 `ensure_safe_ble_length()` 当前限制请求帧不超过 20 字节。
- 因此 BLE 上写 `0x10` 多寄存器时必须控制数量，使整个 Modbus RTU 请求帧不超过 20 字节。
- 串口不受 BLE 单包限制，固件串口 RX/TX 缓冲为 256 字节级别。

BLE 响应流程:

1. 上位机向 `...0002` 写入完整 Modbus RTU 请求。
2. 固件 `module_onReceiveData()` 取 ATT value。
3. 调用 `modbus_on_frame(data, len, ble_rsp_buf, &rsp_len)`。
4. 如果有响应，调用 `notify_big_packet()`。
5. 固件通过 `...0003` 按 20 字节 notify 分片返回。

## 5. 串口通信参数

| 参数 | 当前值 |
|---|---|
| 波特率 | `115200` |
| 数据位 | 8 |
| 校验 | None |
| 停止位 | 1 |
| 流控 | None |
| 从站地址 | `0x01` |
| TX | `GPIO_PC2` |
| RX | `GPIO_PC3` |
| 收发方式 | DMA RX/TX |

源码中保留了 `8E1` 注释，但实际初始化调用是 `PARITY_NONE, STOP_BIT_ONE`，上位机也按 8N1 打开串口。

## 6. Modbus 地址总表

### 6.1 地址段概览

| 地址范围 | 读 | 写 | 内容 |
|---|---|---|---|
| `0x0000..0x0002` | 是 | 否 | MAC 地址，3 个 word，共 6 字节 |
| `0x0100..0x010B` | 是 | 是 | 蓝牙名后缀读取区，读 12 word |
| `0x0100..0x010F` | - | 是 | 蓝牙名后缀写入区，最多 16 word 入口，实际后缀最大 22 字节 |
| `0x1005` | 否 | 是 | 写 SOC |
| `0x1007` | 否 | 是 | 事件日志复位 |
| `0x1102` | 否 | 是 | 调试/工厂控制 |
| `0x1103` | 否 | 是 | 调试清理控制 |
| `0x2100..0x2140` | 是 | 是 | 保护参数 `g_tParam.protect` |
| `0x2200..0x221E` | 是 | 部分可写 | SH3673520 AFE 参数 |
| `0x2319` | 否 | 是 | 进入工厂模式 |
| `0xC002..0xC011` | 是 | 否 | 产品序列号 ASCII |
| `0xC012..0xC021` | 是 | 否 | 硬件版本 ASCII |
| `0xC022..0xC031` | 是 | 否 | 软件版本 ASCII |
| `0xC008` 起始读 | 是 | 否 | 兼容事件日志入口，只有起始地址等于 `0xC008` 时走日志读 |
| `0xD000..0xD03E` | 是 | 否 | 旧版实时数据结构映射 |
| `0xD100..0xD116` | 是 | 否 | 旧版故障/系统状态窗口 |
| `0xD120..0xD12A` | 是 | 否 | 新版实时状态窗口 |

### 6.2 MAC 地址

| 地址 | 内容 |
|---|---|
| `0x0000` | MAC byte0 << 8 \| byte1 |
| `0x0001` | MAC byte2 << 8 \| byte3 |
| `0x0002` | MAC byte4 << 8 \| byte5 |

数据来自 `g_stCellInfoReport.mac_public`。

### 6.3 蓝牙名称

| 项 | 值 |
|---|---|
| 读起始 | `0x0100` |
| 读 word 数 | 12 |
| 写起始 | `0x0100` |
| 写入口上限 | 16 word |
| 固定前缀 | `BT_` |
| 后缀最大长度 | 22 字节 |
| 总名称最大长度 | 25 字节 |
| 允许字符 | `0-9`, `A-Z`, `a-z`, `_`, `-`, `.` |
| 持久化 | 冷区 KV，key `0x4001..0x4006` |

写入建议:

- 使用 `0x10` 从 `0x0100` 开始写 ASCII 字节。
- 每个 word 按高字节、低字节放两个 ASCII 字符。
- 写入非空合法后缀后，固件保存到冷区 KV，并立即更新 GAP 设备名和广播响应数据。

### 6.4 产品信息

每个寄存器包含两个 ASCII 字节，高字节在前，低字节在后。

| 地址范围 | word 数 | 内容 | 当前默认值 |
|---|---:|---|---|
| `0xC002..0xC011` | 16 | 序列号 | `20260604` |
| `0xC012..0xC021` | 16 | 硬件版本 | `SH3673520-20S` |
| `0xC022..0xC031` | 16 | 软件版本 | `D100` |

注意:

- `0xC008` 同时是旧事件日志入口。只有当 `0x03` 请求的起始地址正好是 `0xC008` 时，固件才返回事件日志。
- 如果从 `0xC002` 连续读取产品信息，范围内的 `0xC008` 会按产品序列号字符处理。

### 6.5 控制寄存器

| 地址 | 写入值 | 行为 |
|---|---:|---|
| `0x1005` | `0..100` 建议 | 调用 `set_soc_param(value, 1, 1)` 写 SOC |
| `0x1007` | `0x0001` | 清空事件日志 |
| `0x1102` | `0x0003` | `Runtime_ReenterFactoryMode()`，重新进入工厂模式 |
| `0x1102` | `0x000A` | 设置 `deepsleep_en = true` |
| `0x1103` | `0x0001` | 清除当前测试、均衡、日志相关标志 |
| `0x2319` | 任意写入 | `enter_fac_mode(true)` |

### 6.6 保护参数 `0x2100..0x2140`

保护参数直接映射 `struct PRT_E2ROM_PARAS` 中从 `u16VcellOvp_First` 开始的连续 `u16` 字段。写入后固件调用 `SaveParam()` 保存到内部 Flash 冷区，并设置 `AFE_PARAM_WRITE_Flag = 1`。

| 地址 | 字段 |
|---|---|
| `0x2100` | `u16VcellOvp_First` |
| `0x2101` | `u16VcellOvp_Second` |
| `0x2102` | `u16VcellOvp_Third` |
| `0x2103` | `u16VcellOvp_Rcv` |
| `0x2104` | `u16VcellOvp_Filter` |
| `0x2105` | `u16VcellUvp_First` |
| `0x2106` | `u16VcellUvp_Second` |
| `0x2107` | `u16VcellUvp_Third` |
| `0x2108` | `u16VcellUvp_Rcv` |
| `0x2109` | `u16VcellUvp_Filter` |
| `0x210A` | `u16VbusOvp_First` |
| `0x210B` | `u16VbusOvp_Second` |
| `0x210C` | `u16VbusOvp_Third` |
| `0x210D` | `u16VbusOvp_Rcv` |
| `0x210E` | `u16VbusOvp_Filter` |
| `0x210F` | `u16VbusUvp_First` |
| `0x2110` | `u16VbusUvp_Second` |
| `0x2111` | `u16VbusUvp_Third` |
| `0x2112` | `u16VbusUvp_Rcv` |
| `0x2113` | `u16VbusUvp_Filter` |
| `0x2114` | `u16IchgOcp_First` |
| `0x2115` | `u16IchgOcp_Second` |
| `0x2116` | `u16IchgOcp_Third` |
| `0x2117` | `u16IchgOcp_Rcv` |
| `0x2118` | `u16IchgOcp_Filter` |
| `0x2119` | `u16IdsgOcp_First` |
| `0x211A` | `u16IdsgOcp_Second` |
| `0x211B` | `u16IdsgOcp_Third` |
| `0x211C` | `u16IdsgOcp_Rcv` |
| `0x211D` | `u16IdsgOcp_Filter` |
| `0x211E` | `u16TChgOTp_First` |
| `0x211F` | `u16TChgOTp_Second` |
| `0x2120` | `u16TChgOTp_Third` |
| `0x2121` | `u16TChgOTp_Rcv` |
| `0x2122` | `u16TChgOTp_Filter` |
| `0x2123` | `u16TchgUTp_First` |
| `0x2124` | `u16TchgUTp_Second` |
| `0x2125` | `u16TchgUTp_Third` |
| `0x2126` | `u16TchgUTp_Rcv` |
| `0x2127` | `u16TchgUTp_Filter` |
| `0x2128` | `u16TdischgOTp_First` |
| `0x2129` | `u16TdischgOTp_Second` |
| `0x212A` | `u16TdischgOTp_Third` |
| `0x212B` | `u16TdischgOTp_Rcv` |
| `0x212C` | `u16TdischgOTp_Filter` |
| `0x212D` | `u16TdischgUTp_First` |
| `0x212E` | `u16TdischgUTp_Second` |
| `0x212F` | `u16TdischgUTp_Third` |
| `0x2130` | `u16TdischgUTp_Rcv` |
| `0x2131` | `u16TdischgUTp_Filter` |
| `0x2132` | `u16TmosOTp_First` |
| `0x2133` | `u16TmosOTp_Second` |
| `0x2134` | `u16TmosOTp_Third` |
| `0x2135` | `u16TmosOTp_Rcv` |
| `0x2136` | `u16TmosOTp_Filter` |
| `0x2137` | `u16VdeltaOvp_First` |
| `0x2138` | `u16VdeltaOvp_Second` |
| `0x2139` | `u16VdeltaOvp_Third` |
| `0x213A` | `u16VdeltaOvp_Rcv` |
| `0x213B` | `u16VdeltaOvp_Filter` |
| `0x213C` | `u16SocUp_First` |
| `0x213D` | `u16SocUp_Second` |
| `0x213E` | `u16SocUp_Third` |
| `0x213F` | `u16SocUp_Rcv` |
| `0x2140` | `u16SocUp_Filter` |

### 6.7 SH3673520 AFE 参数 `0x2200..0x221E`

AFE 参数共 31 word。读写入口在 Modbus，持久化在内部 Flash 冷区 AFE key，应用时会把 `0x41..0x57` 镜像写入 AFE。

| 地址 | 偏移 | 属性 | 内容 |
|---|---:|---|---|
| `0x2200` | 0 | 只读 | 型号字，固定 `0x3520` |
| `0x2201` | 1 | 只读 | 串数，当前 `20` |
| `0x2202` | 2 | 读写 | 采样电阻，单位 uohm，默认 `2000` |
| `0x2203` | 3 | 只读 | 最近状态，`BSTATUS1 << 8 | BSTATUS2` |
| `0x2204` | 4 | 只读 | `FLAG1` |
| `0x2205` | 5 | 只读 | `FLAG2` |
| `0x2206` | 6 | 只读 | `FLAG3` |
| `0x2207` | 7 | 读写 | 预留 |
| `0x2208` | 8 | 读写 | AFE 配置寄存器 `0x41` 镜像，低 8 bit 有效 |
| `0x2209` | 9 | 读写 | AFE 配置寄存器 `0x42` 镜像，低 8 bit 有效 |
| `0x220A` | 10 | 读写 | AFE 配置寄存器 `0x43` 镜像，低 8 bit 有效，默认 `SeriesNum & 0x1F` |
| `0x220B` | 11 | 读写 | AFE 配置寄存器 `0x44` 镜像，低 8 bit 有效 |
| `0x220C` | 12 | 读写 | AFE 配置寄存器 `0x45` 镜像，低 8 bit 有效，默认 `0x3F` |
| `0x220D` | 13 | 读写 | AFE 配置寄存器 `0x46` 镜像，低 8 bit 有效 |
| `0x220E` | 14 | 读写 | AFE 配置寄存器 `0x47` 镜像，低 8 bit 有效 |
| `0x220F` | 15 | 读写 | AFE 配置寄存器 `0x48` 镜像，低 8 bit 有效 |
| `0x2210` | 16 | 读写 | AFE 配置寄存器 `0x49` 镜像，低 8 bit 有效 |
| `0x2211` | 17 | 读写 | AFE 配置寄存器 `0x4A` 镜像，低 8 bit 有效 |
| `0x2212` | 18 | 读写 | AFE 配置寄存器 `0x4B` 镜像，低 8 bit 有效 |
| `0x2213` | 19 | 读写 | AFE 配置寄存器 `0x4C` 镜像，低 8 bit 有效 |
| `0x2214` | 20 | 读写 | AFE 配置寄存器 `0x4D` 镜像，低 8 bit 有效 |
| `0x2215` | 21 | 读写 | AFE 配置寄存器 `0x4E` 镜像，低 8 bit 有效 |
| `0x2216` | 22 | 读写 | AFE 配置寄存器 `0x4F` 镜像，低 8 bit 有效 |
| `0x2217` | 23 | 读写 | AFE 配置寄存器 `0x50` 镜像，低 8 bit 有效 |
| `0x2218` | 24 | 读写 | AFE 配置寄存器 `0x51` 镜像，低 8 bit 有效 |
| `0x2219` | 25 | 读写 | AFE 配置寄存器 `0x52` 镜像，低 8 bit 有效 |
| `0x221A` | 26 | 读写 | AFE 配置寄存器 `0x53` 镜像，低 8 bit 有效 |
| `0x221B` | 27 | 读写 | AFE 配置寄存器 `0x54` 镜像，低 8 bit 有效 |
| `0x221C` | 28 | 读写 | AFE 配置寄存器 `0x55` 镜像，低 8 bit 有效 |
| `0x221D` | 29 | 读写 | AFE 配置寄存器 `0x56` 镜像，低 8 bit 有效 |
| `0x221E` | 30 | 读写 | AFE 配置寄存器 `0x57` 镜像，低 8 bit 有效 |

写入规则:

- `0x2200`, `0x2201`, `0x2203..0x2206` 为只读，驱动拒绝写入。
- `0x2202`, `0x2207`, `0x2208..0x221E` 可写。
- 写入 AFE 参数后固件调用 `bms_afe_param_commit_and_apply()`。
- commit 会逐 word 保存到冷区 KV key `0x5000 + index`，然后调用 `sh3673520_apply_params()` 写 AFE 配置寄存器。

### 6.8 旧版实时数据 `0xD000..0xD03E`

该窗口直接从 `struct stCell_Info` 起始字段连续读取 63 个 word。

| 偏移 | 地址 | 内容 | 当前实现说明 |
|---:|---|---|---|
| 0..19 | `0xD000..0xD013` | `u16VCell[0..19]` | 20 串电芯电压，单位 mV |
| 20..28 | `0xD014..0xD01C` | `u16VCell[20..28]` | SH3673520 发布时清零，保留 |
| 29 | `0xD01D` | `u16VCell[29]` | 主流程可能写入电池 NTC ADC mV |
| 30 | `0xD01E` | `u16VCell[30]` | 主流程可能写入 MOS NTC ADC mV |
| 31 | `0xD01F` | `u16VCell[31]` | 主流程可能写入 VBUS ADC mV |
| 32 | `0xD020` | `u16VCellMax` | 最大单体电压，mV |
| 33 | `0xD021` | `u16VCellMin` | 最小单体电压，mV |
| 34 | `0xD022` | `u16VCellMaxPosition` | 最大单体位置，1 基 |
| 35 | `0xD023` | `u16VCellMinPosition` | 最小单体位置，1 基 |
| 36 | `0xD024` | `u16VCellDelta` | 单体压差，mV |
| 37 | `0xD025` | `u16VCellTotle` | SH3673520 当前写入 pack mV，超过 `0xFFFF` 饱和 |
| 38..47 | `0xD026..0xD02F` | `u16Temperature[0..9]` | 温度，旧格式为 `(摄氏度 + 40) * 10` |
| 48 | `0xD030` | `u16TempMax` | 最大温度，旧格式 |
| 49 | `0xD031` | `u16TempMin` | 最小温度，旧格式 |
| 50 | `0xD032` | `u16Ichg` | SH3673520 当前实现写入充电电流 mA |
| 51 | `0xD033` | `u16IDischg` | SH3673520 当前实现写入放电电流 mA |
| 52 | `0xD034` | `SocElement.u16Soc` | SOC，% |
| 53 | `0xD035` | `SocElement.u16Soh` | SOH，% |
| 54 | `0xD036` | `SocElement.u16CapacityNow` | 当前容量 |
| 55 | `0xD037` | `SocElement.u16CapacityFull` | 满充容量 |
| 56 | `0xD038` | `SocElement.u16CapacityFactory` | 出厂容量 |
| 57 | `0xD039` | `SocElement.u16Cycle_times` | 循环次数 |
| 58 | `0xD03A` | `unMdlFault_First` | 一级故障 word |
| 59 | `0xD03B` | `unMdlFault_Second` | 二级故障 word |
| 60 | `0xD03C` | `unMdlFault_Third` | 三级故障 word |
| 61 | `0xD03D` | `u16BalanceFlag1` | 均衡标志 1 |
| 62 | `0xD03E` | `u16BalanceFlag2` | 均衡标志 2 |

### 6.9 旧版故障/系统状态 `0xD100..0xD116`

| 地址 | 内容 |
|---|---|
| `0xD100..0xD102` | 固定返回 0 |
| `0xD103..0xD104` | 最新一级故障记录，2 word |
| `0xD105..0xD106` | 最新二级故障记录，2 word |
| `0xD107..0xD108` | 最新三级故障记录，2 word |
| `0xD109..0xD114` | `System_ErrFlag` 起始错误标志，每 word 两字节 |
| `0xD115` | `SystemStatus.all & 0xFFFF` |
| `0xD116` | `SystemStatus.all >> 16` |

### 6.10 新版实时状态 `0xD120..0xD12A`

新版窗口用于上位机稳定识别当前协议，建议优先使用。

| 地址 | 内容 | 说明 |
|---|---|---|
| `0xD120` | Magic | 固定 `0x4253` |
| `0xD121` | Version | 固定 `0x0001` |
| `0xD122` | Pack voltage | `u16VCellTotle`，当前 16-bit mV 饱和值 |
| `0xD123` | Signed current | int16 编码，放电为负，充电为正 |
| `0xD124` | SOC | % |
| `0xD125` | Max temp | 旧温度格式 |
| `0xD126` | Min temp | 旧温度格式 |
| `0xD127` | MOS temp | `u16Temperature[MOS_TEMP1]` |
| `0xD128` | Max cell voltage | mV |
| `0xD129` | Min cell voltage | mV |
| `0xD12A` | Cell delta | mV |

### 6.11 事件日志

| 项 | 值 |
|---|---|
| 入口地址 | `0xC008` |
| 最大数量 | 100 word |
| 读取方式 | `0x03`，起始地址必须等于 `0xC008` |
| 清空方式 | 写 `0x1007 = 0x0001` |
| word 格式 | 高字节事件 ID，低字节间隔码 |
| 排序 | 最新优先 |
| 持久化 | 独立事件日志 Flash 区 |

事件 ID:

| ID | 名称 |
|---:|---|
| 0 | `NULL1` |
| 1 | `BMS_START_UP` |
| 2 | `BMS_SLEEP` |
| 3 | `BALANCE_OPEN` |
| 4 | `HEAT_OPEN` |
| 5 | `COOL_OPEN` |
| 6 | `VCELL_OVP` |
| 7 | `VBUS_OVP` |
| 8 | `CHG_OCP` |
| 9 | `VCELL_UVP` |
| 10 | `VBUS_UVP` |
| 11 | `DSG_OCP` |
| 12 | `CHG_UTP` |
| 13 | `DSG_UTP` |
| 14 | `CHG_OTP` |
| 15 | `DSG_OTP` |
| 16 | `VDELTA_OP` |
| 17 | `CBC_ERR` |
| 18 | `AFE1_ERR` |
| 19 | `AFE2_ERR` |
| 20 | `EEPROM_ERR` |

## 7. SH3673520 AFE 驱动

### 7.1 模块分层

| 层 | 文件 | 职责 |
|---|---|---|
| 业务调用层 | `app.c`, `sh367309_datadeal.c` 保留路径 | 周期采样、保护判断、数据上报 |
| AFE 抽象层 | `bms_afe.c`, `bms_afe.h` | 根据 `BMS_AFE_TYPE` 切换 SH367309 或 SH3673520 |
| SH3673520 驱动 | `sh3673520_afe.c`, `sh3673520_afe.h` | SPI 初始化、复位、寄存器读写、采样转换、参数持久化 |
| 冷区持久化 | `bms_cold_kv_store.c` | 保存 AFE 参数 word |

当前项目编译条件 `BMS_AFE_TYPE == BMS_AFE_TYPE_SH3673520`，因此 `bms_afe_*()` 会转调 `sh3673520_*()`。

### 7.2 AFE SPI 通信

| 参数 | 当前值 |
|---|---|
| SPI 模式 | Mode 3 |
| SPI 频率 | `SPI_CLK_500K` |
| SPI 引脚组 | `SPI_GPIO_GROUP_A2A3A4D6` |
| CS | `GPIO_PD6` |
| Reset/CTL | `GPIO_PB6` |
| ACK | `0xA5` |
| NACK | `0xFF` |
| CRC8 | 多项式 `0x07`，初值 `0x00`，无 xor-out |

复位流程:

1. `AFE_CTL_PIN` 拉低 2 ms。
2. `AFE_CTL_PIN` 拉高 5 ms。
3. SPI 发送复位命令帧。
4. 等待 10 ms。

SPI 命令:

| 命令 | 值 | 用途 |
|---|---:|---|
| Write | `0x01` | 写单个寄存器 |
| Read | `0x02` | 读连续寄存器 |
| Reset | `0x0B` | 软复位 |

写寄存器帧:

```text
MOSI: 0x01, reg, value, crc8(0x01, reg, value), dummy
MISO: 最后检查 ACK 0xA5
```

读寄存器帧:

```text
MOSI: 0x02, reg, len, dummy...
MISO: data..., crc
CRC 校验覆盖: 0xFF, 0x02, reg, len, data...
```

睡眠:

- `sh3673520_sleep()` 写 `SCONF1(0x40) = 0xAA`。

### 7.3 当前驱动使用的 AFE 内部寄存器

| 寄存器 | 地址 | 读写 | 当前用途 |
|---|---:|---|---|
| `SCONF1` | `0x40` | 写 | 睡眠控制，写 `0xAA` |
| Config mirror start | `0x41` | 写 | AFE 参数镜像起点 |
| `SCONF4` | `0x43` | 写 | 默认写 `SeriesNum & 0x1F` |
| `SCONF6` | `0x45` | 写 | 默认写 `0x3F` |
| Config mirror end | `0x57` | 写 | AFE 参数镜像终点 |
| `FLAG1` | `0x58` | 读 | 故障/状态标志，映射到 `0x2204` |
| `FLAG2` | `0x59` | 读 | 故障/状态标志，映射到 `0x2205` |
| `FLAG3` | `0x5A` | 读 | 故障/状态标志，映射到 `0x2206` |
| `BSTATUS1` | `0x5B` | 读 | 状态字节 1，映射到 `0x2203` 高字节 |
| `BSTATUS2` | `0x5C` | 读 | 状态字节 2，映射到 `0x2203` 低字节 |
| `TEMP1` | `0x5D` | 读 | 温度 1 起点，`TEMP1..TEMP4` 占 `0x5D..0x64` |
| `CUR` | `0x67` | 读 | 电流原始值 |
| `CELL1` | `0x69` | 读 | 单体 1 起点，`CELL1..CELL20` 占 `0x69..0x90` |
| `CPLUS` | `0x95` | 读 | Pack/C+ 电压原始值 |

采样批量读取范围:

- 起始 `0x58`。
- 结束覆盖到 `0x96`。
- 一次读出 flag、status、温度、电流、20 节电芯、C+。

### 7.4 采样换算

| 项 | 当前公式/处理 |
|---|---|
| 单体电压 | `cell_mv = raw * 5 / 32` |
| C+ 电压 | `cplus_mv = raw * 5 / 32 * 25` |
| 温度 | 按外部 10K NTC 旧表换算，输出旧格式 `(摄氏度 + 40) * 10` |
| 温度电阻 | `rt_kohm_x100 = raw * 1000 / (32768 - raw)` |
| 电流方向 | `CUR bit15 = 1` 判为放电，`0` 判为充电 |
| 电流幅值 | `raw & 0x7FFF` |
| 电流换算 | 当前按 `mA = 100 * CUR / (29127 * RSENSE)` 实现，`RSENSE` 来自 `0x2202` |
| Pack 电压发布 | 内部使用 `u32 pack_mv`，写入旧 `u16VCellTotle` 时超过 `0xFFFF` 饱和 |

注意:

- 旧 `struct stCell_Info` 注释中 `u16Ichg/u16IDischg` 标为 `A * 10`。
- SH3673520 当前驱动实际把换算后的毫安值写入 `u16Ichg/u16IDischg`。
- 上位机新版窗口 `0xD123` 只做 int16 正负编码，不重新换算单位。

### 7.5 发布到业务数据结构

`sh3673520_publish_to_cell_info()` 将采样结果发布到 `g_stCellInfoReport`:

- `u16VCell[0..19]`: 20 节单体电压 mV。
- `u16VCell[20..31]`: SH3673520 发布时清 0，主流程后续可能重用 `29..31` 放 ADC 工程值。
- `u16VCellMax/Min`: 单体最大/最小值。
- `u16VCellMaxPosition/MinPosition`: 1 基位置。
- `u16VCellDelta`: 最大最小压差。
- `u16Temperature[0..3]`: 4 路 AFE 温度。
- `u16TempMax/Min`: 温度最大/最小。
- `u16Ichg/u16IDischg`: 充/放电流当前实现值。

## 8. AFE 参数读写流程

### 8.1 启动加载

1. `sh3673520_param_load()` 先构造默认参数:
   - `0x2200 = 0x3520`。
   - `0x2201 = SeriesNum`。
   - `0x2202 = SH3673520_DEFAULT_RSENSE_UOHM`，当前默认 2000 uohm。
   - 配置镜像 `0x41..0x57` 默认 0。
   - `SCONF4(0x43)` 默认 `SeriesNum & 0x1F`。
   - `SCONF6(0x45)` 默认 `0x3F`。
2. 读取冷区 AFE 参数 key。
3. 只有当 key `0x5000` 中的型号字为 `0x3520` 时，才加载冷区保存的 31 个 word。
4. 强制修正只读字:
   - 型号固定 `0x3520`。
   - 串数固定当前 `SeriesNum`。

### 8.2 上位机写入

推荐流程:

1. 读 `0x2200..0x221E`，确认型号和当前配置。
2. 写需要修改的可写 word。
3. 如果 BLE 连接，单帧长度超过 20 字节时分多次写。
4. 固件收到写入后:
   - 更新 RAM 镜像。
   - 保存到内部 Flash 冷区 KV。
   - 写 AFE `0x41..0x57` 配置寄存器。
5. 再读 `0x2200..0x221E` 验证。

写失败条件:

- 写只读 index。
- index 超过 30。
- 冷区 KV 写入失败。
- AFE SPI apply 失败。

当前 `modbus_on_frame()` 在 AFE apply 失败时会调用 `System_ERROR_UserCallback(ERROR_EEPROM_STORE)`，但 Modbus 写响应仍按协议层写成功路径返回；上位机应增加写后读回验证。

## 9. Flash 分区

### 9.1 基础参数

| 项 | 值 |
|---|---:|
| Sector size | 4096 byte |
| Page size | 256 byte |
| Runtime sectors | 2 |
| Run/SOC KV sectors | 8 |
| Cold/protect KV sectors | 4 |
| Event log sectors | 8 |

### 9.2 512K Flash 布局

| 区域 | 起始地址 | 大小 | 用途 |
|---|---:|---:|---|
| Event log | `0x40000` | 32 KB | 事件日志 |
| Legacy btname anchor | `0x50000` | 4 KB | 历史蓝牙名单 sector 锚点 |
| Runtime | `0x51000` | 8 KB | runtime 状态 |
| Run/SOC KV | `0x53000` | 32 KB | SOC、运行参数 KV |
| Cold/protect KV | `0x5B000` | 16 KB | 保护参数、系统参数、蓝牙名、AFE 参数 |
| Historical soft protect anchor | `0x78000` | - | 历史 `PARAM_ADDR` 兼容锚点 |

### 9.3 1M Flash 布局

| 区域 | 起始地址 | 大小 | 用途 |
|---|---:|---:|---|
| Run/SOC KV | `0xB0000` | 32 KB | SOC、运行参数 KV |
| Cold/protect KV | `0xB8000` | 16 KB | 保护参数、系统参数、蓝牙名、AFE 参数 |
| Legacy btname anchor | `0xC0000` | 4 KB | 历史蓝牙名单 sector 锚点 |
| Runtime | `0xC1000` | 8 KB | runtime 状态 |
| Event log | `0xC7000` | 32 KB | 事件日志 |

### 9.4 2M Flash 布局

| 区域 | 起始地址 | 大小 | 用途 |
|---|---:|---:|---|
| Run/SOC KV | `0x1B0000` | 32 KB | SOC、运行参数 KV |
| Cold/protect KV | `0x1B8000` | 16 KB | 保护参数、系统参数、蓝牙名、AFE 参数 |
| Legacy btname anchor | `0x1C0000` | 4 KB | 历史蓝牙名单 sector 锚点 |
| Runtime | `0x1C1000` | 8 KB | runtime 状态 |
| Event log | `0x1C7000` | 32 KB | 事件日志 |

### 9.5 OTA 布局限制

`flash_store_cfg_layout_supported()` 会检查当前 Flash 容量和 OTA multi-boot 地址:

- 512K Flash 需要当前 multi-boot 地址为 `MULTI_BOOT_ADDR_0x20000`。
- 1M Flash 在部分芯片上不支持 `MULTI_BOOT_ADDR_0x80000`。
- 如果布局不支持，runtime/SOC KV/cold KV/event log base 返回 0，相关持久化不可用。

### 9.6 冷区 KV key 分组

| Key base | 内容 |
|---:|---|
| `0x1000` | 保护参数 |
| `0x2000` | 系统参数 |
| `0x3000` | 控制参数 |
| `0x4000` | 蓝牙名后缀 |
| `0x5000` | SH3673520 AFE 参数 |

关键映射:

- 保护参数使用 key `0x1001..0x1041`，对应 Modbus `0x2100..0x2140`。
- 蓝牙名后缀使用 key `0x4001..0x4006`，每个 key 保存 4 字节，总 24 字节空间。
- AFE 参数使用 key `0x5000..0x501E`，对应 Modbus `0x2200..0x221E`。

## 10. Qt 上位机

目录: `tc_ble_single_sdk/vendor/ble_sample/BMSAssistantQt`

当前上位机能力:

| 能力 | 实现 |
|---|---|
| BLE 连接 | `ble_transport.py`，使用 `BMSUUIDs` 中的 service/request/response UUID |
| 串口连接 | `serial_transport.py`，默认 115200 8N1 |
| Modbus 编解码 | `protocol.py` |
| 寄存器目录 | `RegisterCatalog` |
| AFE 参数读写 | `app_controller.py` + UI 调试页 |
| 保护参数预览 | `protectStart = 0x2100` |
| 实时状态读取 | 优先读 `0xD120` 新窗口，兼容 `0xD000` 旧窗口 |
| 产品信息读取 | 读 `0xC002/0xC012/0xC022` |
| 事件日志预览 | 从 `0xC008` 读取 |
| 蓝牙名设置 | 从 `0x0100` 写入后缀 |

上位机协议常量:

| 常量 | 值 |
|---|---:|
| `currentProjectSeriesCount` | 20 |
| `realtimeStatusStart` | `0xD120` |
| `realtimeStatusCount` | 11 |
| `legacyCellArrayStart` | `0xD000` |
| `legacyCellArrayCount` | 63 |
| `macAddressStart` | `0x0000` |
| `btNameStart` | `0x0100` |
| `productSerialStart` | `0xC002` |
| `productHardwareStart` | `0xC012` |
| `productSoftwareStart` | `0xC022` |
| `eventLogStart` | `0xC008` |
| `systemStatusStart` | `0xD115` |
| `protectStart` | `0x2100` |
| `afeParamStart` | `0x2200` |
| `afeParamCount` | 31 |
| `afeParamConfigOffset` | 8 |
| `socWriteRegister` | `0x1005` |
| `debugRegister1102` | `0x1102` |
| `debugRegister1103` | `0x1103` |
| `btNameMaxWriteBytes` | 10 |

BLE 和串口共用同一套 Modbus 编解码，区别只在 transport:

- BLE 单请求限制 20 字节。
- 串口可以发送更长请求。
- 响应解析均通过 `parse_response()`。

## 11. 常用调试帧示例

以下示例不含空格也可发送；CRC 为低字节在前。

读新版实时窗口 `0xD120` 11 word:

```text
01 03 D1 20 00 0B 3C FB
```

读 AFE 参数 `0x2200` 31 word:

```text
01 03 22 00 00 1F 0E 7A
```

写 SOC 为 80:

```text
01 06 10 05 00 50 9D 37
```

清空事件日志:

```text
01 06 10 07 00 01 FD 0B
```

进入工厂模式:

```text
01 06 11 02 00 03 6D 37
```

## 12. 维护注意事项

- Modbus 地址表和 Qt `RegisterCatalog` 必须同步维护。
- 如果将 BLE MTU 提升到 247，需要同时调整固件 notify 分片长度和上位机 BLE 请求长度限制。
- 如果修改 SH3673520 配置寄存器含义，需要同步更新 `0x2208..0x221E` 的说明和上位机标签。
- 如果修改 Flash 布局，需要重新检查 OTA multi-boot 区、SDK 系统区、运行 KV、冷区 KV、事件日志区是否重叠。
- 如果修正 SH3673520 电流单位，需要同步更新 `0xD032/0xD033`、`0xD123` 和上位机显示单位。
- AFE 参数写入建议始终做写后读回，因为当前 Modbus 写响应不能完全表达 AFE apply 失败。
## 15. 2026-06-08 协议完善补充

固件与上位机已经完成一次非硬件联调项完善，详细说明见 `doc/sh3673520_protocol_completion_20260608.md`。关键变化如下:

- `0xD120` 实时状态窗口升级为 v2，长度 `17` word，新增 32 位总压、mA 单位 32 位有符号电流和 AFE 应用状态。
- `0x2200` AFE 参数窗口默认读取 `32` word，其中 `0x221F` 是只读的最近一次应用状态；实际持久化参数仍为 `0x2200..0x221E`。
- SH3673520 `0x41..0x57` 参数镜像已按参考文档补齐默认值，避免把空白镜像写回 AFE。
- Modbus 写只读/越界 AFE 参数时返回异常 `0x03`，Flash/SPI 应用失败时返回异常 `0x04`。
- Qt 上位机已经显示 AFE 参数标签、访问权限、寄存器地址和 `AFE Apply Status`。
- SIF/BLE 业务上报电压已优先使用 32 位总压镜像换算，避免 20 串总压被 16 位 mV 饱和影响。
