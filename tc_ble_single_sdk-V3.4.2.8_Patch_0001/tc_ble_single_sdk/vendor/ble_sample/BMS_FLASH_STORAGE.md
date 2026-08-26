# TLSR8251 BMS Internal Flash Storage

This document defines the application-owned nonvolatile storage layout for the
512 KiB TLSR8251 build of `vendor/ble_sample`.

## 1. Layout

The layout assumes the Telink default 0x20000 OTA multiple-boot scheme.

| Range | Size | Owner / purpose |
| --- | ---: | --- |
| `0x00000-0x1FFFF` | 128 KiB | active firmware |
| `0x20000-0x3FFFF` | 128 KiB | OTA firmware slot |
| `0x40000-0x41FFF` | 8 KiB | cold/config KV, 2-sector generation A/B |
| `0x42000-0x45FFF` | 16 KiB | SOC hot KV journal, 4 sectors |
| `0x46000-0x55FFF` | 64 KiB | BMS event log ring, 16 sectors |
| `0x56000-0x57FFF` | 8 KiB | runtime journal, 2 sectors |
| `0x58000-0x73FFF` | 112 KiB | BMS expansion reserve |
| `0x74000-0x7FFFF` | 48 KiB | Telink / SDK top-of-flash reserved area |

`flash_store_cfg.h` is the single source of truth for these addresses.  New
application storage must not hard-code an address elsewhere.

The 512 KiB application layout is rejected at runtime if OTA is enabled with a
multiple-boot address other than `0x20000`.  In particular, a future 0x40000 OTA
scheme overlaps the application NVM area and requires a new partition map.

## 2. Storage models

### Cold/config parameters

`bms_cold_kv_store` uses `flash_kv32` with two sectors.  `flash_kv32` writes a
new generation and complete latest-value snapshot before recycling the older
generation.  Records have CRC32 and a transaction commit trailer.  This is the
configuration A/B power-loss-safety mechanism; do not add a second parameter
store beside it.

### SOC

`soc_kv_store` uses `flash_kv32` as a four-sector append journal.  The caller
only persists the SOC fields already selected by the SOC module.  Do not write
raw high-frequency current integration samples to Flash.

### Event log

`bms_event_log` keeps the existing compact event model and rotates through 16
4-KiB sectors (64 KiB total).  The persistent snapshot carries generation,
CRC32 and commit information.

### Runtime

The existing two-sector runtime journal is retained and placed immediately
after the event-log partition.

## 3. Flash access rules

All application NVM program/erase operations go through `flash_store_safe.h`.
The wrapper enforces these contracts:

1. The target range must be one of cold/config, SOC, event-log or runtime.
2. OTA activity blocks application program and erase operations.
3. A logical program is split into physical chunks of no more than 16 bytes
   and must not cross a 256-byte page boundary in one physical call.
4. 4-KiB sector erase is refused while the BLE link-layer state is connected.
5. Program and erase are read-back verified.
6. Telink flash protection is restored according to the existing application
   flash-protection session policy.

A refused operation is not a successful commit.  Higher storage layers must
leave their RAM/persistent state dirty and retry later.  Existing SOC and event
paths already preserve their persisted/latch state when a write fails; callers
of configuration writes must also check the return value.

## 4. Migration from the previous layout

This branch intentionally changes the 512-KiB storage map.  The previous map
used different addresses for event log, runtime, SOC and cold/config KV.  No
automatic on-device migration is performed in this first implementation.

Therefore, do not deploy this branch as an in-place firmware update to a field
device when preserving its previous NVM data is required.  For development or
a new product template, erase/reset the old application NVM once before using
the new layout.  If field migration becomes a requirement, implement it as an
explicit versioned migration before any new-layout store initialization; do not
silently probe and rewrite overlapping sectors.

## 5. Extension rules

The range `0x58000-0x73FFF` is reserved for future BMS data.  Consume it only by
adding a named, sector-aligned partition to `flash_store_cfg.h`, extending the
range whitelist, and adding/adjusting the static layout tests.  Keep
`0x74000-0x7FFFF` outside application ownership.
