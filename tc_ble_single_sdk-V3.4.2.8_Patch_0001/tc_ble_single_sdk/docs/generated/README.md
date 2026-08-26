# Generated BMS Client Assets

这些文件由 BMS 客户端资产生成工具从 `docs/register_catalog.json` 生成。

当前输出包括：

- `BMSGeneratedRegisterCatalog.swift`
- `bms_generated_register_catalog.py`
- `BmsGeneratedRegisterCatalog.kt`
- `bms_generated_register_catalog.hpp`
- `BMSGeneratedRegisterCatalog.ts`

生成入口：

- Swift / Python / Kotlin / C++：`script/bms_client_asset_tool.py generate`
- TypeScript / 微信小程序：`script/bms_client_typescript_asset.py`

请不要手工维护寄存器地址副本。协议或寄存器变化时，应先修改 `docs/register_catalog.json` 与 `docs/protocol_test_vectors.json`，再重新生成客户端资产并执行回归测试。
