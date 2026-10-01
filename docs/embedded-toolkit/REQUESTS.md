# 1. BMS PC 仿真器：脱离 MCU 验证全部保护逻辑

我要建立一个完全运行在 PC 上的 BMS 软件仿真与自动测试框架。

背景：
- 我的实际项目主要是裸机 MCU BMS。
- MCU 包括 STM32、Telink 等。
- AFE 包括 SH367309/SH36735x、BQ769xx 等。
- 我希望未来各种项目的软件保护逻辑、恢复逻辑、SOC、状态机尽可能共用。
- 核心业务逻辑不应该依赖具体 MCU、寄存器、GPIO、Flash 或 AFE 驱动。
- 我希望在没有硬件的情况下，在 Windows/Linux/macOS 上验证绝大部分 BMS 行为。

请完成以下工作：

1. 调研我现有 BMS 仓库中：
   - 电压保护
   - 过流保护
   - 短路
   - 温度保护
   - 充放电 MOS 控制
   - 恢复逻辑
   - SOC
   - Sleep
   - AFE fault
   - 通信故障
   - 参数管理
   的耦合情况。

2. 设计一个纯 C 的 platform-independent BMS Core：
   - 禁止直接访问 MCU 寄存器；
   - 禁止依赖 HAL；
   - 输入全部通过结构体；
   - 输出全部通过 command/event 结构体；
   - 所有保护逻辑可在 PC 编译；
   - 后续 MCU 只负责采样、执行输出和存储。

3. 建立 Host Simulator：
   可以模拟：
   - 单体电压变化
   - 总电压
   - 电流
   - 多路温度
   - MOS 温度
   - AFE fault
   - 充电器接入
   - 负载接入
   - 时间流逝
   - 通信异常
   - 掉电重启

4. 支持 Scenario 文件，例如：
   scenario.yaml / json：
   - 0~10s 正常
   - 10s 电流变为 100A
   - 10.5s 应触发 OCD
   - 20s 电流恢复
   - 满足恢复时间后 MOS 应重新打开

5. 每个保护必须具有自动化测试：
   - threshold - 1
   - threshold
   - threshold + 1
   - debounce 前
   - debounce 后
   - hysteresis
   - recovery
   - 多 fault 同时发生
   - 参数边界
   - 时间溢出

6. 输出 HTML/JUnit/Markdown 测试报告。

7. 接入 GitHub Actions。

8. 不要只写设计文档。
   请实际建立一个可以编译、运行、测试的 MVP。

9. 最后给出：
   - 架构图
   - 目录结构
   - MCU 如何接入
   - 后续迁移现有项目的方法
   - 哪些旧代码可以最终删除

目标：
以后新增 BMS 项目时，大部分核心安全逻辑不再重复开发。


# 2. BMS Hardware-in-the-Loop 测试平台

设计并实现一个低成本 BMS HIL 自动测试平台。

目标不是昂贵的专业 HIL，而是适合个人、小公司的自动化 BMS 测试设备。

硬件可以考虑：
- STM32 / ESP32
- USB
- DAC
- ADC
- IO
- CAN
- RS485
- UART

需要模拟：
- 电芯电压
- NTC 温度
- 电流信号
- Charger Detect
- Load Detect
- Wakeup
- Fault pin
- CAN/RS485 对端设备

PC 端开发控制软件，可以：

Test Case：
设置 Cell1 = 3650mV
等待 500ms
设置 Cell1 = 3700mV
检查 BMS 是否触发 OVP
检查 MOS
检查告警寄存器
等待恢复
检查状态恢复

要求：

1. 给出完整硬件架构方案。
2. 优先选择便宜、容易购买的器件。
3. PC 软件优先 Python。
4. Test Case 使用 YAML/JSON。
5. 自动生成测试报告。
6. 测试结果能够和 GitHub commit 对应。
7. 预留未来量产测试功能。
8. 可以最终做成我自己的 BMS 自动测试产品。

