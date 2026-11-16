# Comm Tool 开发入口

来源：comm-tool-branch@5521e851d77e517b76bec679dd215bbd3e2d4528；[导入白名单及原始哈希](import-manifest.json)。
只迁入 Comm Tool App/IAP/BSP、其使用的 STM32F10x StdPeriph V3.5.0/CMSIS、维护/诊断工具和相关文档，未导入旧 BMS 业务固件。
原始供应商文件保留原有许可声明；后续代码差异由 Git 记录。

## 工程和布局

工程：../../firmware/comm_tool_f103ret6/keil/COMM_TOOL_F103RET6.uvprojx。
同一工程有 COMM_TOOL_Release（App）和 COMM_TOOL_IAP 两个独立 target；目标 STM32F103RET6，沿用 ARMCC 与 StdPeriph，不切换 HAL。
IAP 为 [0x08000000,0x08008000)，App 为 [0x08008000,0x08018000)，缓存从0x08018000开始、元数据页0x0807F800；SRAM末32字节给0x2000FFE0 mailbox。
Comm Tool 自身升级与被升级 BMS 的地址合同不同，不能互换。

使用 tools/comm-tool/build_comm_tool.py 构建；默认仅生成 AXF/MAP/LST，项目副本、日志和缓存在用户临时区。
可用 --prepare-only 准备临时工程；需要HEX时显式加 --images。本次迁移不生成BIN，不烧录。
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
