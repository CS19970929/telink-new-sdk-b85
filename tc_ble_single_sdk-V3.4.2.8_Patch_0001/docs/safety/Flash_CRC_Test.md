# Flash CRC Test

## 测试目的

检测 APP Flash 区域是否被异常改写，降低固件损坏后继续运行的风险。

## 测试原理

`safety_flash_crc.c` 预留 CRC32 计算逻辑和元数据格式：

```text
Magic
Version
Length
CRC32
```

默认 `SAFETY_FLASH_CRC_ENFORCE=0`，因为当前生产流程尚未写入 CRC 元数据。

## 测试步骤

1. 量产工具计算 APP 区域 CRC32。
2. 将 Magic、Version、Length、CRC 写入保留 Flash 地址。
3. 打开 `SAFETY_FLASH_CRC_ENFORCE=1`。
4. 上电检查 `Safety_FlashStartupTest()`。

## 故障注入方法

```c
#define SAFETY_TEST_ENABLE 1
#define SAFETY_INJECT_FLASH_FAULT 1
```

也可以手工篡改 APP 区域或 CRC 元数据。

## PASS 条件

- CRC 元数据有效。
- APP 区域计算 CRC 与存储 CRC 一致。

## FAIL 条件

- 元数据 Magic/Version/Length 无效。
- CRC 不一致。
- 故障注入后未进入 Fail Safe。
