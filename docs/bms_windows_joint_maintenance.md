# BMS 固件与 Windows 上位机联合维护

本仓库同时维护 BMS 固件和 Windows 上位机，但两者保持明确的项目边界。

## 目录边界

| 项目 | 目录 | 说明 |
| --- | --- | --- |
| BMS 固件 | `tc_ble_single_sdk-V3.4.2.8_Patch_0001/`、`tc_ble_single_sdk/`、`bms_tools/` | Telink B85 固件源码、构建脚本与校验工具 |
| Windows 客户版 | `bms-tool-windows/BmsTool.Windows/` | 日常客户使用的 BMS Assistant；不包含工厂测试入口 |
| Windows 工厂版 | `bms-tool-windows/BmsFactoryTest.Windows/` | 独立出厂测试程序，包含客户版完整功能并增加“出厂测试”页 |
| 固件协议说明 | `docs/factory_test_protocol.md` | Factory Session、心跳、超时、注入和清理协议 |

## 维护规则

- 固件改动只提交到 Telink/BMS 相关目录；Windows 改动只提交到 `bms-tool-windows/`。
- 客户版与工厂版是两个独立的 `.csproj` 和可执行程序。普通客户版不应引用或显示工厂测试入口。
- 两端协议变更必须同时更新固件实现、`BmsClient.cs`/`ModbusRtu.cs` 和协议文档，并分别完成固件与 Windows 构建检查。
- `bin/`、`obj/`、发布缓存和临时验证文件不属于源码提交；构建输出应放到项目约定的输出目录或用户临时目录。

## 常用构建入口

### BMS 固件

在 `bms_tools/` 目录按现有脚本执行：

```text
python bms.py sources --check
python bms.py rebuild --jobs 4
python bms.py static
python bms.py verify
```

### Windows 上位机

分别对以下项目执行 Release、`win-x64`、self-contained、single-file 发布：

```text
dotnet build bms-tool-windows/BmsTool.Windows/BmsTool.Windows.csproj -c Release
dotnet build bms-tool-windows/BmsFactoryTest.Windows/BmsFactoryTest.Windows.csproj -c Release
dotnet publish bms-tool-windows/BmsFactoryTest.Windows/BmsFactoryTest.Windows.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true
```

工厂版测试流程的安全约束和未完成的真机验证项，以 `bms-tool-windows/BmsFactoryTest.Windows/README.md` 与 `docs/factory_test_protocol.md` 为准。
