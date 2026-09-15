# GitHub Actions / TC32 CI 运维规则

本文只保留稳定的 CI 运维原则。**实际 job、测试脚本和触发分支以 `.github/workflows/*.yml` 为唯一执行真值**；不要把历史日期、旧分支名或旧产品测试清单复制到本文长期维护。

## 1. 两层 CI

### Host contract checks

运行在 GitHub-hosted runner，用于源码/协议/配置/Flash/SOC 等可跨平台验证的 contract。它不是 TC32 固件编译，也不能证明实板正确。

### TC32 production build

运行在带 `self-hosted, Windows, X64, telink-tc32` 标签的 Windows runner，使用项目固定 TC32 工具链和 Vendor 库。典型门禁包含：`env -> sources --check -> product contracts -> clean rebuild -> check-fw -> size/map -> manifest/verify -> cppcheck -> artifact upload`。

各产品具体 contract 见当前 workflow 和 `BUILD_AND_TEST.md`。

## 2. Runner 固定要求

- TC32：项目锁定 `tc32-elf-gcc 4.5.1-tc32-1.3`；
- GNU Make、Python、cppcheck；
- 仓库内 `tl_check_fw2.exe`、Vendor `.a`、`boot.link`。

不得用 ARM GCC、host GCC、其他 TC32 版本或不同 Vendor 库替代生产工具链。`python bms_tools/bms.py env` 是环境自检入口。

## 3. Public repository 安全边界

- 外部 fork PR 不应直接在本机 self-hosted runner 执行；
- 不使用 `pull_request_target` 运行不可信代码；
- TC32 job 的仓库来源/变量门禁不得为了省事移除；
- 注册 token、GitHub token、密码和证书不写入仓库/Issue/日志；
- Runner 仅授予完成构建所需权限。

## 4. 日常使用

推送后分别确认 Host contracts 和 TC32 production build；后者必须真实运行而不是条件跳过。发布候选归档最终 BIN/ELF/MAP、manifest、静态分析输出和 commit SHA；Actions Artifact 有保留期，不作为永久量产档案。

## 5. 故障排查顺序

1. `bms.py env`；
2. `sources --check`；
3. 失败的 product contract；
4. 本机 `rebuild --jobs 4`；
5. `check-fw` / `map` / `verify`；
6. cppcheck；
7. Runner 服务、权限、PATH、网络。

Host test成功不等于TC32链接成功；TC32成功不等于AFE/MOS/低功耗实板验收。

## 6. 产品分支原则

D008、D011、D013 workflow 可运行不同 integration contract。测试入口变化时同步 workflow 和 `BUILD_AND_TEST.md`，不维护另一个静态旧清单。产品 IO/AFE 硬件事实只放在该分支 `Dxxx_PRODUCT_REFERENCE.md`。