先实现软件框架和 Fake Hardware Backend，即便我目前没有 HIL 硬件，也能够开发并测试。


# 3. BMS 通用协议 SDK

分析我现有 BMS 项目、老上位机、新上位机和 Android App 中重复实现的通信协议。

建立一个独立仓库，例如：

bms-protocol-sdk

目标：
协议定义只维护一份。

需要支持：
- Modbus RTU
- UART
- RS485
- BLE transport
- CAN transport
- 参数读写
- 实时数据
- 告警
- 诊断
- K/B 校准
- 固件升级扩展

请设计：

protocol/
    spec/
    codec/
    transport/
    generated/
    tests/

协议数据定义尽量使用机器可读格式，例如 YAML。

从协议定义自动生成：
- C MCU codec
- Python library
- TypeScript library
- Kotlin/Android model
- Markdown 协议文档

要求：
1. 地址只定义一次。
2. 自动检查地址冲突。
3. 自动检查数据长度。
4. 自动生成 endian 转换。
5. 自动生成测试向量。
6. PC 与 MCU 共用同一组 golden test。
7. CI 检查协议兼容性。

实际实现一个 MVP，并迁移几组现有寄存器作为示范。


# 4. BMS 桌面版“工程师工具箱”

创建一个跨平台 BMS Engineer Toolbox。

要求至少支持 Windows，架构允许以后支持 macOS/Linux。

不要做成单纯漂亮 UI，而要解决实际工程问题。

功能：

## 串口
- 串口终端
- HEX ASCII
- 自动时间戳
- 自动分帧
- 保存原始数据
- CRC 检查

## Modbus
- 自动解析 RTU
- 地址解释
- 自动 CRC
- 请求生成
- 批量读写
- 周期采样

## CAN
- 导入 DBC
- CAN frame 解析
- 数据曲线

## BMS
- 单体电压
- 温度
- 电流
- SOC
- MOS
- fault
- 保护状态

## 调试
- 原始寄存器
- AFE raw
- 电流 zero offset
- K/B calibration
- 日志导出

## 分析
粘贴一段十六进制报文后自动输出：
- 帧结构
- 长度
- CRC
- 可疑错误
- 与另一条报文 diff

## 日志
CSV -> 图表 -> 自动异常分析。

要求：
先研究我的现有上位机功能，不要盲目重写。
将通用协议层独立出来。
完成可运行 MVP、README、安装包构建和 CI。


# 5. BMS Log 自动分析器

开发一个专门分析 BMS 日志的工具。

输入：
- CSV
- TXT
- UART log
- CAN log
- Modbus log

自动识别：
- 单体压差异常
- 电流突变
- 电流零漂
- 电压异常
- 温度异常
- SOC 跳变
- MOS 异常
- 保护触发
- 保护恢复
- AFE communication lost
- reset
- sleep/wakeup
- 通信丢帧
- 数据冻结

输出一个 HTML 报告：

Timeline
↓
异常事件
↓
相关数据窗口
↓
可能原因
↓
建议检查代码/硬件

要求：
支持用户配置规则。
不要把规则写死。

建立 rules.yaml：
例如：

current_zero_drift:
 idle_current: < 200mA
 duration: > 30s
 offset: > 300mA

第一阶段使用规则引擎，不需要机器学习。

以后可以发展成商业化 BMS Log Analyzer。


# 6. MCU Firmware Size Explorer

创建一个专门分析嵌入式固件代码体积的工具。

输入：
- ELF
- MAP
- OBJ
- HEX

支持 ARM GCC/Keil ARMCC/ARMClang，未来扩展 tc32。

输出：

Flash：
module A 12.3KB
module B 8.5KB

RAM：
module A 3.1KB

进一步分析：
- 最大函数
- 最大全局变量
- 重复字符串
- 不必要 printf
- dead code
- Debug code
- table
- library overhead

支持比较两个 commit：

