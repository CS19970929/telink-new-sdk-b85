# GitHub Actions 运维

日常入口为 [.github/workflows/bms-ci.yml](../.github/workflows/bms-ci.yml)。本页按 2026-10-05 源码说明 job，实际触发条件和命令以该 YAML 为准；本机通过不代表远端 run 已通过。

| Job | 环境 | 内容与边界 |
|---|---|---|
| `host-contract` | Ubuntu hosted + Python 3.11/GCC | 四产品 sources、来源对象检查、完整 host、CMake/CTest；输出 host 日志 |
| `host-sanitizers` | Ubuntu 24.04/GCC | 新公共编译辅助支持的保护/配置/AFE/feature/协议/校准/核心场景，ASan+UBSan；明确为选定范围 |
| `tc32-production` | Ubuntu 24.04 + digest 固定的 Telink 官方 TC32 容器 | D008 三种 profile + D011/D013/D014，共六配置生产 ELF/资源门禁；不生成 BIN |
| `tc32-windows` | `self-hosted, Windows, X64, telink-tc32` | 四产品生产 ELF/resources/static；D008 16S；需要 `TELINK_TC32_CI_ENABLED=1` 且 PR 来源同仓库 |

push 分支、pull_request、workflow_dispatch 条件直接查 YAML。Windows job 被条件跳过时不能记为 Windows TC32 验证通过。Linux 容器与 Windows 固定工具版本可能不同，不宣称产物逐字节相同；Windows 工具与实板仍需独立验收。

## Runner 与失败排查

Windows 按 [构建指南](BUILD_AND_TEST.md) 准备官方 TC32、Make、Python、Cppcheck 及 Vendor 库；`BMS_BUILD_ROOT`/`BMS_TEST_OUTPUT` 指向 runner 临时区。workflow 使用单独 checkout 目录避免长期 runner 的旧锁文件干扰，生产仍要求干净 Git 状态。

先查失败 step 原始日志，再按 sources → 对应 host → 同产品/模式/profile link → resources → static 定位。`verify_baselines.py --fetch` 会访问远端取得来源证据；网络失败与固件行为失败应分开记录。编译失败不能靠旧 ELF 或提高 warning 基线绕过。

只读权限、同仓库 PR 门禁和禁止不可信代码在 self-hosted 上运行的约束保持。不要使用 `pull_request_target` 执行外部 PR，也不把 token/证书写入日志。

## 遗留 workflow

旧 D011 AFE split workflow 已从本分支删除：它引用已移除的检查脚本和旧产物路径，且保留生成 BIN 的手动入口。当前统一入口只有 `bms-ci.yml`；历史分支的 workflow 不受本次删除影响。

## 发布归档

当前 CI ELF artifact 不是发布 BIN；Actions artifact 有保留期。需要镜像时按构建指南的明确镜像流程，归档 commit/profile、工具、BIN hash/manifest、ELF/MAP/resources、host/static 和实板记录。绿色 CI 不关闭 [硬件验收](HARDWARE_VALIDATION.md) 项目。

host 与 sanitizer artifact 保留 90 天，包含 Markdown/JSON/JUnit 和原始日志。长期接受的基线应另外保存；使用统一 runner 的 `--baseline` 作结构化比较，见 [自动化验证](AUTOMATED_VALIDATION.md)。

## 构建证据上传范围

TC32 Windows 与 production matrix 仅上传 ELF、MAP、resources、输入/链接收据、build.log 和 static 结果目录，排除对象和 LST。上传失败仍令 CI 失败，不使用 continue-on-error；源码验证与证据传输结果分别判定。路径无匹配时明确报错。
