# C077 上位机、串口与 ST-Link 联合实板验收（2026-11-12）

本轮实际使用 `BmsClient`、`Shared` 和 `BmsSerialTransport` 的生产实现，没有另写 Modbus/EEPROM/升级协议。客户 WPF、内部 WPF、CLI 使用相同实现。GUI 未逐按钮自动操作，以下是其共享业务逻辑的真实 COM18/19200/8N1 验证。

台架为用户确认的13串电池模拟器，无充电器/负载。板上 APP 为 `0ebf7bbe` 的 `_DEBUG_`、`C077_ACTIVE_AFE_COUNT=1`、真实 RTC 版本，APP CRC32 `2EFACC06`，配套新 IAP。AFE2按用户决定不操作、不编程、不据此判定硬件好坏。此次没有改动固件业务源码。

## 实板发现并修正

|路径|旧行为及证据|本轮结果|
|---|---|---|
|AFE1重新校零|实际 AFE1 READY、8样本、valid，但客户端等AFE2就绪20s后误报失败|5A07只判断AFE1配置/采样/校零完成及事务/控制门禁；返回快照仍保留AFE2与pack真实状态。双片编程仍要求两片完成。|
|RTC后首次读取|保持串口开启、空闲36s后D000无响应；下一只读探测成功|已明确识别C077、串口空闲至少3s时，读取超时最多补读一次；活动通信错误仍直接报错。|
|RTC后首次控制写|空闲36s后的1103=3丢帧，客户端报4s超时|写前用D170只读确认APP，必要时仅重试该读；确认失败不发写。原写命令始终只发一次。|
|SOC设置读回|ST-Link证明set_calsoc后内部61%、上报仍59%，随后SOC_Result_Pass才发布61%|C077保留180ms首次等待，未更新时每200ms继续读，在约2s轮询窗口内确认；不重复设置。|
|循环次数设置读回|容量组已恢复1次，但D000还报告上一值2次|同样等待上报更新，容量组只写一次；参数组回读门禁保留。|

轮询中的通信请求仍遵循现有RTU超时，不把约2s轮询窗口宣称为整个操作的绝对2s期限。未改变协议地址、缩放、产品识别、Flash格式或双片编程条件。

## 串口与上位机共享逻辑

逐字段写入阶段执行241个用例、1424帧、229条06/10写请求：65软件保护字按13组合法变值/整组读回/恢复；所有开放的校准K/B（含负B符号转换）；六组常规参数及保留字保持；串数13→12→13；只读/非法/组合校验；SOC、循环、GBK身份、JSON导入导出；AFE1全部32字段逐个Flash保存/读回/恢复、32非法物理编码及过期编辑拒绝；最后合并下发一次AFE1 EEPROM、独立解码全部32字段，再合并恢复基线。

该阶段发现的循环次数上报延迟已单独修复并回归：连续设置SOC 61..65及恢复、循环7..11及恢复全部通过（77帧、12条写请求）。历史失败日志保留，不能将首次失败阶段改写成一次全绿运行；241用例中该项的最终结论来自针对性回归。

另外完成连续读取/参数及硬保护JSON往返/事件C001与C008/重连及RTC唤醒6用例，以及RTC空闲后直接写命令的专项回归。29个旧功能选择器验证ON/OFF位图及恢复，另验证0/12/13/33拒绝。**选择器标志读回不等于每个历史标志都有物理功能。** 旧退役/NOOP接口及前一轮逐地址证据沿用固件的 `docs/c077-afe1-validation-20261112.md`，不把仅有枚举名称的功能列为已实现。

`test-c077-compatibility.ps1`的56项回归包括：AFE2不可用时AFE1校零完成、双片编程不得提前完成、AFE1失败/无效不得完成、空闲读最多重试一次、活动读不补读、写前唤醒成功/失败零写入、丢失写ACK不重发、SOC与循环的延迟发布/超时/一次写入。另通过生产C309维修与C RTU/Flash参数互通、C060兼容回归。

## ST-Link 执行的 ARM 分支

使用板上匹配AXF，在 `App_AFEGet` 且AFE/MOS温度均有效的前台边界停机。只通过正常参数接口临时将13组Filter改为3扫描；不改保护阈值或持久布局。GDB注入测量RAM，实际调用板上 `App_WarnCtrl`、`App_MOS_Relay_Ctrl`、`MOS_SetOutputs` 等生产函数，20组均通过：