commit A：
Flash 45KB

commit B：
Flash 52KB

增加：
+7KB

具体来自：
bms_soc.c +1.8KB
diagnostics.c +2.4KB
xxx...

最终输出 HTML 报告。

增加 GitHub Actions：
PR 如果 Flash 增加超过阈值则 warning。

把它设计为通用嵌入式开源工具，不绑定我的项目。


# 7. Embedded Map File 可视化工具

做一个独立的 Map File Analyzer Web/桌面工具。

输入 Keil/GCC map 文件后：

自动显示：
- ROM
- RAM
- stack
- heap
- module
- function
- symbol

生成：
Treemap
模块占比
Top Functions
Top Variables

还要支持：

两个 map diff。

例如：

before.map
after.map

输出：
Flash +5304 bytes

新增：
xxx_func +1220
xxx_table +2048

删除：
xxx...

目标：
让我以后每次重构后能够快速判断“为什么 Flash 变大了”。

要求实际实现，并准备公开 GitHub 的 README、截图、Release workflow。


# 8. MCU Firmware Architecture Scanner

开发一个静态代码分析工具，专门面向裸机 MCU 项目。

输入一个 C/C++ Firmware Repository。

自动生成：

## 目录
文件数量
代码行数
模块大小

## Dependency Graph
模块之间 include/call/global dependency。

## 风险
- 巨型 C 文件
- 巨型函数
- 过多 global
- extern 滥用
- circular dependency
- HAL 与业务逻辑耦合
- ISR 中复杂操作
- blocking delay
- magic number
- duplicated logic

## 输出
architecture_report.html

并生成：

architecture.json

使 AI/Codex 后续也可以读取。

目标：
做成一个真正可以用于旧嵌入式项目“体检”的工具。

先对我的一个 BMS 项目进行 dogfooding。


# 9. 嵌入式项目“AI 上下文生成器”

我频繁使用 Codex/ChatGPT 分析大型 MCU 工程，但每次 AI 都需要重新理解项目。

建立工具：

repo-context

运行：

repo-context scan .

自动分析：
- README
- 构建系统
- MCU
- AFE
- 文件树
- include graph
- important globals
- task loop
- ISR
- protocol
- protection
- storage
- SOC
- sleep
- firmware upgrade

生成：

AI_CONTEXT.md
ARCHITECTURE.md
MODULE_INDEX.md
SYMBOL_INDEX.json

同时支持：

repo-context diff HEAD~5 HEAD

告诉 AI 最近代码发生了什么。

目标：
以后我把仓库交给 Codex，它首先读取这些文件，就可以快速理解项目。

要求：
工具本身不要依赖特定 BMS 项目。
做成通用嵌入式项目工具。


# 10. BMS 参数配置代码生成器

开发一个 Parameter Schema 系统。

当前问题：
BMS 项目中参数往往同时存在于：
- C struct
- Flash
- Modbus
- 上位机
- Android
- 默认值
- min/max
- 文档

非常容易不同步。

我要实现：

parameters.yaml

例如：

over_voltage:
 type: uint16
 unit: mV
 default: 3650
 min: 3000
 max: 4500
 modbus: 0xD010
 persistent: true

根据它自动生成：

MCU：
bms_params.h
bms_params.c

PC：
Python model

Android：
Kotlin data class

Documentation：
parameters.md

同时：
- 检查地址冲突
- range check
- schema version
- migration
- CRC
- default values

实现 MVP，并演示 20 个典型 BMS 参数。


# 11. Firmware Fault Injection Framework

建立一个嵌入式故障注入框架。

要求正式版本默认完全关闭。

编译：

BMS_FAULT_INJECTION_ENABLE=1

后可以模拟：

- AFE I2C failure
- ADC fixed
- ADC noise
- current offset
- voltage overrange
- NTC broken
- EEPROM failure
- Flash CRC failure
- CAN timeout
- UART corruption
- watchdog timeout
- charger detect
- load detect

