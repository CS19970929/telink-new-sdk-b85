# GitHub Actions / TC32 CI 运维规则

本文只保留稳定的 CI 运维原则。**实际 job、测试脚本和触发分支以 `.github/workflows/*.yml` 为唯一执行真值**；不要把历史日期、旧分支名或旧产品测试清单复制到本文长期维护。

## 1. 两层 CI

### Host contract checks

运行在 GitHub-hosted runner，用于源码/协议/配置/Flash/SOC 等可跨平台验证的 contract。它不是 TC32 固件编译，也不能证明实板正确。

### TC32 production build

运行在带 `self-hosted, Windows, X64, telink-tc32` 标签的 Windows runner，使用项目固定 TC32 工具链和 Vendor 库。典型门禁包含：

```text
env
sources --check
product contract tests
clean rebuild
check-fw
size / map
manifest / verify
cppcheck
artifact upload
```

各产品具体 contract 见当前 workflow 和 `BUILD_AND_TEST.md`。

## 2. Runner 固定要求

- TC32：项目锁定 `tc32-elf-gcc 4.5.1-tc32-1.3`；
- GNU Make；
- Python；
- cppcheck；
- 仓库内 `tl_check_fw2.exe`、Vendor `.a`、`boot.link`。

Runner 不得用 ARM GCC、host GCC、其他 TC32 版本或不同 Vendor 库替代生产工具链。

当前 `bms_tools/bms.py env` 是环境自检入口；机器安装路径变化时优先修运维环境，不在固件仓库中硬编码新的本机私有路径。

## 3. Public repository 安全边界

Self-hosted Windows runner 可以执行仓库代码，因此：

- 外部 fork PR 不应直接在本机 self-hosted runner 执行；
- 不使用 `pull_request_target` 运行不可信代码；
- TC32 job 的仓库来源/变量门禁不得为了省事移除；
- 注册 token、GitHub token、密码和证书不写入仓库/Issue/聊天日志；
- Runner 仅授予完成构建所需权限。

## 4. 日常使用

推送后检查两个层次：

1. Host contracts 是否通过；
2. TC32 production build 是否真实运行并通过，而不是被条件跳过。

发布候选还应下载/保留：最终 BIN/ELF/MAP、manifest、静态分析输出和对应 commit SHA。Artifact 有保留期，量产发布证据应另外归档。

## 5. 故障排查顺序

1. `python bms_tools/bms.py env`；
2. `python bms_tools/bms.py sources --check`；
3. 查看失败的 product contract；
4. 本机 `rebuild --jobs 4`；
5. `check-fw` / `map` / `verify`；
6. cppcheck；
7. 最后再排查 Runner 服务、权限、PATH、网络。

不要把 Host test 成功当作 TC32 链接成功，也不要把 TC32 编译成功当作 AFE/MOS/低功耗实板验证完成。

## 6. 产品分支原则

D008、D011、D013 各自 workflow 可以运行不同的 integration contract。任何新增/删除测试必须同步当前 workflow 和 `BUILD_AND_TEST.md`；不维护另一个静态“测试清单副本”。产品 IO/AFE 硬件事实不属于 CI runbook，统一放在该分支 `Dxxx_PRODUCT_REFERENCE.md`。