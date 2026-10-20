# Windows AFE Hardware Protection V2 Editor

## Scope

The Windows AFE hardware editor configures the independent AFE hardware protection profile. It is not a mirror of the MCU software First / Second / Third protection parameters.

Both Windows applications use the same V2 protocol and firmware-side transaction rules:

- `BmsTool.Windows`: customer application. The AFE editor is hidden unless the program is started with `--enable-afe-hw-editor` and the existing protected advanced-feature UI is unlocked.
- `BmsFactoryTest.Windows`: internal engineering/factory application. The AFE editor is directly visible because the whole application is an internal tool.

The internal application does **not** bypass firmware authorization. Both applications must open the same temporary AFE hardware session before a write.

## Protocol

The common semantic Modbus map is:

- `0x2500..0x2522`: requested/persisted 35-word AFE hardware profile.
- `0x2523..0x252B`: capability and transaction metadata.
- `0x2540..0x2562`: read-only effective/quantized profile.
- custom function `0x42`: temporary AFE hardware write session.

A profile write is accepted only as one complete 35-word Modbus `0x10` transaction. Partial hardware-profile writes are rejected.

## Transaction

The PC application performs the following sequence:

1. Read requested profile, metadata and effective profile.
2. Detect backend ID and capability bitmap.
3. Validate edited semantic values locally.
4. Open the firmware AFE hardware session (`0x42`).
5. Write the complete 35-word candidate in one `0x10` request.
6. Read the requested profile back.
7. Read the effective profile and transaction state back.
8. Report success only when requested values match, effective values are readable, `apply_state=OK` and `last_error=None`.
9. Close the temporary session. Successful firmware commit also closes it automatically.

Firmware persists the candidate before applying it and rolls back to the previous profile if apply/verification fails. If rollback itself fails, the firmware reports `CONFIG_INCONSISTENT`; the Windows tool treats that as failure and requires the operator to re-read status before continuing.

## Transport

Direct serial Modbus RTU is preferred for engineering parameter work. BLE is supported only when the transparent transport can carry the complete atomic request. The V2 editor does not intentionally split the 35-word profile into independent hardware writes.

## Backend abstraction

The PC application does not expose a shared raw-register configuration model. It uses semantic fields and the device-reported `backend_id + capabilities`:

- D008 / DVC1124: model `0x1124`.
- D011 / D013 SH35xx stack: model `0x3510`.

The firmware backend owns validation, chip-specific quantization, register encoding and effective-value reporting. Requested and effective values are shown side-by-side so hardware step-size differences are explicit.

## Enable mask

The generic Windows V2 page currently preserves the device's existing `enable_mask` and does not provide a generic control for enabling additional hardware protection channels. This is intentional: a reusable PC tool must not silently enable a product-specific safety channel that has not been reviewed for that board/AFE combination.

Changes to the hardware enable mask belong to a product-specific engineering change with schematic/BOM review and real-board validation.

## Safety boundary

The `0x42` session is an accidental-write/engineering gate, not cryptographic authentication. It must not be treated as a security boundary against a hostile client.

The editor also does not authorize unvalidated product values. In particular, D008 SCD, AFE watchdog, body-diode recovery, load-detect policy, external NTC population and related DVC-specific behavior remain subject to product/hardware sign-off. The generic tool does not enable them by itself.

## CI

`.github/workflows/windows-afe-editor-ci.yml` restores, builds and publishes both Windows projects and checks that:

- the `0x42` AFE session protocol is present;
- a serial-capable complete `0x10` writer is present;
- the requested/effective V2 maps are present;
- the customer UI remains launch-gated;
- the internal UI uses the same V2 transaction but is not launch-gated;
- neither editor reintroduces a BLE-only requirement for AFE hardware writes.

## D014 完整参数预检查（2026-10-20）

欠压 2400→2500 mV 本身符合 SH3673510 的 1000..3500 mV 范围。当前 D014 源码中的
AFE 充电低温 −25℃低于原厂 SH CV1.0A p49 的 −20..10℃保证范围，完整 35-word
候选夹带该值时会被固件拒绝；旧 OCC1=100 ms 同样低于 p38 的 140 ms 首档。
本次仅取得源码和截图证据，未读取该次失败后的完整设备参数或状态。

客户版和内部版补齐 SH 电压、四类温度、全部延时和 OCD1/OCD2 共用使能检查；原有
电流、SC/OCD2 基准和恢复关系检查继续使用。整组标量范围错误一次列出，表格显示
“允许输入范围”。不根据沿用请求值的温度 effective 字段自动改写用户温度。
公共 DVC 页面同步补齐固件现有电压/延时检查及 1992 us 短路延时上限。

电流和温度使用 decimal 检查 0.1 单位精度，拒绝 NaN/Infinity 等非法输入。
设备返回失败时独立读取元数据，显示最近事务的 apply_state/last_error，区分校验、
存储、AFE 应用及回滚错误。effective 不可读不阻止元数据诊断；通信结果不确定时
仍要求重新读取，保持单次提交、不自动重发。

新增边界、截图参数候选、共用使能、非法数值和失败元数据回归断言。本轮仅完成源码及
Git 差异审查，未执行测试、编译、EXE 发布或设备写入；已有 EXE 不因源码修改而更新。
