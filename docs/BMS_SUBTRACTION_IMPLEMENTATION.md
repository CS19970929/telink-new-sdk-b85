# BMS 减法式重构实施记录

## 范围与行为边界

2026-10-01 用户确认实施前三批：低风险清理、SH 遗留实现退出、缩短阅读路径。四个 common 分支分别验证和提交；当前主工作区 d014-485-test 的未提交改动不参与。

保护阈值/时序、恢复、MOS、SOC 算法、协议、Flash schema 和低功耗行为保持。SH 休眠固定 UART 与 OWC idle 条件冲突、D011/D013 guard 启动资格差异留作独立问题。测试日志入口不是正式诊断；正式事件与诊断窗口保留。

## 第一批：低风险清理

- 删除无调用/注册的 test_log_app、test_log_balance_first 及废弃 app 声明/注释调用。
- SH 删除仅赋值、不读取的 s_balance_effective；硬件实际 balance mask 仍每次读回。
- ProductionInfor 收为 Modbus 内部 static；WriteProID_Default 与所有协议字段保持。
- 删除两个 KV 名称别名头，调用真实 bms_config_store / bms_state_store；存储实现、记录格式和错误传播不变。
- Time_T 字段未删除；第一批不改变结构布局。

## 验证方法

改前改后采用相同 TC32 O2/ABI、固定 source_order、MCU_STARTUP_8251，全部 C/汇编编译并直接链接 ELF/MAP，输出到用户级临时区，不调用 objcopy 或 BIN 包装。运行各产品既有 Host contracts；资源/源码/实板证据分别记录。

改前已知 Host 失败：D008 profile selection 的默认值预期与当前 profile 不一致；D014 production policy 的 debug LED 预期、sleep fixture 缺少 modbus_uart_tx_active 桩。不得通过修改产品默认值消除测试失败。

实板尚未验证，本次结果不关闭 HARDWARE_VALIDATION.md。

### 第一批验证结果（D014）

全部 100 个编译输入、ELF 链接通过，无编译诊断。text/data/bss：改前 109376/3980/6588 B，改后 109360/3980/6592 B。

Host：除前述改前失败外通过；D008 storage harness 去除别名头替换形成的重复头拼接后，掉电/参数/SOC/诊断用例通过。无固件 BIN、无烧录。

## 第二批：SH 遗留实现退出

删除 SH 树内 5 个 DVC C、5 个 DVC H 和不适用的 DVC source contract；删除空 SIF/mux 的 2 C/2 H，并同步 source_order。原 ELF 不含 DVC 后端/服务可达符号，DVC Modbus window 的 count 原为0，因此删除其恒不命中的分支；未知地址读值/写异常继续由原 fallback 处理。D008 的真实 DVC/SIF/mux 不受影响。

UART 在 app 初始化中直接调用 modbus_uart_init，IRQ 仅保留 SDK 与 UART 的原顺序。两个 PM 条件用固定 SH3673510_FIXED_UART_BLOCKS_PM=1 表达原固定 UART 非 OWC idle 的结果；没有开启任何新睡眠入口。sleep host 默认检查该门禁，同时在独立测试场景隔离门禁以保留原潜在 PM 故障路径注入；测试场景不是支持的产品模式。

### 第二批验证结果

全部 93 个编译输入、ELF 链接通过，无编译诊断。text/data/bss：改前 109376/3980/6588 B，当前 108592/3976/6580 B。Host 25 组，剩余失败：production_policy_check.py。D014 production policy 为改前既有失败。

## 第三批：缩短阅读路径

- 公共 bms_afe.h 不再按 include 顺序重映射名字；DVC 定义显式使用 dvc1124_backend_*。新增仅声明两种实际 backend 的私有 bms_afe_driver.h，guard/driver 引用它，应用引用公共头。没有新增调用层或运行时派发。
- 原 APP_SOC_IntEnhance_Ctrl 的输入组装移到 app.c 的采样任务附近，成为 static app_update_soc_from_sample；字段、故障 mask、时间戳、电流符号与 OpenWire 资格保持各板原值。SOC 算法本体不变。SOC 头中诊断/持久结构仍是实际公共参数类型，不强行隐藏。
- AFE profile owner 负责完整35-word BE payload 的授权、校验、persist/apply/readback/rollback和状态。Modbus 保留帧/地址/CRC与异常映射。授权检查仍在解析和存储之前，失败状态、session关闭规则及返回异常不变。
- 事务状态与错误 enum 移到 profile 头，数值与协议 metadata 不变。未合并不同生命周期的持久模块，也未新增 manager/service。

### 第三批与最终验证结果（D014）

- 默认配置：全部93个编译输入、ELF链接通过，无编译诊断。软件/硬件保护11、10、01、00四组合均完整编译/链接通过；非11组合只作BMS_PRODUCTION_BUILD=0的离线检查，不作为产品配置。
- Host contracts：25/26组通过，剩余改前失败：production_policy_check.py。工具单测26/26通过。 未修改相应产品默认值或工具命令来消除已有失败。
- AFE事务：16种成功/失败注入 × 4种既有apply-state共64组，改前/改后的异常码、状态、last-error、会话关闭、存储和外部调用顺序逐项一致；新增Host测试长期保留这64组。
- SOC输入组装：实际函数体和字段与改前源码逐字比较一致（仅static/函数名改变）。两个头文件include顺序均经TC32编译，生成的引用都指向公共guard的bms_afe_sample。既有SOC/OpenWire/存储Host场景通过。
- Cppcheck：实际产品C翻译单元，官方TC32平台/配置和编译器预定义宏；应用范围error/warning为0，style报告68→55，按id/message比较无新增报告。未套用MISRA addon，也未生成Excel。
- 产品目录C/H口径：85→70个文件，19786→15830物理行。text/data/bss：108628/3976/6580 B。源码行数与实际编译输入数分别记录。
- 同工具链ELF的_bin_size_：113488→112756 B；_ram_use_end_：0x845e2c→0x845e24；TLSR8251 __SRAM_SIZE=0x848000，预留0xC00栈后的地址空间余量5596 B。这是ELF/MAP资源证据，不是固件BIN或实板水位。

构建直接使用当前source_order与CLI同款TC32 O2/ABI和startup8251，并按原顺序链接Vendor静态库。历史IDE生成subdir.mk在改前已有旧绝对路径及不存在的bms_cold_kv_store.c/soc_kv_store.c，未将它作为本次构建入口。未执行会生成固件BIN的rebuild/打包/check-fw/verify，未执行OTA或刷板；ELF未加入正式发布Build ID打包流程，不能作为设备固件身份或发布验收。

数组/长度和协议帧边界保留原检查；事务授权仍早于解析和存储，apply/rollback失败路径有Host注入。Flash掉电、真实AFE通信失败、物理MOS、watchdog、低功耗恢复、实际保护时序及资源水位仍须按HARDWARE_VALIDATION.md实板验证。本次没有引入新的IRQ工作、Flash布局、协议地址或低功耗入口。
