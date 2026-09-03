# Windows 上位机

本目录是 BMS 仓库中的独立 Windows 项目区，与固件源码并列维护。

- `BmsTool.Windows/`：客户版 BMS Assistant。
- `BmsFactoryTest.Windows/`：工厂版 BMS Assistant，基于客户版最新版功能，额外提供“出厂测试”页和 Factory Session 测试流程。

两个项目分别构建、分别发布。客户版不包含工厂测试入口；涉及 Factory Session 的共享协议代码位于各项目自身源码中，并与 BMS 固件的 `docs/factory_test_protocol.md` 对照维护。

详细的仓库边界、构建入口和协作规则见 `../docs/bms_windows_joint_maintenance.md`。