支持通过：
UART/Modbus/BLE
动态打开。

例如：

inject current_raw 0xFFF0
inject afe_comm_fail 10s

要求：
不破坏正式代码结构。
生产 build 必须能编译期完全删除注入代码。

同时生成自动测试案例。


# 12. BMS Firmware Fuzzer

构建一个适合 MCU BMS 的 fuzzing 系统。

不要 fuzz MCU 寄存器，而是 fuzz platform-independent 业务接口。

输入：
- cell voltage
- current
- temperature
- fault
- timers
- parameter values
- communication frames

寻找：
- overflow
- underflow
- illegal state
- MOS contradiction
- crash
- endless loop
- invalid SOC
- recovery logic bug
- state machine deadlock

使用：
libFuzzer / AFL++ / property-based testing
选择合理方案。

定义重要 invariant，例如：

0 <= SOC <= 100%
CHG fault active 时 CHG MOS 不应打开
严重 DSC fault 时 DSG MOS 不应打开
禁止数组越界
参数损坏不能导致危险输出

实际实现可运行 MVP。


# 13. BMS 自动文档生成系统

我要解决“代码更新了，协议/SRS/设计文档没有同步”的问题。

创建 Documentation Pipeline。

从源码和 schema 自动生成：

- Firmware Architecture
- Software Module Design
- Modbus Protocol
- Parameter Table
- Protection Matrix
- Fault Matrix
- State Machine
- Build Information
- Firmware Version
- Test Coverage

例如：

make docs

生成：

docs/generated/

同时生成 PDF/HTML/Markdown。

重点：
自动生成的部分和人工填写的认证内容明确分离，避免人工文档被覆盖。

为以后 ISO 13849 / IEC 60730 / IEC 60335 文档维护打基础。


# 14. BMS Release 工厂

设计并实现完整 Firmware Release Pipeline。

Git tag：

v1.3.0

自动：

1. 编译 firmware
2. 生成 bin/hex
3. 生成 map
4. 计算 CRC
5. 运行 PC unit tests
6. 运行 protocol tests
7. 运行 static analysis
8. 生成 firmware size diff
9. 生成 changelog
10. 生成 release manifest

manifest 示例：

product
hardware revision
firmware version
git commit
compiler
build time
CRC
Flash size
RAM size
protocol version
parameter schema version

最后生成：

release/
 firmware.hex
 firmware.bin
 manifest.json
 CHANGELOG.md
 TEST_REPORT.html

目标：
任何交付给客户的固件都能追溯到源码和测试结果。


# 15. 跨平台串口/RS485 调试神器

做一个非常轻量的串口调试工具，目标不是替代所有串口助手，而是专门解决嵌入式工程师调试痛点。

功能：

- HEX 发送接收
- 时间戳精确到 us/ms
- frame gap 自动分帧
- Modbus RTU 自动识别
- CRC 自动判断
- 差异比较
- 重复发送
- 脚本测试
- TX/RX 原始日志
- 自动检测异常帧

非常重要：

支持“双通道对比”。

例如：
Channel A = MCU UART
Channel B = RS485 receiver

同一次报文自动比较：

Byte 0 same
...
Byte 24 differs

这样可以直接解决类似我之前 MCU UART 正确，但经过 485 芯片后报文异常的问题。

做成 Windows 可执行程序。


# 16. Embedded Serial Logic Analyzer

基于廉价 USB UART/MCU 做一个“串口逻辑分析器”。

目标：
同时采集：
- UART TX
- UART RX
- RS485 DE
- GPIO
- optional ADC

生成统一时间轴：

12.001 ms TX start
12.034 ms DE high
12.566 ms UART last byte
12.580 ms DE low
...

自动分析：
- DE 提前关闭
- DE 过晚关闭
- byte gap
- baud mismatch
- framing anomaly

PC 端可视化。

先实现软件协议和模拟采集数据，不要求立即有真实硬件。


