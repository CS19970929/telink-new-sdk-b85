# GitHub Actions 运维

日常入口为 [.github/workflows/bms-ci.yml](../.github/workflows/bms-ci.yml)。本页按 2026-10-05 源码说明 job，实际触发条件和命令以该 YAML 为准；本机通过不代表远端 run 已通过。

| Job | 环境 | 内容与边界 |
|---|---|---|
| `host-contract` | Ubuntu hosted + Python 3.11/GCC | 四产品 sources、来源对象检查、完整 host、CMake/CTest；输出 host 日志 |
| `tc32-production` | Ubuntu 24.04 + digest 固定的 Telink 官方 TC32 容器 | D008 三种 profile + D011/D013/D014，共六配置生产 ELF/资源门禁；不生成 BIN |
| `tc32-windows` | `self-hosted, Windows, X64, telink-tc32` | 四产品生产 ELF/resources/static；D008 16S；需要 `TELINK_TC32_CI_ENABLED=1` 且 PR 来源同仓库 |

push 分支、pull_request、workflow_dispatch 条件直接查 YAML。Windows job 被条件跳过时不能记为 Windows TC32 验证通过。Linux 容器与 Windows 固定工具版本可能不同，不宣称产物逐字节相同；Windows 工具与实板仍需独立验收。

## Runner 与失败排查

Windows 按 [构建指南](BUILD_AND_TEST.md) 准备官方 TC32、Make、Python、Cppcheck 及 Vendor 库；`BMS_BUILD_ROOT`/`BMS_TEST_OUTPUT` 指向 runner 临时区。workflow 使用单独 checkout 目录避免长期 runner 的旧锁文件干扰，生产仍要求干净 Git 状态。

先查失败 step 原始日志，再按 sources → 对应 host → 同产品/模式/profile link → resources → static 定位。`verify_baselines.py --fetch` 会访问远端取得来源证据；网络失败与固件行为失败应分开记录。编译失败不能靠旧 ELF 或提高 warning 基线绕过。

只读权限、同仓库 PR 门禁和禁止不可信代码在 self-hosted 上运行的约束保持。不要使用 `pull_request_target` 执行外部 PR，也不把 token/证书写入日志。

## 遗留 workflow

[afe-hw-split-ci.yml](../.github/workflows/afe-hw-split-ci.yml) 是旧 D011 分支流程，仍带手动触发入口、隐式产品选择和旧产物路径；不能用它验收 monorepo，也不要在本分支手动运行它。它含 `rebuild/check-fw`，会生成镜像。本次文档任务未修改 CI 执行行为；未来如退役，应作为单独 CI 变更检查其分支使用者。

## 发布归档

当前 CI ELF artifact 不是发布 BIN；Actions artifact 有保留期。需要镜像时按构建指南的明确镜像流程，归档 commit/profile、工具、BIN hash/manifest、ELF/MAP/resources、host/static 和实板记录。绿色 CI 不关闭 [硬件验收](HARDWARE_VALIDATION.md) 项目。
