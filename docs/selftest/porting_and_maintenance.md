# 移植与维护说明

移植到另一 Telink/BMS 目标时必须先修改端口层，不得直接沿用本板极性：确认 MCU 型号/ABI、SRAM、时钟、WDT、复位原因、保留寄存器、Flash/OTA 分区、CHG/DSG/PCHG/均衡/熔断/CTLC 引脚、AFE 读写协议及反馈含义。

维护检查表：

1. 每个新增“开启输出”函数都调用 `BMS_FailSafe_AllowOutputs()`；CI 的 RF_EN 静态检查只是最低防线。
2. 修改启动/链接后以 NM/MAP/LST 确认早期调用、Manifest 和 guard 未被删除或重叠。
3. 修改镜像头/OTA 后同步修改 Manifest 范围并保留 Telink 原生后处理。
4. 修改任务周期后重新计算 IRQ 窗口、心跳窗口、Flash/RAM 分块和 WDT 裕量。
5. 修改 ADC/AFE 参数后重做容差、开短路、配置漂移和错误持续时间测试。
6. 启用 deep-retention 前验证唤醒初始输出、保留变量、时基偏移和完整重检策略。
7. 发布前确认生产报告 `test_build=false`、故障掩码为 0，并隔离/销毁测试 BIN。
8. 更新需求追踪、测试报告、工具版本、固件哈希、偏差和残余风险；任何认证结论由授权人员批准。