# 17. 嵌入式 Code Review Bot

开发一个针对 MCU Firmware 的 GitHub PR Review Bot。

每个 PR 自动分析：

- ISR 是否变复杂
- Flash/RAM 是否增加
- global variable 是否增加
- stack risk
- blocking call
- delay
- watchdog
- array bounds
- parameter migration
- protocol breaking change
- safety protection modification
- sleep/wakeup change
- bootloader risk

对于 BMS 项目额外检测：

修改 protection logic 时必须提示：
“是否增加 threshold/debounce/recovery test？”

输出 GitHub PR comment。

目标：
降低大量 AI 生成代码未经人工审核的风险。


# 18. AI Firmware Review Dashboard

开发一个本地 Web 页面，用于管理多个 Firmware Repository。

首页：

Repository
Branch
Last Commit
Build
Tests
Flash
RAM
Static Analysis
Open TODO
Risk

点击项目显示：

Architecture
Recent changes
Protection
Protocol
Storage
SOC
Sleep
Known Issues

可以调用 Codex CLI 或生成 Codex prompt：

“审核最近 5 个 commits”
“检查睡眠逻辑”
“检查 Flash 增长”
“生成测试”

重点：
它不是一个 IDE，而是 Firmware Engineering Dashboard。


# 19. 我的个人工程知识库

建立一个真正适合嵌入式工程师的个人 Knowledge Base。

自动扫描我授权的 GitHub repository。

抽取：

- MCU
- AFE
- protocol
- drivers
- bugs
- fixes
- calibration
- known hardware issues
- reusable modules

形成：

knowledge/
 chips/
 afe/
 protocols/
 bugs/
 projects/
 patterns/

例如：

SH367309/
 current_sampling.md
 protection.md
 known_issues.md

以后问：

“309 CADC 更新周期是多少？”
“上次 485 问题最终怎么定位？”

可以直接搜索。

要求：
不要只是简单把 README 拼起来。
需要有实体、标签、来源 commit、时间和仓库链接。


# 20. GitHub 多仓库项目驾驶舱

我有多个 MCU、Bootloader、上位机和 App 仓库，希望统一管理。

做一个本地 Web Dashboard。

自动读取 GitHub：

- repository
- branches
- commits
- Actions
- issues
- releases

按照“产品”而不是“仓库”组织：

例如：

Product D014

Firmware
Bootloader
PC Tool
Android
Protocol
Documentation

显示各组件版本是否匹配。

检查：

Firmware protocol v3
PC protocol v2

=> Protocol mismatch

目标：
解决多仓库产品版本混乱。


# 21. BMS Project Generator

创建：

bms-new-project

以后输入：

bms-new-project
 --mcu stm32f103
 --afe sh367309
 --cells 16
 --can
 --rs485
 --ble

自动生成项目：

platform/
afe/
bms_core/
protection/
soc/
storage/
protocol/
diagnostics/
tests/

并包含：
- CMake
- GitHub Actions
- cppcheck
- unit test
- coding style
- firmware manifest

项目模板必须保持简单。
禁止生成过度抽象的“企业级架构”。

目标：
新项目一天内进入业务开发，而不是复制一个旧项目继续积累历史包袱。


# 22. Firmware Configuration Studio

设计一个浏览器式 BMS 配置工具。

用户选择：

电池：
LFP

串数：
16S

容量：
50Ah

AFE：
SH367309

配置：

OVP
UVP
OCD
OCC
SCD
OTP
UTP

网页自动：

- 参数合法性检查
- 参数依赖检查
- 导出 JSON
- 导出 C Header
- 导出 Flash image
- 导出 Modbus 写入脚本
- 生成参数报告

以后可以作为客户配置工具。


# 23. BMS CAN/Modbus 协议转换网关

开发一个小型网关产品。

硬件：
ESP32 / STM32。

功能：

