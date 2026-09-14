# AFE Hardware Protection V2

## 1. Purpose

Software protection and AFE hardware protection are independent safety channels.

- MCU software protection continues to use `g_tParam.protect` with First / Second / Third levels.
- AFE hardware protection uses `bms_afe_hw_profile_t` and has no First / Second / Third concept.
- Updating software protection must not rewrite AFE hardware protection.
- Updating AFE hardware protection must not rewrite `g_tParam.protect`.

On the first firmware version containing this split, an empty AFE profile is initialized once from the legacy protection parameters and persisted. After that migration, the two parameter sets evolve independently.

## 2. Product backends

- D008: DVC1124 (`afe_model = 0x1124`).
- D011 / D013: SH35xx backend (`afe_model = 0x3510`), implemented by the current SH3673510/SH3673520 stack.

Raw AFE register values are never part of the common PC protocol. Each backend validates, quantizes and applies semantic values using its own chip rules.

## 3. Persisted profile

The persisted profile is 35 unsigned 16-bit semantic words:

| Word | Meaning | Unit |
|---:|---|---|
| 0 | schema version | - |
| 1 | AFE model | - |
| 2..9 | cell OV/UV trip, delay, recovery and recovery confirmation | mV / ms |
| 10..15 | discharge OC1/OC2 and common recovery | 0.1 A / ms |
| 16..21 | charge OC1/OC2 and common recovery | 0.1 A / ms |
| 22..24 | short circuit trip, delay and recovery confirmation | 0.1 A / us / ms |
| 25..33 | hardware temperature trip/recovery and recovery confirmation | `(degC+40)*10` / ms |
| 34 | enable mask | bitmap |

Unsupported capability bits are rejected rather than silently ignored.

## 4. Modbus map

### Requested / persisted values

- `0x2500..0x2522`: 35-word requested profile.
- Writing is allowed only as one complete 35-word Modbus `0x10` transaction.
- Partial writes are rejected.

### Metadata

- `0x2523`: capability bitmap
- `0x2524`: persisted profile valid
- `0x2525`: product shunt resistance in uOhm
- `0x2526`: product cell count
- `0x2527`: AFE watchdog seconds
- `0x2528`: privileged AFE session active
- `0x2529`: apply state
- `0x252A`: last error
- `0x252B`: interface version (`2`)

### Effective values

- `0x2540..0x2562`: 35-word effective profile after AFE quantization.
- These registers are read-only.
- The PC tool displays both requested and effective values so a user can see chip quantization explicitly.

## 5. Privileged write session

Custom Modbus function `0x42` gates AFE hardware writes.

- OPEN `0x01`
- HEARTBEAT `0x02`
- CLOSE `0x03`
- STATUS `0x04`
- Session timeout: 60 seconds
- Successful hardware-profile commit closes the session automatically.

The session is a deliberate write gate, not a cryptographic security boundary. Customer builds additionally hide the editor UI by default.

## 6. Transaction semantics

A hardware write executes as one safety transaction:

1. Read and retain previous persisted profile.
2. Validate the complete candidate profile and capability mask.
3. Persist the candidate profile.
4. Apply it through the active AFE backend.
5. Read back the requested profile.
6. Read back the effective/quantized AFE representation.
7. Mark success only when all checks complete.

On apply or verification failure, firmware restores and reapplies the previous profile. If rollback itself fails, `apply_state` becomes `CONFIG_INCONSISTENT`; the PC tool reports this state and must not present the write as successful.

## 7. Software protection remains independent

The normal protection parameter path updates only `g_tParam.protect`. It does not call `bms_afe_apply_protection_config()` as a side effect of software parameter updates.

This preserves the intended hierarchy:

- software First / Second: warning / reporting policy;
- software Third: MCU protection / MOS blocking policy;
- AFE hardware protection: independent chip-level backup or fast primary protection where appropriate (for example short circuit).

## 8. Windows tool behavior

Both Windows projects use the same V2 protocol model and the same `0x42` firmware write gate. Direct serial Modbus RTU is the preferred transport. BLE remains optional when its negotiated MTU can carry the complete atomic write.

Customer project `BmsTool.Windows` compiles the editor but hides it by default. The page is shown only when both conditions are met:

1. the application is launched with `--enable-afe-hw-editor`;
2. protected advanced features are unlocked.

Internal project `BmsFactoryTest.Windows` exposes the AFE hardware page directly because the whole application is already an engineering/factory tool. It still has to open the same 60-second firmware authorization session before any write; the internal UI does not bypass firmware validation or rollback.

Both editors detect `backend_id + capabilities`, render only supported semantic parameters, and show requested vs effective values. The current GUI intentionally keeps `enable_mask` read-only and preserves the device's existing enable state; this prevents a generic PC tool from silently enabling an unreviewed hardware protection channel. Product-specific enable-mask changes require an explicitly reviewed engineering change.

## 9. Safety restrictions

This architecture does not authorize unreviewed product values. In particular, D008 SCD, AFE watchdog, body-diode recovery, load-detect policy and other hardware-specific options remain at their reviewed defaults unless separately validated on hardware. The common interface unifies parameter semantics and transaction handling; it does not force the same raw AFE configuration across different chips or products.
