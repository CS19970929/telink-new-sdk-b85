# 启动升级门禁与欠压默认值校验

2026-09-17 14:37 诊断包：Requested=ON/ON；CHG/DSG只剩UPGRADE阻断；保护参数有效、升级未完成；Config/State/Event初始化均成功；Flash底层未记录失败；当前Flash欠压参数3000/3000/2200/3100mV。

后续工作区源码默认值变为3000/3000/3200/3300mV，软件参数revision为2。实际validator拒绝Second(3000)<Third(3200)。升级事务在校验候选值时返回失败，旧Flash和revision保持，LoadParam仍加载有效旧参数；因此可能同时显示“参数有效、存储初始化成功、升级未完成”。包内Build ID未知，不能仅凭工作区版本确认实板编译输入；需新版冻结诊断确认。

## 解决方式

必须由产品使用者确认欠压First/Second/Third/Recover，欠压顺序要求First>=Second>=Third，Recover>Third。不可擅自改安全阈值、放宽校验或清除升级门禁。修正已确认默认值后重编译升级，失败事务未发布新revision，重新启动会重试；无需全片擦除。软件revision独立于AFE revision，不要为修软件参数重置硬件SCD参数。升级应再次核对目标机实际固件和有效参数。

## 诊断扩展

保持schema1、既有地址和Flash布局，capabilities新增bit4表示启动升级详情。旧固件没有bit4，Windows显示“未提供”，不误判通过。所有字段在boot freeze后不再改写：

- 0x2A1B：阶段，0未执行、1开始、2Config加载失败、3候选校验失败、4Config保存失败、5Config完成、6State初始化失败、7Event初始化失败、8全部完成。
- 0x2A1C：无效类别bitmap，bit0软件保护、bit1AFE参数、bit2SOC配置、bit3容量；可同时置位。
- 0x2A60..63：启动原有CUV First/Second/Third/Recover，mV。
- 0x2A64..67：升级候选CUV四值，mV。它们是定位证据，不能替代全部软件参数校验。
- 0x2A6A..6B：存储的软件revision，32位低字寄存器在前。
- 0x2A6C..6D：目标软件revision，同上。
- Trace event9=UPGRADE，arg0阶段、arg1无效类别bitmap。

只记录数据，不改变存储/保护事务、短路执行顺序、参数所有权或失败门禁。全部初始化成功不再被UI当作升级成功的替代证据。

验证入口：`python tests/sw_protection_defaults_check.py --series-num 16 --boot-check` 使用真实软件校验器及存储事务；新增有效旧Flash+非法新CUV候选复现，检查门禁=1、revision未发布、SaveParam不能绕过，纠正默认值并重启后门禁=3。AFE/SOC校验器在该Host夹具中仍为stub，不声称覆盖实板或这些默认值。通用存储Host tests另覆盖掉电和多阶段失败。

用户已确认恢复CUV First/Second/Third/Recover=3000/3000/2200/3100mV。本次仅恢复该阈值组合，不修改其他保护参数或AFE revision；工作区现有滤波设置保留。