BMS Modbus RTU
↕
CAN
↕
MQTT
↕
BLE

配置由 JSON 控制。

例如：

Modbus 0xD000
→ CAN ID 0x301 byte0..1

目标场景：
旧 BMS 接新设备
储能
电动车
测试台

软件架构需要支持新增 protocol adapter。

先建立 PC 模拟版本证明架构。


# 24. BMS 数据记录器

设计一个便宜的独立硬件：

BMS Black Box Logger。

接口：
- CAN
- RS485
- UART
- optional BLE

持续记录：
- 时间
- 电压
- 电流
- SOC
- fault
- raw frames

存储到 SD 卡。

发生 Fault 时自动保留：

fault 前 5 分钟
+
fault 后 5 分钟

生成文件可以直接导入前面的 BMS Log Analyzer。

先开发文件格式、PC Reader、模拟 Firmware。


# 25. 面向副业的“嵌入式诊断工具套装”

不要从我的某一个项目出发，而是假设我要把自己的嵌入式经验产品化。

研究 GitHub 上现有嵌入式调试工具，设计一个可以逐步形成独立产品的：

Embedded Engineer Toolkit

第一阶段至少包含：
- Serial Analyzer
- Modbus Analyzer
- CRC Calculator
- HEX Converter
- Endian Converter
- Register Calculator
- CAN frame parser
- Map Analyzer
- Firmware size analyzer
- Binary diff
- Log analyzer

要求：

1. 功能全部能离线使用。
2. Windows 优先。
3. UI 简洁。
4. 工具之间数据可互通。
5. 不做复杂账号系统。
6. 优先真正工程师每天会使用的工具。
7. 架构允许以后加入付费 Pro 功能。

请：
- 调研竞品
- 确定 MVP
- 建仓
- 实现
- 自动测试
- 打包 Windows Release
- 写 README
- 准备产品截图素材
- 给出后续商业化路线

不要停留在“产品建议”，需要真正实现第一版。


# 26. 自动生成客户版 BMS 调试报告

开发一个工具：

输入：
- BMS log
- firmware version
- parameter dump
- fault record

自动生成一份面向客户/FAE的 PDF/HTML 报告。

报告包含：

Device Information
Firmware
Configuration
Fault Timeline
Voltage
Current
Temperature
SOC
Protection Events
Communication Events

最后生成：
Observed Facts
Possible Causes
Recommended Checks

注意：
“事实”和“推测”必须明显分离。

目标：
以后客户发给我 log，我不再人工整理 Excel。


# 27. BMS Field Diagnostic Package

建立一套“现场诊断包”。

Firmware 可以导出一个：

diagnostic_bundle.zip

包含：
- firmware version
- reset reason
- uptime
- parameters
- runtime state
- AFE registers
- fault history
- SOC state
- current calibration
- communication counters

PC 工具一键读取并生成 zip。

然后另一个工具可以：

bms-diagnose diagnostic_bundle.zip

生成 HTML 报告。

目标：
以后远程定位客户问题时，避免来回问：
“你再读一下这个寄存器。”
“你再测一下这个值。”


# 28. BMS 数字孪生 Digital Twin MVP

研究能否为 BMS 做一个简单 Digital Twin。

不是做高精度电化学模型。

输入：
- Cell voltage
- current
- temperature
- capacity
- internal resistance

模拟：
- charge
- discharge
- SOC
- voltage response
- protection event

可以连接我的真实 BMS firmware PC core：

Battery Model
→ Firmware Core
→ MOS output
→ Battery Model

形成闭环。

支持加速时间：
1 小时实际运行可以在几秒内模拟。

用来验证：
- SOC
- OCV
- protection
- sleep
- long-term behavior

先实现简化模型 MVP。


# 29. MCU 教学 / 自我学习系统

我希望真正掌握自己的项目，而不是代码都由 AI 写完后自己看不懂。

开发一个工具：

firmware-tutor

输入一个 GitHub Repository 后：

