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
