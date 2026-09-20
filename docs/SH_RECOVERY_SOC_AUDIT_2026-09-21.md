# D014 SOC 接入、恢复与 CI 修复（2026-09-21）

基线 `851197d`，分支 `refactor/d014-common-bms-features`。本轮不改产品 IO、参数/Flash 布局、OTA 格式或 AFE 寄存器定义。完整跨产品审核在 D008 分支 `docs/BMS_ENGINEERING_AUDIT_2026-09-21.md`。

## 已修复

- SH 原始正充负放直接送入负充正放的 SOC core。新增编译期 `bms_afe_current_to_soc_ma()`，只在 app 的 SOC 输入边界转换；Runtime word194 保持原始值，word196 表示实际 SOC 输入。公开实时电流、保护方向保持原约定。INT32_MIN 饱和，避免取负溢出。
- SC 持续置位时每帧清零恢复计数，导致10帧 LOADOFF 永远无法完成。只在首次锁存时初始化，clear后必须下一帧确认。
- pending clear遇负载重新接入没有作废，可能绕过新的稳定窗口。现在负载重新接入/SC重新置位都重启资格。
- 通信失败、休眠、AFE重配置丢弃恢复资格但不清除SC锁存。
- 直接OFF/AFE重配置使FET命令缓存失效，避免重新授权时漏发ON；OFF失败进入通信inhibit。
- HW=0下未使用的硬件恢复函数被零warning门禁拒绝。增加与调用方一致的编译条件，没有关闭门禁。
- `bms.py ci` 接入本产品所有host检查；修正跟不上最近SOC提交的schema、Runtime v3及算法字段契约。UTF-8子进程规避Windows默认编码差异。

## 新增回归

`tests/sh3673510_recovery_host_check.py` 执行原生产C函数：持续SC、负载抖动、clear失败、readback、十个断流位置、休眠、重配置、OFF/ON和写失败。修复前HW=0/1为14/40项断言失败，修复后为0。

`tests/sh3673510_soc_direction_host_check.py` 执行真实边界转换与SOC方向判断，覆盖正负、死区、无效样本、INT32边界，并检查app/诊断调用点。

`python tests/run_host_regression.py`：16组通过。D013/D014运行自身产品集成检查，不将遗留D011板级检查作为产品证据。

## 编译与资源

SW/HW `1/1、1/0、0/1、0/0` 均TC32 clean rebuild/check-fw/size/map/manifest/verify通过，0 warning / 0 error；最终产物恢复1/1。

- 正式BIN：110692 B；SHA-256 `f8b9dd3796633d8c2879bd44d73fe55c0724a99a619b07d2cf2b6416e595a915`。
- `_ram_use_end_ = 0x845cc8`；`__SRAM_SIZE = 0x848000`；扣除600B栈预留的地址余量 8416 B。
- 默认cppcheck：114 style，0 warning/error，应用coverage gap=0；MISRA未执行。
- 编译时基线Build ID带DIRTY，不能作为clean release。发布需从提交后的干净checkout重建。

## 边界与下阶段

未进行刷板、实际Gate/Vgs、持续短路或AFE watchdog试验。必须按本分支 `HARDWARE_VALIDATION.md` 完成验证。

优先验证真实充放电→SOC积分方向；SC/LOADOFF→clear/readback→FET；dead SPI→WDT→重配；休眠失败传播。`sh3673510_control_sleep()`当前void接口不能向MCU deep sleep调用方传播失败，本轮未扩大修改此产品策略。

SH当前采样路径未接入历史Boot Current Zero Calibration。D013软件保护仍与其它产品分叉，需专门的默认参数/边界向量测试后再决定同步；不能因函数同名视为同一行为。D014 heater/MOS NTC capability仍按BOM边界保留。