自动生成学习路径：

Day 1：启动流程
Day 2：main loop
Day 3：AFE
Day 4：保护
Day 5：通信
...

每个模块提供：
- 阅读文件
- 核心函数
- 调用关系
- 5 个问题
- 小练习
- 修改任务

例如：

“找到过压 debounce 的变量。”
“如果把恢复阈值改为 3.45V，哪些地方受到影响？”

支持记录学习进度。

首先对我的一个 BMS 项目 dogfood。


# 30. 个人嵌入式项目模板和工程规范

建立一个独立的：

embedded-project-template

它不是某个 BMS 项目，而是以后我所有 MCU 项目的基础模板。

包含：

src/
drivers/
platform/
app/
middleware/
tests/
tools/
docs/

提供：
- CMake
- GCC
- cppcheck
- clang-format
- unit tests
- host build
- GitHub Actions
- firmware version
- build metadata
- map analysis
- release packaging

原则：

1. 裸机优先。
2. 不强依赖 RTOS。
3. 不绑定 STM32 HAL。
4. 可以 STM32/Telink/其他 MCU 接入。
5. 不过度设计。
6. 人类工程师容易阅读。
7. AI 也容易分析。

请建立可运行示例。


# 31. 独立副业：Firmware Binary Inspector

开发一个面向嵌入式工程师的桌面工具：

Firmware Binary Inspector。

拖入：
.bin
.hex
.elf

自动显示：
- 文件格式
- size
- address range
- gaps
- vectors
- strings
- entropy
- CRC
- checksum

支持两个 firmware diff：

- 哪些地址变化
- 变化比例
- section diff
- 可视化 binary heatmap

支持自动判断：
“仅 version/CRC 变化”
或者
“大量程序区域变化”。

目标：
工具独立于 BMS，可以作为公开产品。


# 32. 独立副业：协议逆向辅助工具

建立一个 Protocol Reverse Engineering Helper。

用户粘贴大量 HEX frame。

工具自动：
- 按长度聚类
- 找固定字段
- 找递增字段
- 猜测 counter
- 猜测 timestamp
- 尝试 endian
- 尝试常见 CRC
- 找 request/response 对
- 显示 byte correlation

例如：
几十条未知 RS485 报文导入后，
工具告诉用户：

Byte 0：可能为 device ID
Byte 1：command
Byte 4~5：little endian value
最后 2 bytes：疑似 CRC16-Modbus

不需要承诺完全自动逆向。
核心是帮助工程师快速发现规律。


# 33. 独立副业：CRC/Checksum Explorer

创建一个比普通在线 CRC Calculator 强很多的离线工具。

输入：

01 03 D0 00 00 26 FC D0

自动尝试：
- CRC8 variants
- CRC16 variants
- CRC32
- checksum
- XOR

如果用户输入多帧：
自动寻找哪种算法能够同时匹配全部帧。

增加：
- endian
- init
- xorout
- reflect
- polynomial

目标：
专门解决“我拿到一套协议但不知道最后几个字节是什么校验”的问题。

实现 Windows/macOS/Linux CLI + GUI。


# 34. 副业产品 Landing Page + 自动发布

为我的 Embedded Engineer Toolkit 建立一个完整产品官网。

技术栈尽量简单。

页面：
- 首页
- Features
- Screenshots
- Download
- Documentation
- Changelog
- GitHub

要求：
- 响应式
- SEO 基础
- 静态网站优先
- GitHub Actions 自动部署
- 自动从 GitHub Release 获取版本
- 不建立复杂后端

同时生成：
- 产品 icon
- screenshot layout
- README
- release notes 模板

目标：
让我真正具备把工具公开出去的基础设施。


# 35. Embedded 工具 License 系统设计

如果我的工程师工具未来需要 Free/Pro 两个版本，请设计一个不过度复杂的 License 系统。

要求：

Free：
核心功能永久可用。

