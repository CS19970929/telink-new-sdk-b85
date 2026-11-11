# D008 TC32 跳转表修复与 BLE 验证

## 现场身份与故障

2026-11-11，目标 `A4:C1:38:A0:EB:83`，广播名称 `BT_d00823s002`，
身份读回 `D008-20261019` / `D008` / `V1.8`，Build ID 为 `efcd5ef5`，
构建标志 `0x000C`（production、dirty）。Windows 正式上位机 DLL 和 Android 均可建立
BLE 连接、发现服务并订阅通知；首次读取 `D120`、`D121` 两个 word 后约 4～5 秒断连。

分别读取的结果：`D000` 两个 word 正常；`D120` 一个 word 返回 `0x4253`；
`D121` 一个 word 无应答并断连。绕过故障窗口后，同一 Windows 上位机 DLL 连续五次
读取 `D000` 全部 63 word 成功，得到 23 个有效串位及约 75 V 总压。

归档 `firmware/production-dvc1124-23s-lfp/d008/` 的旧 BIN：

- 基线提交：`efcd5ef50f484d94ad91e2fc47f64fab6902aeef`，production `-Os`。
- SHA-256：`73c67b0d8c11daffc5ad8a7db1f066eb182b0f1d93d8d5b932dbec7fbe486db7`。
- `modbus_on_frame` 内联的实时寄存器 switch，在 `0x106D8..0x106DE` 以
  `index << 2` 取 32 位表项并写入 PC；表 `0x106E0` 实际只有
  `58 07 0D 26 2C 32 38 3E 47 4D` 十个压缩偏移字节。
- `D121` 对应索引零，取出的跳转地址为 `0x260D0758`，应有的业务结果是 `0x0001`。

旧归档 BIN 的哈希与 manifest 一致，Modbus、ATT、诊断和参数访问源码也与该提交一致。
未整片读回实板 Flash；dirty Build ID 本身不能证明实板每个字节与归档完全相同。

## 修复边界

`bms_tools/build.mk` 将 production 公共 core 的参数改为 `-Os -fno-jump-tables`，
避免 TC32 4.5.1 产生压缩表与绝对地址跳转不匹配的结果。开发、SDK、平台、AFE、启动、
预编译库、ABI、链接脚本和外部协议保持原体系。不是通过跳过 `D121` 掩盖故障。

同时修正量产构建的停顿取证默认值：production 默认关闭 `BMS_STALL_DIAG_ENABLE`；
开发取证默认选择不变，显式在 production 开启仍由原编译门禁拒绝。

`tests/fixtures/modbus_read.c` 增加实时窗口全部 66 种合法子区间、整块读取、
充放电符号和非法边界检查，期望含独立 CRC；这些 host 检查不替代 TC32 最终指令检查。

## 构建与主机验证

本轮执行四产品 production `sources --check`、`link`、`resources`；各目标通过。
D008 使用当前工作区 23S LFP 参数，关闭量产取证后先建立修复前 ELF 基线。

| 项目 | 修复前最新代码 | 禁用跳转表后 |
|---|---:|---:|
| D008 Flash 投影 / bytes | 119924 | 120036 |
| D008 Flash 余量 / bytes | 7052 | 6940 |
| D008 RAM 跨度 / bytes | 23300 | 23300 |
| D008 预留 3072-byte 栈后余量 / bytes | 6396 | 6396 |
| `modbus_on_frame` 内间接 PC 跳转数 | 3 | 0 |

另外三个产品的 Flash 投影：D011 116676、D013 112916、D014 116804 bytes。
均在 126976-byte APP slot 与 TLSR8251 32 KiB SRAM 范围内。
D008 余量低于 8 KiB，当前工具按既有策略报告提示，不绕过物理槽位门禁。

四产品真实 Modbus 入口回归通过；畸形帧检查通过 100000 帧、3395078 项断言；
量产构建/诊断门禁四项单测通过。畸形帧夹具显式关闭无硬件所有者的停顿取证，
与原地址回归夹具的边界保持一致。

上述 host 与 ELF 检查只证明软件及构建结果，不作为实板 OTA、MOS、保护、低功耗或掉电验收。

## 本次升级策略与实板记录

用户明确要求最新代码、最新参数且不兼容旧数据。目标旧格式为 CFG2/schema 3、
STB/schema 3、EV2/schema 3；当前格式为 CFG4/schema 4、ST5/schema 5、EV4/schema 4。
因此本次采用独占 `factory_reset`，保留当前源码的 23S LFP、10.0 Ah 默认参数；
重建参数、校准、身份后缀、SOC/循环和事件，MAC 与 SDK 校准区仍由原边界保护。
升级前协议可读取的软/硬件保护、校准、容量、均衡、实时状态和事件已在树外备份，
按用户要求不导入新设备状态。

最终 BIN/manifest、OTA 确认、新 Build ID、参数读回、扫描和重连结果在实板执行后追加。
当前段落不宣称这些尚未执行的操作已经通过。

本机证据根目录：`%LOCALAPPDATA%/CodexTemp/d008-jump-table-20261111/`；
旧现场只读记录位于 `%LOCALAPPDATA%/CodexTemp/bms-pc-ble-connect/20261111/`。
