# 有效配置、SOC 与协议读取的四项简化

基线为 `b76b8a763dfe0a6141f3fd3b2ad03be0690d62e5`，分支 `codex-bms-deep-simplification-20261016`。本轮按用户授权分四次提交；保留在线 SOC 曲线编辑、放电末端补偿、现有上位机协议和安全行为。四产品 `sources.txt` 仍是编译归属依据。

## 1. DVC 有效保护配置

核心整组查询直接调用 DVC 后端整组回读，不再逐字段经过语义窗口。后端集中解码，旧 `0x2850..0x285D` 字段共用相同规则、保持原读取范围。成功整组读取由 12 次（启用 SC 时 14 次）变为三个连续块：`0x70..0x73`、`0x59..0x5C`、`0x5E..0x61`（SC 时至 `0x63`）。全部通过底层 CRC/有限重试后才填写实读字段；任一块失败不发布部分值，总线静默门禁保持。SH 查询仍只用已验证的缓存，不增加 SPI。

依据为项目原件 DVC1124-2 DS V1.1 PDF 第 23 页（连续读及 CRC），RM V1.2 第 18–20、23–24 页（R/W 配置、阈值、延时）。这三个块不包含 RC 状态；不改寄存器写入、SC/OCD/OCC 恢复、默认参数和 OTA 策略。型号后缀、板级装配与实测 Rsense 的未决项见 [AFE 资料指南](AFE_REFERENCE_GUIDE.md)。

新增 host 场景用原 RM 公式核对全部字节编码、四种 shunt（含零和饱和）、三个块逐次失败、总线静默和 35-word 未覆盖字段保持；三个 D008 profile × HW_PROTECT 0/1 均通过。相关 `dvc_register_scenarios`、`dvc1124_fixed_policy`、`dvc_startup_protection`、`afe_hw_transaction`、D008 `modbus` 共五组既有 host 入口通过；D008 development 目标链接为 0 error/0 warning。最终固定提交与四产品资源结果在完成后追加。

## 验证边界

本轮只执行用户授权的相关 host 回归及四产品 sources/link/resources，不生成 BIN。host 与 ELF/MAP 不能替代实板 I2C/SPI 电气、物理保护恢复、MOS Gate/Vgs、Flash 掉电、低功耗/WDT、OTA 及现有 EXE/Android 与设备联调验收。
