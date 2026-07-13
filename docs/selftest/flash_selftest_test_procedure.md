# Flash 自检测试步骤

1. 运行生产一键构建，保存 BIN、ELF、MAP、JSON 与 SHA-256。
2. 用 `verify_image_crc.py` 验证 Manifest CRC、两段镜像 CRC 和 Telink CRC 全部通过。
3. 用 `corrupt_image.py` 在 Manifest 覆盖范围分别翻转首段、中段、尾段各一位；每个损坏文件均应校验失败。
4. 仅在隔离样板烧录损坏测试镜像，限流上电并同步观察 CHG/DSG/PCHG、均衡、CTLC、RF_EN 和复位。
5. 确认正常输出从未开放、诊断报告 Flash 故障、致命标记按预期跨热复位存在。
6. 重刷原始合格镜像，完全掉电并按维修恢复流程验证。

主机步骤已自动化；第 4～6 步当前环境未执行。
