# D008 AFE MTU23 / Windows 串口连接修复验证记录

日期：2026-09-17。固件实现提交 `d48a682f`（现有 refactor/d008-common-bms-features 分支）；Windows 实现提交 `a8fefb4`（现有 feature/windows-afe-hw-protection-editor-v2 分支）。本次没有创建开发分支、推送远端或烧录设备。

## 实现与问题边界

- 原BLE MTU23无法承载79-byte AFE完整参数写帧。新增0x42 v2暂存/提交，单包≤20byte，不修改MTU。完整CRC/地址/长度检查通过后复用既有AFE完整事务和回滚；未新增持久化区域。
- 上位机增加SCD使能编辑，其他使能位保留；有效性检查和Requested/Effective回读保留。没有猜测短路阈值或默认打开保护。
- 原ProbeAsync共用BLE重建/三次重试/GATT错误。串口现保持端口打开，等待一线通切换，用只读请求确认设备身份；有界重试及取消。未修改固件总线复用时序。
- MTU传输问题与实际AFE应用/回滚失败是不同问题。本次修复不能证明设备已有CONFIG_INCONSISTENT已消失；需升级后读取apply_state/last_error和Requested/Effective确认。

## 验证结果

- source-order：94 objects，通过。
- 15项Host/contract全部通过：DVC config、D008 framework/common-port/profile、AFE access/profile/fragments、documentation、SW protection/common policy、SOC、power/SOC、storage、diagnostic、Flash。
- tooling unittest：22项通过。
- TC32 clean rebuild / check-fw / MAP / manifest / verify：SW/HW=1/1、1/0、0/1、0/0，以及用户当前16S SW0/HW1配置全部通过。
- cppcheck：31个实际应用编译单元、覆盖缺口0；error=0、warning=0、style=94。与上一版按file/id/message比较新增0、减少0。MISRA未执行。
- Windows两版net8.0-windows10.0.19041.0、自包含win-x64发布成功。test-afe-fragments.ps1、test-diagnostics.ps1通过。
- 无设备故障注入，无自动OTA/烧录。BLE实链路、串口切换和短路动作均保留TODO_VERIFY_HW。

## 资源

默认SW/HW=1/1与上一版02b09093比较：text 101064→101512（+448），data 4000→4000，bss 7056→7148（+92）bytes。text+data=105512/126976，实际含尾部BIN=105668bytes。

链接符号：__SRAM_SIZE=0x850000，_ram_use_end_=0x84606c，地址间距40852bytes；链接器600byte保留检查通过。该地址间距不是实测栈高水位，未做实板最坏嵌套栈测量。新暂存为静态79byte，不新增动态分配或ISR Flash路径；增加完整提交调用层级仍需实板运行验证。

## 交付

`outputs/d008-afe-mtu23-serial-20260917/` 包含customer/internal EXE、当前设置BIN、使用说明、SHA256及evidence。

当前设置BIN为 **16S、SW=0/HW=1**，保留用户工作区参数/LED改动，仅用于当前台架；不能作为量产双保护配置。默认24S及其他编译组合只放evidence供验证，不应直接用于16S设备。当前工作区差异保存在evidence/current-settings.patch，未带入Git提交。固件BIN来源和各编译组合hash见manifest。

实板验收顺序：更新配套固件和EXE→串口观察MODE切换及有效读数→BLE MTU23读取/保存已确认参数→回读Requested/Effective/apply_state→断电重启确认保持。开启SCD前由用户确认电流/延时；在受控台架验证真实关断和恢复条件，不能仅凭Driver Flag证明物理MOS关断。
