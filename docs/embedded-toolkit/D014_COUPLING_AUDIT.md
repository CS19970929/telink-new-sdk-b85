# D014 当前耦合审计与第一批证据

源码基线：`25ced678dd31601cd96834c9f63901bd58e6c621`，工作分支 `d014-485-test`。开始时已有 `app.c`、`conf.h` 脏修改和未跟踪 references；本次未修改或提交它们。按用户选择，本批交付通用路线和可运行基础，不立即切换生产安全逻辑。

## 实际职责与状态所有者

路径前缀：`tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/`。

| 行为 | 当前入口/所有者 | 耦合与迁移范围 |
|---|---|---|
| cell/pack OV/UV | bms_sw_protection.c / s_filter / g_tParam.protect | 无寄存器访问，但测量/参数/故障是全局；先把状态与输入显式化 |
| charge/discharge OC | 同上，u16Ichg/u16IDischg | 100mA单位；200ms计数，不与AFE硬件profile合并 |
| 温度/NTC断线 | update_groups、bms_error、backend NTC换算 | 电池min/max与MOS分开；电池温度新fault需对应电流方向；active后按温度恢复 |
| short / HW flag恢复 | sh3673510_bms.c：service_short_recovery / service_hw_flag_recovery | AFE状态/LOADOFF/清flag/物理资格；不能按零电流简化迁移 |
| MOS | app请求→bms_afe_set_fets→guard→SH backend→control寄存器 | 请求、允许、HW锁存、共口反向/body-diode和实读状态不同；Host参考输出不是最终实机输出 |
| AFE/通信失败 | bms_afe_guard.c：inhibit_local / enter_failsafe_wait | 立即撤销授权、best-effort OFF、总线静默等待硬件WDT、有效样本资格；仍须实板证明物理关断 |
| SOC | SocEnhance.c、bms_soc_profile、config/cold/state stores | 200ms积分、g_soc_input与report、OCV资格和持久状态耦合；本批未执行SOC |
| Sleep | app_note_sleep_and_enter_deepsleep / app低电压/通信失败计时 | OTA/Flash/UART/PAD门禁、bms_afe_sleep、Runtime、cpu_sleep_wakeup；只许可判定适合Core |
| 参数管理 | param.h、g_tParam、bms_config_store、bms_cold_kv_store、bms_afe_hw_profile | 软件与AFE硬件参数独立；旧Flash/CRC/schema与协议是迁移约束 |
| 通信参数/诊断 | sci_upper/Modbus、bms_diag、btname_modbus、Windows工具 | 现有地址/单位/写事务必须保留；SDK要从唯一上位机分支核对 |

实机采样链：SH backend采样/状态发布 → 公共软件保护 → hardware恢复/flag与fault合并 → guard/后端仲裁 → AFE执行。source_order只证明文件成员，不等于所有条件分支启用；后续提取需逐调用核对。

现有重要语义：高保护 `value >= trip`、低保护 `value <= trip`；Third用Recover回差，First/Second按自己的阈值清除；Filter以10ms给定、ceil到200ms样本；电压/电流触发是leaky计数。温度方向消失不能把active故障直接清掉。

参数包含不代表保护已实现：旧SocUp/SocLow兼容字段在软件统一滤波模块未启用；SOC文件自己的告警路径不能混淆为同一保护Core。

## 第一批实现目录

```text
tools/embedded_toolkit/
  __main__.py            统一CLI
  common.py              路径与源身份
  compiler.py            GCC/Clang/MSVC原生Host编译
  simulator.py           JSON/YAML、FakeHardwareBackend、生产C桥接
  checks.py              12类边界/恢复场景与seed属性
  reports.py             HTML/JUnit/Markdown/JSON
  scanner.py             通用架构与AI上下文
  host/host_bridge.h     结构化输入/command
  host/host_bridge.c     Host执行壳与保守输出参考策略
  examples/ocd_recovery.json
tests/test_embedded_toolkit.py
.github/workflows/embedded-toolkit.yml
docs/embedded-toolkit/   需求、路线、审计
outputs/embedded-toolkit/ 正式本地生成证据（沿用仓库outputs约定）
```

## 证据层次

- 已执行：Windows原生MSVC编译生产软件保护模块，108组Host测试（含20,000样本seed属性），Fake HIL OCD场景；15个工具单元测试，包括错误输入、预期失败报告、HTML转义和YAML安全加载。Filter=0及UINT16_MAX的完整触发/恢复窗口均覆盖。未安装可选PyYAML的平台会跳过该单项。
- 已执行：source-order以及11个相关现有contract入口；未生成生产固件BIN或下载硬件。
- 已执行：当前项目扫描dogfood，报告分列全目录与source-order中的C/C++；数字为词法统计，不是编译器AST或资源报告。
- 已配置：Windows/Linux/macOS Actions和Clang ASan/UBSan；远端运行结果尚未取得。
- 未证明：MOS Gate/Vgs、真实短路/恢复、电流采样/温度准确度、硬件WDT关断、Sleep/Wake功耗与Flash掉电可靠性。

报告文件有输入场景hash和源文件hash。commit对应的工作树有未提交修改时必须同时看dirty和SHA256，不能把报告解释为纯commit产物。无需把几MB扫描JSON每次提交；保留代码和验证摘要，完整报告在本地outputs或CI artifact。

## HIL 后续硬件方案

```text
PC Python（scenario / commit / report）
  -> USB命令和统一时戳
  -> 采集控制MCU
       -> 独立/隔离电芯源模块 -> DUT VC0..VCn
       -> 电阻切换/NTC模拟 -> DUT TS
       -> 微小差分电压源/实际电流校准夹具 -> DUT shunt输入
       -> 受限IO -> charger/load/wake/fault
       <- 高阻采集MOS Gate/Vgs与AFE/告警
       <-> CAN/RS485/UART对端
```

第一阶段FakeBackend只有逻辑数据输入与command检查；下一阶段先真实只读协议backend，再接模拟源与独立测量反馈。电芯节点是叠加共模，不能把普通共地DAC通道直接当作各节浮动电芯。需明确隔离、输出限流、最大共模、插拔默认状态和独立量测，再选低成本器件；本批没有给出未经验证的采购BOM或价格。

所有真实执行backend应记录设置值/回读值/时戳/仪器身份/校准版本；量产模式用只读检测与经授权校准操作，避免场景任意写保护参数。生产注入功能默认编译删除，协议控制必须有独立测试会话，复用现有ID前先核对兼容性。
