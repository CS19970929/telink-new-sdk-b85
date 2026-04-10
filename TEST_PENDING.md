# 待测试清单

这个文件用于记录“已经实现、但还没完成实机验证”的改动。

建议配合仓库内的 `.githooks/pre-push` 使用：

```bash
git config core.hooksPath .githooks
chmod +x .githooks/pre-push
```

说明：

- `- [ ]` 表示待测试
- `- [x]` 表示已完成测试
- 建议每项都写明分支、commit、测试重点和结论

## 当前待测试

- [X] `ac209f1` AFE I2C CRC 补强
  分支：`codex-kv32-flash`
  重点：
  1. 模拟 CRC 异常时，`MTPRead/TwiRead/App_AFEGet` 是否稳定报错
  2. 正常读写路径是否无回归
  3. `MTPWrite` 的跳过回读场景（`RESET` / `SLEEP`）是否符合预期

- [ ] AFE 参数写成功强化
  分支：`codex-kv32-flash`
  重点：
  1. `sh309_i2c_write_with_crc` 的 `NAK` 检查是否有效
  2. `Write_Parameters()` 写后逐字节回读是否稳定
  3. 参数整包最终校验失败时，`AFE_PARAM_WRITE_Flag` 是否保留以便重试
  4. 开机 `SH367309_UpdataAfeConfig()` 成功后才清 `AFE_PARAM_WRITE_Flag`

## 已完成测试

- [x] 暂无
# [x] 示例测试：`git config core.hooksPath .githooks` 后执行 `git push`，确认 pre-push hook 会检查 `TEST_PENDING.md`
