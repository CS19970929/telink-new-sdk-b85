# Comm Tool 开发入口

来源：comm-tool-branch@5521e851d77e517b76bec679dd215bbd3e2d4528；[导入白名单及原始哈希](import-manifest.json)。
只迁入 Comm Tool App/IAP/BSP、其使用的 STM32F10x StdPeriph V3.5.0/CMSIS、维护/诊断工具和相关文档，未导入旧 BMS 业务固件。
原始供应商文件保留原有许可声明；后续代码差异由 Git 记录。

## C073 转接

Comm Tool APP 使用 115200 8N1 与 PC 通信，C073 使用 250 kbit/s、APP 地址 0、Boot 节点 1。原有一线通 A1.3.3 和锂神监控沿用原入口。

| PC 命令 | 载荷和应答 |
|---|---|
| 0x10 / 0x11 | 寄存器读取 / 原有逐字写，地址和数量为小端 16 位 |
| 0x16 | 地址、数量、连续寄存器值均为小端 16 位，最多 120 字。通过 CAN 0B/0C/0D 暂存、校验并一次提交整组 FC10。写入超时后重新读回 |
| 0x17 | 空请求。应答按 0x14F80200 至 05 排列，每帧为小端 32 位年龄（毫秒）和 8 个原始字节。年龄 FFFFFFFF 表示尚未收到 |
| 0x20～0x23 | 缓存裸 APP，校验地址、长度、CRC16 和 CRC32。C073 起点 08003000，按内嵌升级身份和版本族检查 44/46 KiB |
| 0x31～0x33 | 开始、查询和中止 CAN 升级。C073 用缓存 APP 的升级身份和版本族核对在线 D170 身份及容量，再请求进入 Boot。0.2.10 支持历史 44/46 KiB，0.2.11 增加身份 C073002C 的加固 44 KiB。HELLO ACK 字节 6、7 的 C0、73 仅证明板号，46 KiB 保留直接恢复。44 KiB 要求先取得在线 D170，已在 Boot 时用直连串口或 BLE 恢复。身份或布局不符报 23，在 START 前终止 |

Windows 中选择「CAN」和 Comm Tool 的 COM 端口。设备监控走寄存器，可单独打开「飞道广播」查看六帧及其新鲜度。升级完成后上位机通过 D170 和 D17C 核对实际运行 APP。旧 D009 APP 和转接器自身的分区仍按各自原有流程选择。

## 工程和布局

工程：../../firmware/comm_tool_f103ret6/keil/COMM_TOOL_F103RET6.uvprojx。
同一工程有 COMM_TOOL_Release（App）和 COMM_TOOL_IAP 两个独立 target；目标 STM32F103RET6，沿用 ARMCC 与 StdPeriph，不切换 HAL。
两个 target 的 Groups/Files 必须保持完全相同的顺序和唯一文件清单；App/IAP 专用源文件通过各 target 的 `Include in Target Build`（`FileOption/CommonProperty/IncludeInBuild`）排除，共用启动文件、watchdog、异常处理和标准库各保留一份。不要靠在两个 target 中分别增删同一路径表达编译归属，Keil 打开/保存时可能合并文件树，导致重复 `RESET, +First` 和 L6235E。
IAP 为 [0x08000000,0x08008000)，App 为 [0x08008000,0x08018000)，缓存从0x08018000开始、元数据页0x0807F800；SRAM末32字节给0x2000FFE0 mailbox。
Comm Tool 自身升级与被升级 BMS 的地址合同不同，不能互换。

使用 tools/comm-tool/build_comm_tool.py 构建；默认仅生成 AXF/MAP/LST，项目副本、日志和缓存在用户临时区。
构建入口始终保留完整双 target 工程，通过 `-t` 选择编译目标；两个 target 的输出都重定向到临时区，并关闭自定义后处理。
可用 --prepare-only 准备临时工程；需要HEX时显式加 --images。本次迁移不生成BIN，不烧录。
`python -B tools/comm-tool/check_comm_tool_keil.py` 检查两个 target 的有序文件清单、启动/入口归属和临时工程准备结果，不调用编译器。
供应商文件现在位于 firmware/comm_tool_f103ret6/vendor，不再依赖旧103 + 309目录。

## 维护工具

tools/comm-tool/ 保留原Python维护工具及四种界面变体，共用一份源码；镜像由用户选择。
客户一线通日常监控使用 bms-tool-windows 的 WPF 窗口，不启动第二个EXE。
串口/CAN桥接仍沿用Comm Tool本身能力；不据此宣称普通USB-UART可接一线通或WPF已支持Comm Tool全部CAN升级功能。
当前硬件默认PC串口USART1，PC10/11用于一线通上拉/采集；USART3与此引脚冲突，禁止启用接收。
实板验收前核对实装芯片、5V上拉、空闲电平及实际波形。源码/host/链接验证不等同于实板验收。

## 协议与历史资料

[PC串口协议](protocol/COMM_TOOL_SERIAL_PROTOCOL.md)、[BMS CAN-IAP对端合同](protocol/BMS_CAN_IAP_PROTOCOL.md)。
guides/ 与 devlog/ 是从固定提交迁入的原说明，旧日期、旧BMS默认值及旧目录只代表来源版本，不作为新工程路径或当前硬件验收结论。
当前双协议控制和快照合同以PC串口协议新增章节为准。

## 双协议回归

