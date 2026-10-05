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

## 验证

开发模式四产品 TC32 ELF 链接已通过，0 error / 0 warning。
首轮修正测试配置位置后，104 组 host 回归通过。
最终生产、资源与远端 CI 结果在提交验证完成后补充，不以开发链接代替。

CI Linux 编译器来自 Telink 官方 `telink-dev-tc32` 容器，固定 digest：
`sha256:b624c09e880cfc39d8d034062097b6bfeecab91c816fcc32fccd611eafe8d85a`。
编译器为 GCC 4.5.1.tc32-elf-1.5 / Telink TC32 version 2.0 build。
Windows 官方工具链入口保留；不同主机版本的产物不声称逐字节相同。

## 尚需外部证据

- D013 专属原理图/BOM：本次保留既有 4S/100µΩ、direct UART、heater/balance/MOS NTC 不支持的源码输入，没有把继承值宣称为硬件事实。
- D008/D011/D013 原始导入 commit 对象在远端不存在。新的可获取对照 commit 不冒充原始导入；精确导入前历史需要原本机上传这些对象。
- 四产品实板保护/恢复、通信电源/485 波形、Flash 掉电、低功耗及 OTA 验收仍需硬件；未生成 BIN、未烧录。
