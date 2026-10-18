# Issue #11：全项目减法重构执行记录

> 固定提交记录：本文是当时源码与验证的快照，不代表当前 HEAD；当前规则见 [CURRENT_RULES](CURRENT_RULES.md)。文中提交、行数与测试数字只用于追溯。

本报告对应 [GitHub Issue #11](https://github.com/CS19970929/telink-new-sdk-b85/issues/11)。这是一轮已实施的精简，不是待执行建议。结果固定到下述源码与本机证据；后续版本需重新验证。

## 基线、范围与证据边界

- 分支：`codex-bms-monorepo`，直接在原工作树开发。
- 起点：`9b92d3917415cfb9deddbbb7fd3f7f50a27b3ce3`。最终生产代码提交：`82fd6f49`；报告提交只补文档。
- 四产品清单保持不变：D008 101 项，D011/D013/D014 各 100 项。D008 实际应用 C 文件 38 个，SH 各 37 个，合并后共 45 个应用 C 文件；另核对全部 63 个应用头文件。SDK/启动/链接库只核对归属及依赖，没有为缩减数字修改。
- [源码、函数、结构字段与状态导航清单](ISSUE11_SOURCE_INVENTORY.json) 给出四套实际 `sources.txt`、SDK 文件 hash、108 个 C/H 文件 hash、include 依赖、函数内直接调用/宏调用、标识符引用位置、文件级声明及结构字段。它是词法导航，不把声明、条件分支、函数指针或宏引用次数当作 C 可达性证明。以下人工判断与目标编译共同限定其用途。
- 原有未提交文件 `bms/products/d008/bms_product.h` 保留原样，未纳入任何本次提交。其均衡最多 3 串、温度停止/恢复 1000/900、低压期限 60 秒在前后构建中完全相同。所有资源比较都是 development、`-O2`、dirty=1，不能当作干净量产镜像的资源或发布证明。
- 本机证据根目录：`C:/Users/Administrator/AppData/Local/CodexTemp/bms-issue11/`。未创建源码树内临时目录，未生成 BIN、烧录或执行 OTA。
- AFE 依据沿用 [原厂资料入口](AFE_REFERENCE_GUIDE.md) 与固定提交审查。本轮回看原始 `DVC1124-2参考手册_V1.2.pdf` 第 22/23/24/28 页的寄存器表，未变更芯片编码定义、阈值、总线顺序或寄存器语义。原件在 `references/afe/AFE_完整开发参考库_20261005/sources/`。

## 主调用链和状态所有权

`main/IRQ → main_loop → 到期采样 → AFE backend/guard → 软件保护及 MOS → features → SOC → 通信/持久检查点 → app_power`。已提交终止电源状态在循环入口优先处理；不是所有轮次都执行整条链。IRQ 保留 SDK 分派、DMA 交接及 SIF 发送，不把业务或 Flash 搬进中断。

| 模块（均有实际消费者） | 真正职责与消费者 | 本轮决定及状态保留依据 |
|---|---|---|
| `app.c` | 主循环调度，输入采样资格，SOC 快照；由 main/BLE 调度 | 保留到期 tick、唤醒标志、200 ms 调度和 pending；到期采样不等于新 ADC 转换 |
| `app_power.c` | 普通 Suspend、ACC、保护性关机；主循环及 SDK 回调消费 | 保留 region、ready、tick、committed；普通休眠可否决，保护性到期不被 BLE/OTA/Flash 永久推迟 |
| `bms_features.c` | 加热、均衡、断线诊断；guard 采样后运行，SOC/保护/协议消费 | 删除恒零板级充电源回退；保留请求/回读、温度回差、off_pending、诊断清理与可信度 |
| `bms_afe_guard.c` | 唯一公共输出授权和失联恢复入口 | 保留请求、命令、物理反馈、comm_inhibit、bus_silenced、有效新样本 streak，均非同义 |
| `bms_afe_hw_access.c` | Modbus 授权 token、分片与超时 | 删除 active 镜像和单调用超时包装；非零 token 表达会话，分片资格仍独立 |
| `bms_afe_hw_profile.c` | 硬件默认值、范围、事务/回滚及 effective | SH 直接按产品生成，删除先复制再覆盖；requested、实际量化、已持久化状态不合并 |
| `bms_config_store.c` | CFG2 缓存、校验、编码及当前 schema 3 更新 | 保留 ready、user_valid、needs_save：加载、语义资格、待持久化含义不同；既有 previous 缓冲早已删除，不重复计功 |
| `bms_parameters.c` | 启动存储资格与保护参数发布 | 保留 protection_valid/storage_startup_valid；在线提交不能消除启动失败门禁 |
| `bms_parameter_access.c` | 外部参数地址、校准和序列号分片 | 删除一层授权转发；保留 staging、generation、timeout、预检及错误码 |
| `bms_state_store.c` | SOC/循环 State journal | 删除 attempted 和第二份编码数组；pending 与成功提交 cache 必须分开 |
| `bms_event_log.c` | 历史事件、重复次数、去重与 checkpoint | 删除 attempted；dirty、last_failed、event_latched、merge_ready 和时钟余量各有独立含义 |
| `storage_record.c` | CRC、序号、commit-last、轮转及恢复 | 全部保留；掉电一致性、序号回绕和 IO_ERROR/空区分不可省略 |
| `bms_soc.c` | 时间积分、显示、OCV、端点、循环 SOH | 删除本轮时间暂存和恒真公共参数；保留前帧方向、余数、估算/显示、端点资格、诊断电压隔离 |
| `bms_soc_eta.c` | ETA 滤波、稳定期、方向、置信度 | 删除 eta_valid 镜像；协议诊断仍由 eta_state 导出原字段 |
| `bms_sw_protection.c` | 独立三级软件保护和恢复计数 | 删除未使用 FILTER_COUNT 宏；各级/各保护计数及 Third 物理恢复门禁保留 |
| `bms_state.c` | 协议报告、错误计数、故障历史和查表 | 保留外部地址布局、有效串位和历史统计，不把 61001 槽计入有效数据 |
| `modbus_rtu.c` | UART/BLE 共用 parser、地址窗口、长度/CRC/广播及事务预检 | 全部保留；读写分派不是无意义包装，平台不能绕过公共校验 |
| `btname_modbus.c` | BLE 名称校验、后缀持久化及应用 | 删除单调用保存包装；验证、保存失败、应用顺序不变 |
| `bms_crc.c` | Modbus CRC16 | 保留；不能与 AFE CRC8 或 journal CRC32 合并成泛化层 |
| `bms_diag.c` | 只读 RAM 快照、MOS/失败历史、协议窗口 | 删除已穷尽头字段后的重复范围条件；保留历史/实时快照、序号和冻结状态 |
| `bms_debug_log.c` | 默认关闭的 RAM 日志及只读窗口 | 保留，不写 Flash、不增加发送或低功耗门禁 |
| DVC `dvc1124.c` | I2C/CRC、ADC、保护编码、均衡、断线与启动零点 | 删除死缓存/镜像；所有实际寄存器访问、重试、读回、ADC 新鲜度和故障恢复证据保留 |
| DVC `dvc1124_bms.c` | 换算、保护合并、SC/OCD/OCC 恢复、FET 仲裁 | 保留反向电流 tick、硬件 pending、命令变化抑制；零电流不证明负载/充电器已移除 |
| DVC `dvc1124_boot.c` | 接口上电、wake、初始化和项目配置 | 保留 pending：失败后必须重试，不能当作一次初始化缓存删除 |
| DVC `dvc1124_config_service.c` | 语义/原始只读窗口及 effective 寄存器读取 | 保留真正 effective 所有者；删除头文件未使用通信别名，不改变 enum/地址 |
| DVC `dvc1124_feature_backend.c` | feature 到 DVC 的均衡和断线数据映射 | 保留成功请求缓存及断线 cell 数组，不能用驱动“尝试请求”代替成功请求 |
| DVC `dvc1124_special.c` | COTF 锁存、W0C、自清命令专用访问 | 保留特殊语义，不能用普通寄存器写包装替代 |
| SH `sh3673510_bms.c` | 三产品采样、故障恢复、物理释放检测、输出 | 删除无消费者旧延时宏；保留 LOADOFF/C+ 互斥检测、有效性、short latch、clear pending、清除失败 |
| SH `sh3673510_control.c` | 配置、量化、读回、FET/sleep/wake | 保留实际 SCONF6 构造与校验；删除头文件无消费者固定合成宏 |
| SH `sh3673510_feature_backend.c` | 均衡续期、读回、断线分阶段 | getter 直接返回原硬件读函数；保留 requested、refresh_tick、断线相位 |
| SH `sh3673510_diag.c` | backend 缓存诊断 | 保留，不额外触发 SPI，不能绕过 bus silence |
| SH `sh3673510_ntc.c` | 10K-3435 表 | 保留原表；不能用近似算法换 ROM 来改变温度 |
| SH `sh3673520.c` / `sh3673520_port.c` | 芯片协议/CRC 与 SPI/GPIO 端口 | 保留超时、重试、通信统计及复位观察接口 |
| 平台 `main.c` | MCU 初始化和 IRQ | 保留官方启动、IRQ 路由和 ABI |
| 平台 `app_ble.c` / `ble_ota.c` | SDK 连接/广告/OTA 回调 | 删除只写时间/长度和不可达终止状态；实际广告长度、OTA latch、连接及授权撤销保留 |
| 平台 `app_att.c` | GATT 表及 BLE RTU 分片 | 删除旧注释代码和重复 include；表、callback、20-byte 通知行为不变 |
| 平台 `bms_board.c` | 物理产品能力及 GPIO | 删除两个恒零充电源实现；保留 D011 PB5 LOW、D013 禁 heater/balance、D014 无 heater |
| 平台 `modbus_uart.c` | DMA 缓冲所有权、DE 完成、接收恢复 | 删除 timeout_seen；active 关闭保证超时仅处理一次；dma_done、uart_done、最短线时间仍独立 |
| 平台 `bus_mux.c` / `sif_send.c` | D008 OWC/UART 复用及 SIF ISR 双缓冲 | 删除 SIF_VERSION 死宏；保留通道/缓冲所有权、切换和发送时序 |
| 平台 `bms_storage_platform_telink.c` | Flash 分页、写保护、读回与失败退避 | 删除重复 include/未用 PAGE 宏；Flash session、校验、失败计时和诊断保留 |
| 平台 `bms_runtime_diag.c` / `bms_stack_monitor.c` | 诊断采集与有界栈扫描 | 保留 RAM/栈观测，不能把 MAP 预算当作实际最坏栈 |

产品目录无业务实现副本；实际能力、引脚和 profile 都保留。工具/测试按真实接口更新，未删场景以让测试变绿。同步修正组织说明中残留的“容量学习”和 D013 4S 旧描述，当前分别为循环 SOH 与 10S 编译默认。

## 已实施删除与等价精简

行号、定义、调用及字段完整列表见清单；删除项的原位置以 `git show 9b92d391:<path>` 为准。

| 分类 | 路径与删除项 | 读写/消费者证据、收益与回归 |
|---|---|---|
| 确认可删 | `app_ble.c` 的 `tbl_advDataLen/tbl_scanRspLen/advertise_begin_tick/latest_user_event_tick`；`app.h/ble_ota.c` 对应声明/赋值 | 四产品生产 TU/SDK 无读取，广告实际使用数组 sizeof；删除 4 个全局对象及无效 clock_time 调用 |
| 确认可删 | `sendTerminate_before_enterDeep` 和 `task_terminate` 中相关分支 | 初值 0，唯一赋 2 在“等于 1”分支内，无置 1 路径；实际电源策略仍在 app_power |
| 等价精简 | `modbus_uart.c:s_rs485_tx_timeout_seen` | 入口要求 active，超时同次复位外设并清 active 后返回；新发送才重新激活；UART 所有权/超时场景 |
| 确认可删 | `bms_board_charge_source_present` 的 DVC/SH 两实现及声明 | 两实现恒零，唯一消费者是 AFE 失败 fallback；feature 直接采用成功的 AFE present，保留未知即否；四产品 heater/balance 场景 |
| 确认可删 | `dvc1124.c:dvc_applied_protection_t/s_applied/DVC_QUANT_* /dvc_note_quant/dvc_current_from_sense_uv` | 只写无生产读取；真正 effective 在 config_service 读寄存器；删除 15 字段缓存和仅为其服务的反算/actual 输出 |
| 等价精简 | `dvc_voltage_delay_code/dvc_linear_delay_code/dvc_current_to_oc1_code/dvc_current_to_oc2_code` | 删除 actual 输出，保留返回编码与范围；983,049 个新旧比较一致，三 profile 寄存器轨迹及总线故障场景 |
| 确认可删 | snapshot 的 `chip_version/write_addr/pack_mv/load_mv/cell_mv[24]/gp_code[6]/die_temp_x10`；openwire result 的 `pack_mv`；config 的 `battery_ntc_gp/mos_ntc_gp` | 只写/死链；保留芯片就绪读、报告电压、61001、温度、NTC资格和真正断线 cell 数组；ADC/NTC/断线回归 |
| 确认可删 | `dvc1124_config_service.h` 的未用 `COMM_*` 别名（保留 REG_BASE/COUNT）；`DVC1124_ReadRegisterField/RegPersistentConfigMask` | 全仓无生产/测试消费者；保留 CFG enum、原始寄存器定义、已使用特殊访问语义及安全写入口 |
| 等价精简 | `bms_afe_hw_access.c:s_active/access_expired` | token 0 表示关闭，open 只生成非零 token；保留超时、分片、CRC、撤销和回绕跳零 |
| 确认可删 | `sensitive_factory_write_allowed/btname_save_suffix_to_store` | 纯转发，调用者直接调原授权/Config 所有者；敏感写错误码、保存失败语义不变 |
| 等价精简 | State 的 `g_bms_state_attempted`、Event 的 `attempted` | last_failed 只能在已尝试失败路径为真；删除重复 boolean，保留失败时刻、退避及强制保存仍受退避约束 |
| 等价精简 | `bms_state_save:previous[44]` 及第二次 encode | 按 soc/dsg/cycle/soc_revision/ota_update_id 全部持久字段比较，避免比较结构 padding；44-byte 栈数组消失，固定 payload 不变 |
| 等价精简 | `g_soc_interval_32k`、公开 `set_soc_param` 的 `sync_display`、内部 ETA `eta_valid` | 单轮时间传参；所有公共调用原为 1；有效性等价 eta_state==VALID。私有 `soc_apply_real_value` 的 0/1 语义保留；SOC/ETA/端点/回绕/存储回归 |
| 等价精简 | SH hardware profile 默认构造 | 所有软件默认字段随后被 SH 产品值覆盖；只在 DVC 构造该表，去掉 SH 临时 130-byte 结构及调用，保留全部最终字段；默认矩阵/原厂范围/故障回滚 |
| 确认可删/等价精简 | SH getter 局部搬运；`SH3510_OCD_RELEASE_FILTER_10MS/SH3673510_BOARD_SCONF6_VALUE/BMS_SW_PROTECTION_FILTER_COUNT/SIF_VERSION/FLASH_STORE_PAGE_BYTES`；旧 GATT 注释；重复 include；诊断重复范围条件 | 保留真正参数、寄存器位和现行接口；均衡空指针/失败输出、协议及六配置链接检查 |

已删除 8 个函数名、9 份函数定义（板级函数两个产品分支），10 个文件级静态/全局对象，27 个内部结构字段（含已删 s_applied 的 15 字段）。没有删除生产文件或新增状态所有者。计数口径是源码对象/字段数，不是各 profile 预处理后的实例数，也不是 RAM 节省字节数。

生产 C/H：增加 66 行、删除 481 行，净减少 **415 行**；测试净增加 15 行，用于维护原有验证。State 第二份编码栈数组 44 字节、SH 默认构造临时结构 130 字节已去掉；不据此声称系统最坏栈下降相同字节数，实际函数栈与中断嵌套仍需工具/实板测量。

关键删除点的固定基线定位如下（全部行号属于 `9b92d391`，不是最终源码；同名局部变量不在删除范围）。公共 core/platform 项影响四产品，DVC 项只影响 D008 三 profile，SH 项影响 D011/D013/D014。

| 基线路径 | 定义/消费者行号 |
|---|---|
| `bms/platform/telink/app_ble.c` | 广告长度 94/97（赋值 214/222），广告/用户 tick 101/132（赋值 187/254/622），终止状态 103（分支 176/178） |
| `bms/platform/telink/modbus_uart.c` | timeout_seen 91；消费/复位 143/146/206/343/477 |
| `bms/platform/telink/bms_board.c`、`bms/app/bms_features.c` | 恒零接口 33/95；唯一业务消费者 features:91 |
| `bms/afe/dvc1124/dvc1124.c` | applied 类型末尾/对象 127/129，反算 659，note_quant 720；四编码函数 609/637/669/694 |
| `bms/afe/dvc1124/dvc1124.h` | NTC 配置 44/45，snapshot 56 起，死 inline 231 |
| `bms/afe/dvc1124/dvc1124_reg.h` | 死寄存器 mask inline 498 |
| `bms/core/bms_afe_hw_access.c` | active 13，超时包装 41；消费 61/68/158 |
| `bms/core/bms_parameter_access.c` | 授权包装 42；消费 195/201/219 |
| `bms/core/btname_modbus.c` | 保存包装 129；消费 188 |
| `bms/core/bms_state_store.c` | attempted 41；previous 114～117；资格判断 119 |
| `bms/core/bms_event_log.c` | attempted 31；消费 140/143/217 |
| `bms/core/bms_soc.c` | 积分间隔全局 155；公共 setter 1396，公共调用 1595/1669；私有显示同步资格不删除 |
| `bms/core/bms_soc_eta.c` / `.h` | valid 镜像 c:33/125/137/148、h:29；诊断协议字段仍保留 |
| `bms/afe/sh3673510/sh3673510_project_config.h` | 无消费者 SCONF6 合成宏 74 |

## 时间模型专项结论

| 所有者 | 时钟/单位与使用边界 | 处理结果 |
|---|---|---|
| app 调度 | `clock_time/clock_time_exceed` 系统 tick 控制 200 ms 期限；SOC 输入时间戳另用 32K tick；慢轮只采一次；Event 秒任务独立 | 保留，不追赶伪造多个 ADC 新样本 |
| DVC ADC | CC2/VADC 独立完成事件；512 ms 项目年龄上限；generation 区分断线新帧 | 保留 pending、seen、fresh、tick、generation；删除的只是未消费镜像 |
| SH ADC/检测 | CADC/VADC 400 ms 活性、温度启动 1.2 s、C+/LOAD 检测切换约 200 ms | 保留完成标志、互斥检测、unknown/present/removed；调用频率不代替物理证据 |
| 软件保护 | 有效样本计数；filter_10ms 经当前采样周期换算 | 保留触发/恢复计数，未改成墙钟或取消连续资格 |
| guard | 32K tick 控制 watchdog 总线静默，恢复需三次有效新样本 | 保留，不把一次总线成功当恢复 |
| feature | 断线等待/超时为 tick，稳定资格为样本数；DVC 45 s/SH 20 s 均衡续期 | 保留 cleanup_pending、续期/请求/回读、温度回差；不能合并为单一 active |
| SOC | 同次输入 timestamp 的无符号差；首帧/重复/过长 gap 不积分；策略以 200 ms 量子累计 | 去掉 interval 全局中转，前帧方向和积分余数不变；OCV 诊断暂停与新帧确认保留 |
| State/Event | 32K tick 检查点、失败重试；Event 秒余数跨轮保留 | 仅删除 attempted 镜像；不合并 dirty、last_failed 或 pending/cache |
| UART/RS485 | CPU clock 的帧尾/DE/timeout；DMA done、UART done、理论最短线时间独立；32K tick 判断 30 s 无活动后允许 Suspend | 只删 timeout_seen；接收重装、PAD 残帧、Suspend 资格保留；UART 空闲不等于 guard watchdog 总线静默 |
| BLE/电源 | SDK suspend 回调；保护性到期与普通 suspend 各有条件 | 删除无消费者 BLE tick，保留实际 app_power 期限、连接、OTA及终止态 |

刻意保留的复杂部分：DVC/SH 物理释放证据、读清/自清寄存器、断线激励撤销失败、Flash commit-last/轮转、跨 MCU reset 的锁存边界、普通/保护性低功耗差异。这些需要具体器件/板级证据，不能凭变量数缩减。

## 验证与资源

以下结果固定于 `82fd6f49` 和上述未提交 D008 配置。中间失败记录均保留，不能算作通过。

| 检查 | 最终结果与证据目录/文件（相对本机证据根） |
|---|---|
| 四产品 `sources --check` | 101/100/100/100 项通过，清单及源文件顺序未改；`final-sources.log`，全量回归也包含 `source_manifests-shared` |
| 完整 host 回归 | **114/114 通过，0 失败**；`final-host-v3/report.json`、`report.md`、各组原始日志 |
| 六配置 TC32 link | D008 16S LFP/20S NMC/24S LFP、D011/D013/D014 全部通过，每次 0 error、0 warning；`final-link-v3.log`、`final-20s-nmc-v4.log`、`final-24s-lfp-v4.log` |
| 六配置资源检查 | 全部通过，无 BIN；仍有 Flash 余量小于 8 KiB 的资源提示，详见下表及 `final-build/47196ba33c07/*/*/gen/resources.json` |
| DVC 编码等价 | 原提交/当前实现比较 **983,049** 组编码，全部相同；覆盖 0..65535 请求、不同 Rsense 及 u32 边界；`dvc-equivalence.py`、`dvc-equivalence.log`。三 profile 默认寄存器访问轨迹与旧实现一致，27 次总线操作/49 断言，保留故障注入 |
| Cppcheck 原生四产品 | 从真实编译数据库分析 D008 38 个、SH 各 37 个应用 C TU；**0 error、0 warning**，style 分别 83/79/79/79；`static-v3.log`、各产品最新 `static/run_*/findings.json` |
| ASan/UBSan | **40 个不同产品/场景组合全部通过**：`asan-host` 首次 34 通过、6 未启动；`asan-sh-retry` 将这 6 组全部补跑通过。没有把初次失败报告改写为成功；未发现 sanitizer 错误 |
| Git/独立审查 | Standards/Spec 最后一批源码复核无剩余阻断；报告中系统 tick 与 UART Suspend 时间表述已按复核更正 |

Cppcheck 的 SH 各 2 项覆盖提示是 `bus_mux.h`、`sif_send.h` 未被该产品真实依赖引用；D008 配置已解析它们，四产品联合范围没有因此遗漏应用文件。供应商 SDK 129 个依赖头只作解析，非本轮诊断范围；未执行 MISRA。style 剩余主要为参数命名/变量范围，以及单产品未消费但其他产品/测试消费的接口。核对后保留序号回绕跳零、可配置能力分支、测试观测接口及硬件失败防线，不以压低告警数为由删除。

Sanitizer 在 WSL GCC 上执行，编译参数为 `-fno-pie -no-pie -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer`，并设置 `BMS_SANITIZE=1`。范围为 D008 power/SOC、四产品 SOC、DVC ADC/NTC/寄存器、四产品 Storage/Modbus/UART/features/diagnostics、AFE configuration gate、四产品 AFE transaction、D014 safety loop、SH 三产品 power/recovery；共 40 组。单独以 `CC=<树外 asan-cc 路径>` 启动 SH 补跑，脚本直接 `exec gcc <上述参数> "$@"`。这补充了 114 组正常回归，不代表所有测试脚本或目标 TC32 都受 sanitizer 检查。

最终回归输入指纹为 `63d495c9461fdf8c381e8940f3ebe55b1a9ece42c53e9ca9f10b0db7ffaf55dd`。三个最终 host/sanitizer 报告均记录 `source_changed_during_run=false`；交付前重新逐文件核对其输入 hash，全部仍相同。108 个清单文件 hash 及用户原有 D008 patch 也复核一致。最后提交仅包含报告、清单与文档导航，未变更已验证生产/测试输入。

资源单位均为 byte；Flash 上限 126976，SRAM 32768，主栈预留保持 3072。RAM span 包含 RAM code/cache/IRQ stack；增长余量为扣除主栈预留后的预算，不是实测最坏栈。

| 产品/profile | Flash 前 → 后 | 减少 | RAM span 前 → 后 | 减少 | 最终 Flash 余量 | RAM 增长余量前 → 后 |
|---|---:|---:|---:|---:|---:|---:|
| D008 16S LFP | 125508 → 124340 | 1168 | 26056 → 25932 | 124 | 2636 | 3640 → 3764 |
| D008 20S NMC | 125508 → 124340 | 1168 | 26056 → 25932 | 124 | 2636 | 3640 → 3764 |
| D008 24S LFP | 125508 → 124340 | 1168 | 26056 → 25932 | 124 | 2636 | 3640 → 3764 |
| D011 | 124436 → 123716 | 720 | 25492 → 25480 | 12 | 3260 | 4204 → 4216 |
| D013 | 123588 → 122900 | 688 | 25476 → 25460 | 16 | 4076 | 4220 → 4236 |
| D014 | 124276 → 123556 | 720 | 25492 → 25480 | 12 | 3420 | 4204 → 4216 |

前后 `resources.json` 各自保存 commit、配置、ELF/MAP SHA256；原件分别在 `baseline-build` 与 `final-build`。资源下降不能代替行为等价证据，也未消除量产资源门禁。

复现主要命令（先把 `BMS_BUILD_ROOT`、回归 `--output` 设为源码树外，工具版本及编译命令见报告）：

```powershell
python bms_tools/bms.py --all-products sources --check
python tests/run_host_regression.py --output <树外报告目录>
python bms_tools/bms.py --all-products link --jobs 4
python bms_tools/bms.py --all-products resources
python bms_tools/bms.py --product d008 --d008-profile 20s-nmc link --jobs 4
python bms_tools/bms.py --product d008 --d008-profile 20s-nmc resources
python bms_tools/bms.py --product d008 --d008-profile 24s-lfp link --jobs 4
python bms_tools/bms.py --product d008 --d008-profile 24s-lfp resources
python bms_tools/bms.py --all-products static --no-report
```

基线完整回归最初 15 组失败，属于旧夹具/工具接口与当前源码失配，先单独提交修复并补跑。过程还发现 SOC 私有参数误改、State 字符串检查/夹具旧字段、profile 抽取漏掉条件编译边界，均已修复后重新执行最终 114 组。初次 sanitizer 的 6 个 SH 用例未启动，原因是旧脚本把含参数的 `CC` 整串当可执行文件名；改为树外编译器启动脚本后补跑。上述失败记录保留，不把失败或未启动记录算作通过。

## 中文提交与复审

| 提交 | 主题 |
|---|---|
| `12fe8c0d` | 修复重构前全量回归夹具与工具契约 |
| `b60d3b21` | 平台死状态、空充电源接口及旧注释 |
| `cecfc66f` | DVC 只写保护缓存、采样镜像及别名 |
| `9e5da738` | 授权/持久化重复状态及包装 |
| `f6491cc7` | SOC 局部时间、恒真参数及 ETA 镜像 |
| `54dd503` | 修复审查发现的私有 SOC 显示同步误改 |
| `701e8e2` | 删除未使用 SH 固定 SCONF6 合成宏 |
| `82fd6f49` | 清理静态检查确认的死接口、覆盖赋值与夹具同步 |

独立 Standards 与 Spec 审查均发现同一项 P1：早期参数替换误及 `soc_apply_real_value`。`54dd503` 已恢复其原有双参数/条件，随后两轴复核无剩余阻断。该中间版本的失败编译及中止回归不作为最终验证。静态扫描提示继续核对后才删除，未把单产品 unusedFunction 直接判成全仓死代码。

## 仍需实板或发布验证

- MOS/Gate、SC/OCD/OCC 实际阈值、反向恢复和 watchdog 静默期间硬件关断；host 寄存器 peer 不模拟比较器或 MOS SOA。
- DVC/SH ADC 时序、断线激励与通道位映射、NTC 开短路、真实 Rsense/零点，以及均衡续期和撤销失败。
- D008 PC4 断电、ACC、SH deep sleep/唤醒、BLE连接下 Suspend、UART 首帧与 DE 停止位；未测功耗或最坏 IRQ 时延。
- 真实 Flash 擦写中掉电、写保护与 OTA；RAM 故障注入不构成跨 Config/State/Event 原子性或升级验收。
- TC32 实际最坏栈/中断嵌套；MAP 的 3072-byte 主栈预留是预算。
- D013 受控原理图/BOM、D014 板级阻断及各产品默认参数签核继续按 [硬件验证清单](HARDWARE_VALIDATION.md) 处理。
- development Flash 余量仍小，不能据本轮下降认定具备量产余量；用户原有 D008 参数未提交，量产需在获准的干净配置上单独执行发布门禁。
