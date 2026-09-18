# D008 参数扩展验证记录（2026-09-18）

## 范围

生产逻辑变化、协议及 schema 3 升级注意见 [D008_PARAMETERS_V1.md](D008_PARAMETERS_V1.md)。没有烧录或实板测试；所有硬件项目仍 TODO_VERIFY_HW。

沿用当前用户配置：实际 16S LFP（历史 profile 名仍含24S）、BMS_APP_SAMPLE_WAKEUP_ENABLE=0，既有 LED 采样调试配置保留。本次没有修改这些参数，产物不是已完成量产签核的配置。

## 软件结果

- source-order：95 entries（93 C + 2 assembly），通过。
- 19 项 host/contract 脚本运行，17 项通过；两项现有默认值断言失败：afe_hw_profile_contract_check 固定期望 CUV delay=8000；d008_framework_contract_check 固定期望24S而当前16S。本次不修改用户默认值迎合断言。
- 新增/扩展 production host tests：加热校验、SN保存/复位、Config完整记录每个写字节断点、域独立默认恢复、10000组校准运算64位oracle对照、State失败不更改RAM/待保存值、SN权限/完整位图/60秒回绕超时/不写Flash的暂存、未知/跨区Modbus零副作用拒绝。
- SOC真实代码测试新增额定容量改变时清学习、旧额定容量学习记录启动拒绝；原积分、门禁、ACC、PM、保护恢复、调度、温度分组测试通过。
- Windows 参数 codec/client/export 测试：低字前序、负偏移、温度/容量边界、ASCII、旧固件备份且拒绝写、SN最大17字节写帧、部分失败ZIP/覆盖导出、无凭据、失败关闭session。既有6项Windows测试也通过。
- TC32 pinned GCC 4.5.1-tc32-1.3 四组合 clean rebuild 均通过，温度保护保持默认开启。check-fw、MAP、manifest、verify 通过。
- cppcheck 原生分析32个实际应用编译单元，coverage gaps=0，无 error/warning；104条 style 建议保留（包括既有未调用API）。MISRA未执行。临时源码副本无git metadata，初次Excel报告生成失败；补齐副本unknown标识后重新完整执行成功，不修改分析规则/产品源码来隐藏问题。

| SW / HW | text | data | bss | clean rebuild |
| --- | --- | --- | --- | --- |
| 1 / 1 | 106456 | 4016 | 7256 | PASS |
| 1 / 0 | 104780 | 4016 | 7252 | PASS |
| 0 / 1 | 106456 | 4016 | 7256 | PASS |
| 0 / 0 | 104780 | 4016 | 7252 | PASS |

默认1/1相对本轮前记录 text102920/data4016/bss7160，text增加3536字节，bss增加96字节。ELF size 不等同实际BIN长度；交付目录记录BIN长度与SHA256。MAP `_ram_use_end_=0x8460D8`；沿用链接器栈保留断言并通过，不能据此宣称最坏调用栈实测有余量，栈高水位仍需实板。

## 重现与证据

Host：PYTHONUTF8=1、PYTHONDONTWRITEBYTECODE=1，运行 tests/*check.py（不含Flash quick独立工具）；测试使用临时目录的MinGW可执行程序，无设备I/O。

TC32 通过 bms_tools/bms.py 对复制的源码执行；包装脚本仅重定向外部构建/报告路径及处理副本无git身份的报告值，不修改编译器/SDK/ABI。矩阵明确通过 Make EXTRA_DEFINES 传入 SW/HW 0/1，保存各组合编译日志以核对实际宏。

正式 BIN、默认 MAP/manifest、验证日志及包装脚本放入 outputs/d008-parameters-20260918。Windows Release 发布保留既有 NETSDK1138（net7.0 已结束支持）工具链警告，本次未擅自升级框架。Windows 通过既有 build-release.ps1 分别发布 win-x64 self-contained 单文件EXE，发布路径见 Windows 交付记录。

## 未完成的硬件证据

加热开关/滞回/传感器故障、BLE MTU23分片、串口、掉电/复位参数保留、校准对软件保护及SOC/低功耗的实际影响、Flash延时、栈高水位均需实板核验，不以host成功替代。当前源码两项默认值契约失败保留，不能将本轮称为全项目所有检查通过。
