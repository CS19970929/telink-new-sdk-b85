# BMS 自检主机测试

一键执行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_selftest.ps1
```

认证专用故障注入构建示例（禁止发布）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_selftest.ps1 -TestBuild -FaultMask 0x20
```

测试覆盖 CRC-32 标准向量、Manifest/Telink 双重校验、单比特损坏、Flash 地址边界、Manifest 字段位置、MAP/符号布局、控制流汇编链接、关键正反码算法、参数双扇区选择模型、故障码连续性、安全输出门控以及生产构建注入禁用。主机模型用于验证可自动化逻辑，不等同于目标硬件覆盖率；硬件故障注入步骤见 `docs/selftest/03_certification_test_plan.md`。
