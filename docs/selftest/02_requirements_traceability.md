# 需求追踪矩阵

| ID | 安全需求/故障模型 | 实现 | 验证证据 | 状态/残余 |
|---|---|---|---|---|
| SR-01 | 复位后危险输出先关闭 | `BMS_SelfTest_EarlyBoot`、端口层 | 反汇编、板级 GPIO 波形 | 软件完成；硬件待测 |
| SR-02 | CPU 寄存器固定故障 | `bms_selftest_cpu_tc32.S` | 汇编审查、FI-CPU | r12/SP/LR 直接数据测试有限 |
| SR-03 | 算术/比较标志故障 | CPU 汇编比较/分支 | FI-CPU、反汇编 | 软件完成 |
| SR-04 | 控制流偏离 | 签名和正反码计数 | UT-CF、FI-CF | 编译器/硬件共因仍在 |
| SR-05 | 程序 Flash 位翻转 | Manifest + 启动/分块 CRC | UT-CRC、UT-BIN、FI-FLASH | 头部由 Telink CRC 补充 |
| SR-06 | 链接范围错误 | 链接符号、布局脚本 | UT-MAP | 完成 |
| SR-07 | RAM 固定位/耦合故障 | 保留区 March-like | FI-RAM | 不覆盖全部应用 RAM |
| SR-08 | 运行时 RAM 检查不破坏数据 | 4 字透明保存/恢复 | UT/FI-RAM | 仅专用区 |
| SR-09 | 栈溢出 | linker guard + 水位填充 | FI-STACK | IRQ 独立栈未专测 |
| SR-10 | 时钟停止/严重调度异常 | tick 活动、运行间隔窗口 | FI-CLOCK | 无独立频率参考 |
| SR-11 | 周期中断停止/暴走 | Timer0 计数窗口 | FI-IRQ | 共用时钟风险 |
| SR-12 | 看门狗被无条件喂养 | 四类心跳门控 | FI-WDT | 复位原因语义待确认 |
| SR-13 | AFE 通信丢失 | 通信状态计数与安全态 | FI-AFE-COM | 需断线夹具板测 |
| SR-14 | AFE 配置漂移 | 25 字节回读比较 | FI-AFE-CFG | 寄存器写保护能力待确认 |
| SR-15 | ADC 开路/短路/越界 | 三通道合理性与持续计数 | FI-ADC | 阈值需硬件标定 |
| SR-16 | MOS 命令与反馈不一致 | BSTATUS3 反馈诊断 | FI-MOS | 不能覆盖外部功率管粘连 |
| SR-17 | 故障时关闭 CHG/DSG/PCHG | `BMS_FailSafe_ForceSafeOutputs` | FI-OUT、示波器 | 软件完成；板测待做 |
| SR-18 | 故障时关闭均衡 | AFE 0x41/0x42 清零 | FI-BAL | 板测待做 |
| SR-19 | 普通任务不得重新开输出 | 所有已识别开启路径门控 | 静态检查、FI-REOPEN | 新增路径须维护审计 |
| SR-20 | 致命故障跨复位保持安全 | DEEP_ANA_REG1 粗粒度标记 | FI-RESET | 低 4 位原因；掉电清除 |
| SR-21 | 诊断只读且向后兼容 | Modbus 0xD1E0～0xD1FF | UT-DIAG、协议测试 | BLE 专用窗口未新增 |
| SR-22 | 生产禁用故障注入 | 双宏编译门 | UT-CONFIG | 完成 |
| SR-23 | OTA 镜像完整性 | 后处理 CRC + Telink CRC | UT-OTA-IMAGE | OTA 中途掉电需硬件测 |
| SR-24 | deep-retention 唤醒重新判定 | 预留启动分支 | FI-RET | 当前配置关闭，未板测 |
| SR-25 | 自检不得阻塞保护/BLE | 100 ms 调度、Flash/RAM 分块 | 性能测试 TP-PERF | 实测时间待填 |
| SR-26 | 关键变量单点损坏可检测 | 状态/心跳/掩码正反码 | UT-INVERSE、FI-DATA | 非全部变量均冗余 |
