> 历史记录：仅代表本文标注的日期和固定提交。当前实现见 [文档导航](../README.md)，当前验证以本轮报告为准。

# Monorepo 审核问题整改

日期：2026-10-05。修复基线：`fd50730db94c079a7e4dcc384d9ddf8e4ed7b6c7`。

## 修改范围

| 问题 | 修改与验证入口 |
|---|---|
| SH 通信电源 GPIO 关闭输出 | 正常初始化 CMNT_EN 写高、输入关闭、输出开启；执行实际 board_init 验证先写电平后开输出 |
| D011 MOS NTC 被禁用 | 恢复 TS4 capability；实际采样到保护输入映射和 TEMP_BREAK/MOS OTP/恢复动态测试 |
| D013 可选 MOS 传感器 | 产品固定 `mos_temp_required` 与采样 valid 分离；不支持时不误报，必需电池温度无效仍阻断 |
| 产品事实在共享代码 | 引脚、NTC 索引/能力、静态 AFE 输入和 HW 默认值移入三个 SH 产品头；寄存器组合留共享后端 |
| D013 仍显示 D011 | 硬件版本、序列号默认及 BLE 名称改为 D013，numeric wire ID 仍是 2 |
| 名为生产、实际开发构建 | `--production` 向全部 C 编译单元传递宏；模式/profile 独立输出；收据/manifest/资源记录配置；SH 诊断 bit2 补齐 |
| 工作流未启动 | 去除 job env 中非法的 runner context，改为 step 写 GITHUB_ENV；托管 Linux TC32 矩阵不依赖个人 Windows runner 在线 |
| D008 profile 歧义 | 开发保持 16S；生产工具及头文件双重拒绝隐式 profile；CI 覆盖 16S LFP、20S NMC、24S LFP |
| Flash 余量不足 | 生产链接硬性检查至少 8 KiB 余量，不扩大镜像槽或修改 OTA/Flash 边界 |
| baseline 不可解析 | 保留失联的原始 SHA，增加远端可获取的导入结果快照及对照 SHA，提供实际 fetch/cat-file 检查 |
| 产品文档旧路径/身份 | 更新四产品参考资料、软件保护与架构/生产构建说明 |
| 0x2E00 的 D008 值 | 与上位机 `26d978af2538438edf7a2c8557f2bc4c50ee6824` 的 `Shared/D008Parameters.cs` 核对：是接口 magic；只命名说明，数值和 wire scaling 不变 |

所有运行逻辑仍是裸机原架构；未改变 SDK 启动/ABI、通信外部地址、存储布局、PB5 安全电平或物理恢复条件。

## D008 Flash 预算处理

生产关闭调试后 D008 仍只剩 6,396 bytes，8 KiB 门禁正确拒绝。
为控制体积，生产模式仅对 `bms/core/*.c` 使用 `-Os`；SDK、AFE、Telink 平台仍保持 `-O2`，
TC32 ABI/启动/链接配置不变。构建收据记录该选择，静态分析读取同一实际命令。
调度最坏耗时仍需实板验证，不能由 host 断言替代。

## 验证

开发模式四产品 TC32 ELF 链接已通过，0 error / 0 warning。
首轮修正测试配置位置后，104 组 host 回归通过。
生产配置实测（代码提交 `5f11fe1`）：六个配置完整 TC32 ELF 链接，全部 0 error / 0 warning，均满足 8 KiB Flash 与 SRAM 门禁。

| 配置 | 预计含 CRC 镜像 bytes | Flash 余量 bytes | 扣除 3 KiB 主栈后的 RAM 余量 bytes |
|---|---:|---:|---:|
| D008 16S LFP | 116724 | 10252 | 4672 |
| D008 20S NMC | 116724 | 10252 | 4664 |
| D008 24S LFP | 116724 | 10252 | 4656 |
| D011 | 112116 | 14860 | 5496 |
| D013 | 111300 | 15676 | 5512 |
| D014 | 111956 | 15020 | 5496 |

CMake/CTest 2/2 通过；actionlint 通过。Make 实际展开的 98 条 D008 编译/汇编命令已检查：
所有 C 单元均有生产宏，只有 17 个公共 core C 单元追加 `-Os`，启动仍是 `MCU_STARTUP_8251`。
输入收据拒绝跨模式/产品复用，资源边界测试覆盖 8 KiB 下限两侧；软件温度保护同时在 `-O2` / `-Os` 下验证。
远端 GitHub Actions 必须以本分支最终提交的实际 run 为准，本地结果不替代远端状态。

CI Linux 编译器来自 Telink 官方 `telink-dev-tc32` 容器，固定 digest：
`sha256:b624c09e880cfc39d8d034062097b6bfeecab91c816fcc32fccd611eafe8d85a`。
编译器为 GCC 4.5.1.tc32-elf-1.5 / Telink TC32 version 2.0 build。
Windows 官方工具链入口保留；不同主机版本的产物不声称逐字节相同。

## 尚需外部证据

- D013 专属原理图/BOM：本次保留既有 4S/100µΩ、direct UART、heater/balance/MOS NTC 不支持的源码输入，没有把继承值宣称为硬件事实。
- D008/D011/D013 原始导入 commit 对象在远端不存在。新的可获取对照 commit 不冒充原始导入；精确导入前历史需要原本机上传这些对象。
- 四产品实板保护/恢复、通信电源/485 波形、Flash 掉电、低功耗及 OTA 验收仍需硬件；未生成 BIN、未烧录。