py -3.9 -B tools/comm-tool/check_comm_tool_owc.py 保留旧协议回归；check_comm_tool_owc_dual.py 联合编译真实ct_owc.c和board_owc.c，回放两套边沿、模式、坏帧和回绕。
上述host回放不访问串口。ARMCC工程验证和实板验收分别记录。

## 当前实现与验证（2026-11-16）

App0.2.7支持自动/A1.3.3/锂神模式，旧0x43/0x44兼容。Windows客户/内部版与Python维护窗口都能选择模式，旧Comm Tool明确回退A1.3.3。
控制只影响接收器；没有通过一线通修改BMS发送协议、业务参数或执行升级。
维护Python新协议测试：py -3.9 -B tools/comm-tool/check_comm_tool_owc_dual_host.py。
双协议C/BSP回放26项、Python维护新协议7组及旧快照4组、C#生产模型/协商68项、两版实际WPF窗口检查通过。
IDE 保存、重新打开的自动验证被用户按 Esc 中止，此项尚未完成；上述结果是完整双 target 工程的 UV4 命令行编译结果。
初次迁移的 App/IAP 临时编译只保留单 target，未覆盖双 target 在 Keil IDE 中合并文件树的行为，随后发现 L6235E。修正文件归属并让构建入口保留完整双 target 后，App/IAP ARMCC编译均零错误零警告，AXF入口及分配段范围验证通过；未生成BIN、刷Comm Tool或执行新协议实板验收。
build-keil技能脚本因缺少tool_config共享模块未能运行，实际构建使用本机UV4.exe和本仓库外置输出入口，日志随发布记录。

维护工具使用：
- py -3.9 -B tools/comm-tool/comm_tool_upgrade_ui.py --edition internal
- tools/comm-tool/start_comm_tool_upgrade_ui.ps1支持空固件选择；镜像由用户显式导入。
- tools/comm-tool/build_comm_tool_upgrade_ui_exe.ps1共用单一源码生成客户/内部、中/英文四变体，需安装PyInstaller。
旧Python维护工具的CAN/Modbus对端仍按其合同工作；不会因此自动获得D008经一线通写参数或升级的能力。

迁移的相关原说明：
- [COMM_TOOL_BMS_APP_REVIEW_LOGGING_2026-05-25.html](devlog/COMM_TOOL_BMS_APP_REVIEW_LOGGING_2026-05-25.html)
- [COMM_TOOL_CAN_IAP_ARCHITECTURE_2026-05-22.md](devlog/COMM_TOOL_CAN_IAP_ARCHITECTURE_2026-05-22.md)
- [COMM_TOOL_F103RET6_KEIL_PORT_2026-05-23.md](devlog/COMM_TOOL_F103RET6_KEIL_PORT_2026-05-23.md)
- [COMM_TOOL_UART_SELECT_2026-05-25.md](devlog/COMM_TOOL_UART_SELECT_2026-05-25.md)
- [COMM_TOOL_UPGRADE_UI_2026-05-23.md](devlog/COMM_TOOL_UPGRADE_UI_2026-05-23.md)
- [BMS_COMM_TOOL_UPPER_COMPLETE_GUIDE.md](guides/BMS_COMM_TOOL_UPPER_COMPLETE_GUIDE.md)
- [BMS_WITHOUT_COMM_TOOL_GUIDE.md](guides/BMS_WITHOUT_COMM_TOOL_GUIDE.md)
- [CAN_UPPER_COMPUTER_TECHNICAL_GUIDE.md](guides/CAN_UPPER_COMPUTER_TECHNICAL_GUIDE.md)
- [CAN_UPPER_UI_EDITION_AND_LANGUAGE_GUIDE.md](guides/CAN_UPPER_UI_EDITION_AND_LANGUAGE_GUIDE.md)
- [COMM_TOOL_FUNCTION_LOGIC_PROTOCOL_PLAN.md](guides/COMM_TOOL_FUNCTION_LOGIC_PROTOCOL_PLAN.md)
- [BMS_CAN_IAP_PROTOCOL.md](protocol/BMS_CAN_IAP_PROTOCOL.md)
- [COMM_TOOL_SERIAL_PROTOCOL.md](protocol/COMM_TOOL_SERIAL_PROTOCOL.md)

## 被升级BMS的边界

维护工具中的旧BMS App白名单为[0x08004800,0x0801F800)，保留最后2KiB参数页。
导入源代码曾使用0x08020000旧上限，与源项目当前AGENTS合同不一致；本次统一PC文件校验和Comm Tool缓存准入上限。
启动时对持久缓存描述的大小、地址和上限再校验，不擦写旧缓存，不改变元数据格式；非法缓存不能取得升级资格。
这只适用于原维护工具对应的旧BMS合同，不授权将其它BMS套入该地址；Comm Tool自身App仍为[0x08008000,0x08018000)。

## 持续验证

统一客户端CI已包含Comm Tool目录触发、C#/WPF一线通检查、生产C/BSP与Python维护回归、升级边界检查。Keil双target在本机执行，不把host结果当作ARMCC链接或实板结果。本轮未push，无新远端CI运行结论。

升级边界生产策略与PC文件联合回归15项通过；AXF校验同时覆盖执行区和初始化数据装载区，SRAM末32字节保留。没有执行Flash擦写或掉电测试。

2026-11-16后续：App0.2.8支持本项目锂神扩展E1（有符号电流/故障）；Windows一线通已融入主页面并共用持久记录。详见[统一监控](../../bms-tool-windows/docs/OWC_MONITOR.md)。原始波形仍待实板验收。
