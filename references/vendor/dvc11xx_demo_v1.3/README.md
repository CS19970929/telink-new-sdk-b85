# DVC11XX DemoCode V1.3 — vendor reference

This directory stores a curated source reference derived from the user-provided archive `DVC11XX_demoCode V1.3(1).zip`.

- Original archive SHA-256: `2431315de30ee2669e75bb09ae811d04935258054ec3e0044f34508888fc38bd`
- Reviewed package: DVC11XX DemoCode V1.3 for STM32F103
- The original package also contains many generated Keil/debug/HEX artifacts; only source files relevant to DVC1124/FET/GP low-side behavior are committed here.
- Files under `vendor_demo/` are UTF-8 normalized/curated review copies; `DVC1124_low_side_excerpt.h` is a focused excerpt, not a replacement for the official DVC1124-2 reference manual or the original vendor header.

## Mandatory source precedence

For this repository, the demo is **reference material only**. It must never override authoritative device documentation.

1. DVC1124-2 official reference manual / datasheet: register addresses, bit definitions, reset values, timing, physical formulas and device behavior.
2. HS-D008 schematic/BOM and product hardware review: board wiring, populated options, shunt, GP routing and MOS driver topology.
3. DVC11XX DemoCode V1.3: usage examples, sequencing ideas and cross-checking only.
4. Historical project code / third-party code: lowest priority.

When the demo conflicts with the official DVC1124-2 reference manual, **the official reference manual wins**.

## Important caveats found in this demo

- `ReadMe.txt` states this DemoCode is for the `DVC11XX-D` series, not a DVC1124-2-only package.
- `FWLIB/HARDWARE/DVC11XX.h` defaults to `#define DVC1110`; DVC1124 must be selected explicitly before treating model-specific definitions as DVC1124 evidence.
- `17.GP5/main.c` calls `CHG_FETControl(ENABLE)` while `FETControl.h` defines CHG/DSG mode `1` as closed and mode `3` as open. This is internally inconsistent and must not be copied as product truth.
- `18.GP6/main.c` initializes `GPIO_Pin_0` although `DSG_status` reads `GPIO_Pin_1`; this appears to be a demo typo.
- The demo itself states that it demonstrates register/API usage and is not production business logic.

These are concrete reasons to keep the official reference manual above the demo.

## Low-side CHG/DSG findings

The demo contains explicit low-side CHG/DSG examples:

- `17.GP5`: GP5 configured as `CHG_LS` (`GP5M = 0b111`).
- `18.GP6`: GP6 configured as `DSG_LS` (`GP6M = 0b111`).
- `19.GPn Plus`: combines GP5 CHG low-side and GP6 DSG low-side with other GP functions.

Control still uses the common DVC FET control fields in register R81 / `0x51`:

- `CHGC[1:0]`: CHG driver control.
- `DSGC[1:0]`: DSG driver control.
- The demo's DVC1124 register header documents `11` as driver ON, `00/01` as OFF and `10` as body-diode-assisted automatic drive mode.

The demo separately reads register R6 / `0x06`:

- `CHGF`: CHG driver output flag.
- `DSGF`: DSG driver output flag.

Crucially, the low-side demos **do not treat CHGF/DSGF as the physical GP5/GP6 pin feedback**. They add a separate physical connection to the MCU:

- GP5 -> MCU PA0, read with `GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0)` as `CHG_status`.
- GP6 -> MCU PA1, read with `GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_1)` as `DSG_status`.

The demo therefore distinguishes:

1. FET/driver command (`CHGC` / `DSGC`).
2. AFE internal driver output flag (`CHGF` / `DSGF`).
3. Physical low-side output pin feedback (GP5 / GP6 sampled by an MCU GPIO).

This is the key reference for the D008 MOS-state design.

## D008 requirements derived from this evidence

For HS-D008, where GP5 is low-side CHG and GP6 is low-side DSG and the high-side output is masked:

- Continue controlling low-side CHG/DSG through the verified DVC `0x51` CHGC/DSGC semantics, with GP5M/GP6M configured as low-side outputs.
- Do **not** label `CHGF/DSGF` as "physical MOS actual state". They are AFE driver/output flags.
- Separate software state into at least:
  - requested CHG/DSG state;
  - commanded/read-back DVC CHGC/DSGC state;
  - AFE CHGF/DSGF driver flags;
  - physical low-side/MOS feedback validity and state.
- On the current D008 hardware, physical MOS state must remain `unknown/not available` unless a verified GP5/GP6, gate/Vgs, or equivalent feedback path to the MCU exists.
- Do not use one variable such as `b1Status_MOS_CHG/DSG` simultaneously as both a command target and a physical feedback value.
- The Windows/diagnostic UI should distinguish `Requested`, `AFE Command`, `AFE Driver`, and `Physical Feedback` instead of displaying one ambiguous "MOS state".
- A future hardware revision that requires true physical feedback should route GP5/GP6 output, MOS gate/Vgs, or a dedicated comparator feedback to MCU GPIO/ADC. Gate feedback proves the drive chain; current/VDS may be needed to prove actual conduction.

## Relevant files

- `vendor_demo/11.FETControl/`
- `vendor_demo/17.GP5/`
- `vendor_demo/18.GP6/`
- `vendor_demo/19.GPn Plus/`
- `vendor_demo/FWLIB/HARDWARE/DVC1124_low_side_excerpt.h`
- `vendor_demo/FWLIB/HARDWARE/DVC11XX.h`
- `vendor_demo/ReadMe.txt`