- 13组各验证原始pending、前两扫描不动作、第三扫描Second/Third、无效采样保持且不推进恢复、恢复边界。过流/压差的长恢复计数快进到边界前两次，不宣称实测完整等待时长。
- 分口充/放电、同时请求充电优先，核对MCU状态/GPIOB、AFE1 CONF和BSTATUS许可。
- 采样无效保持已有请求、禁止新开通；充电OV/放电UV/共同MOS高温关断。
- 预充不足100ms不得开放电，之后DSG交接并关闭预充；零尾流复检关闭两路。
- AFE短时故障保持已开DSG，故障年龄注入超过3s后关断。

测量RAM与计时注入是软件分支证据，不代表真实电流、温度、VGS或硬件动作时间。结束复位清除注入，恢复测试前业务区并逐字节验证。原始MI命令、结果JSON及SOC断点记录保留在交付证据中。

当前生产源码的BMS与存储模型均通过，含309总线/CRC/校零/EEPROM中断、双片与单片控制、SOC积分/OCV、RTC/深睡/看门狗、参数A/B与各类掉电切点。模型不等同于真实断电或故障器件测试。

## 复现入口

`test-c077-board.ps1`将临时项目、obj/bin和证据全部放用户TEMP，引用生产Core和串口传输。默认read，其他阶段要求显式台架标记，必须使用新证据目录。

```powershell
pwsh -NoProfile -File bms-tool-windows/test-c077-board.ps1 -Port COM18 -Phase read
pwsh -NoProfile -File bms-tool-windows/test-c077-board.ps1 -Port COM18 -Phase zero -Lab13sNoLoad
pwsh -NoProfile -File bms-tool-windows/test-c077-board.ps1 -Port COM18 -Phase runtime -Lab13sNoLoad
pwsh -NoProfile -File bms-tool-windows/test-c077-board.ps1 -Port COM18 -Phase idlewrite -Lab13sNoLoad
pwsh -NoProfile -File bms-tool-windows/test-c077-board.ps1 -Port COM18 -Phase controls -Lab13sNoLoad
```

`-Phase write`会改变真实Flash、身份、容量与AFE1 EEPROM后恢复；须先用ST-Link备份四页业务区。协议身份接口按32字节保存，无法精确还原旧奇数长度元数据；SOC修订/日志也可能改变。接口读回一致后，仍须按固件报告使用本次备份恢复业务区、复位并逐字节核对。脚本不擅自用Flash工具恢复，也不操作AFE2。`-Phase deep`只是正常深睡命令的受理证据，必须另用ST-Link检查STOP和重附着，完成后复位恢复。

## 剩余边界

AFE2、实际充电器插拔/电流方向及幅值、均衡电流、NTC扫温、模拟器逐串动作点、VGS/预充压差、PC13/PA0物理唤醒、无调试器真实功耗、实板Flash拔电、栈高水位以及USART1/无线物理链路尚未验收。当前没有声称覆盖全部机器分支或生产双AFE板已通过。允许自动执行的接口与核心软件分支验证不能替代这些现场项目。

## 深睡、恢复与最终基线

通过共享客户端发送1102=10并取得ACK后，ST-Link调用栈为 `PWR_EnterSTOPMode → RTC_DeepBoot → InitDevice`。关闭整个OpenOCD服务再重开，仍可附着同一DeepBoot STOP并单步；两次均读到 `0x40015804=2`、GPIOB ODR=`3004`。早期DeepBoot还没有初始化运行状态变量，此时`g_power_mode=POWER_RUN`的BSS初值不能据此否认实际STOP；以调用栈/PC及外设寄存器为证。

退出后恢复本轮开始备份的E800..F7FF四页业务区，并明确halt后转储完整64KiB。恢复前备份与恢复后全片SHA256均为 `CAB56B48F5155E138A6E682E02D85787BE7669EA3510B382EF221B82C0F903BC`；业务4KiB SHA256为 `2FC3F994B8E825B3563DE88DBA9CE62060FC7A1C7ADD057FC6C0A73E1D914FA7`。因此用户13串总压阈值、身份内部长度、校准、两片保存配置、SOC/日志及IAP/APP已精确还原。AFE1 EEPROM另由显式恢复事务读回与基线26字节一致。