Pro：
批量分析
自动报告
大型日志
高级 diff
automation

设计原则：
- 不影响离线基本使用
- 不存储敏感代码
- 不需要长期联网
- 不使用复杂 DRM
- 尽量避免给个人开发者增加服务器成本

先完成 architecture/design + 一个本地 license prototype。

不要立刻把商业限制植入所有工具。


# 36. 我的“一个人嵌入式公司”基础设施

假设未来我要个人接 BMS / MCU 开发项目。

帮我建立一个完整但轻量的工程基础设施模板。

包括：

01_requirements
02_design
03_firmware
04_protocol
05_test
06_release
07_customer
08_issue

模板需要包含：

Requirement Template
Hardware Interface
Firmware Architecture
Protocol Spec
Parameter Table
Test Plan
Test Report
Release Note
Bug Report
Root Cause Analysis
Customer Delivery Checklist

并配套 CLI：

project create customer-x-product-y

自动生成目录和文档。

目标：
以后接项目时，不再从微信聊天 + 零散 Word + Git 仓库开始。


# 37. 自动生成 Firmware Change Impact Report

实现：

firmware-impact HEAD~10 HEAD

分析两个版本之间：

哪些 C 文件变化
哪些函数变化
哪些全局变量变化
哪些协议变化
哪些参数变化
哪些 protection logic 变化
哪些 ISR 变化
哪些 sleep logic 变化

自动输出：

Change
Impact
Risk Area
Required Test

例如：

Changed:
Protection_OvpCheck()

Required regression:
- OVP trigger
- debounce
- recovery
- charging MOS behavior

这会直接帮助我审核 AI/Codex 自动生成的代码。


# 38. “删除代码”专项优化任务

选择我的一个成熟 MCU/BMS 项目。

目标不是继续增加功能，而是尽可能安全地减少代码。

请：

1. 统计：
   - LOC
   - 文件数
   - 函数数
   - globals
   - Flash
   - RAM

2. 找出：
   - duplicated modules
   - unused abstraction
   - compatibility layers
   - dead features
   - debug leftovers
   - wrappers with no value
   - duplicated state
   - unnecessarily generic framework

3. 建立删除候选表。

4. 一次只执行安全的小批次删除。

5. 每次删除：
   - build
   - test
   - size comparison
   - behavior regression

目标：
代码最终明显减少，而不是“重构以后文件更多了”。

特别强调：
优先人类可阅读性，而不是抽象程度。


# 39. BMS Safety Property 自动验证

把 BMS 的关键安全要求转为机器可验证的 property。

例如：

PROPERTY 1
如果 OVP fault active：
CHG MOS == OFF

PROPERTY 2
如果 SCD active：
DSG MOS == OFF

PROPERTY 3
保护必须在指定 debounce 后发生，
不得提前，也不得无限延迟。

PROPERTY 4
恢复必须满足 hysteresis + recovery time。

PROPERTY 5
两个 fault 同时存在时，
清除其中一个不得错误打开 MOS。

实现 property-based tests。

大量随机生成：
voltage/current/temp/time/fault sequence。

寻找人工 testcase 不容易覆盖到的状态组合。


# 40. 统一我的整个 Embedded 开发体验

请研究如何把我的开发环境逐步统一成：

VSCode
+
CMake
+
GCC/ARMClang
+
OpenOCD/pyOCD
+
GitHub Actions
+
Python Tools

覆盖：
- STM32F0
- STM32F1
- Telink（在工具链允许范围内）

要求：

不要一次强制迁移所有生产项目。

建立：
- 标准 VSCode workspace
- tasks.json
- launch.json
- CMakePresets
- build scripts
- flash scripts
- static analysis
- format
- unit test

做到：

Ctrl+Shift+B
→ Build

Task:
Flash
→ 下载

Task:
Analyze
→ cppcheck

Task:
Test
→ PC tests

并写一套迁移指南，让旧 Keil 项目可以渐进迁移。
