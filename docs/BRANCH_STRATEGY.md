# D008 / D011 / D013 分支策略与清理清单

更新日期：2026-09-14。

本文用于解决 `telink-new-sdk-b85` 长期开发过程中形成的大量 `feature/*`、`codex-*`、`refactor/*`、`dev*` 分支重叠问题。当前工作重点是 **D008、D011、D013**，因此后续开发应把产品身份直接体现在分支名中，避免再使用“D013 名字但实际是 D008”这类分支。

## 1. 三个当前主产品分支

| 产品 | 当前主开发分支 | AFE / 硬件 | 状态 |
|---|---|---|---|
| D008 | `refactor/d008-bms-phase2` | TLSR8251 + DVC1124-2 | **当前唯一 D008 主开发入口** |
| D011 | `feature/sh3673510-d011-bms` | TLSR8251 + SH3673510/SH3673520 驱动层 | **继续独立开发** |
| D013 | `feature/sh3673510-d013-bms` | TLSR8251 + SH3673510/SH3673520 驱动层 | **继续独立开发** |

D011 与 D013 已经从共同历史上分叉，双方都有独立提交，不能把其中一个当作另一个的“最新版”直接覆盖。产品差异应继续保留在各自分支；真正通用的软件能力再以经过审核的 commit/cherry-pick 方式同步。

## 2. D008 分支关系

当前主线：

```text
feature/dvc1124-22-bms
        |
        v
feature/dvc1124-config-registry
        |
        v
refactor/bms-template-phase1
        |
        +--------------------------+
        |                          |
        v                          v
codex-d008-d013-framework     refactor/d008-bms-phase2
   (并行原型)                    (当前主线)
```

已确认的 Git 图关系：

- `feature/dvc1124-22-bms` 是 `refactor/bms-template-phase1` 的祖先；Phase-1 比它多 20 个提交，因此该分支不应再作为当前 D008 开发入口。
- `feature/dvc1124-config-registry` 同样是 Phase-1 的祖先；Phase-1 比它多 60 个提交。
- `refactor/d008-bms-phase2` 从 Phase-1 继续开发，当前属于明确的 Phase-2 主线。
- `codex-d008-d013-framework` 与 Phase-2 从 `refactor/bms-template-phase1` 分叉；它保留 **13 个 Phase-2 当前分支没有的独立提交**，因此在审查这些差异前不得删除。
- `refactor/bms-template-symbol-cleanup` 与 Phase-1 也已经分叉，仍存在大量独立提交，不能仅凭名称判断为重复分支。

### D008 建议处理

| 分支 | 处理建议 | 原因 |
|---|---|---|
| `refactor/d008-bms-phase2` | **KEEP / ACTIVE** | 当前 D008 主开发分支 |
| `refactor/bms-template-phase1` | **KEEP / BASELINE** | D008/D011/D013 近期演进的重要公共基线，便于 diff 和回溯 |
| `feature/sh3673510-d013-bmsdvc` | **DELETE AFTER LOCAL MIGRATION** | 旧 D008 错误命名；重命名时与 Phase-2 指向同一提交，不再开发 |
| `feature/dvc1124-22-bms` | **ARCHIVE/DELETE** | 已被 Phase-1 完整包含，无后续独立提交 |
| `feature/dvc1124-config-registry` | **ARCHIVE/DELETE** | 已被 Phase-1 完整包含，无后续独立提交 |
| `codex-d008-d013-framework` | **REVIEW FIRST** | 与 Phase-2 分叉，仍有 13 个独立提交；重点检查 `dvc1124_safe_bms.*`、框架测试和配置差异 |
| `refactor/bms-template-symbol-cleanup` | **REVIEW FIRST** | 与 Phase-1 分叉，仍有大量独立提交 |

## 3. D011 分支关系

`feature/sh3673520-spi` 是当前 D011 分支的直接祖先，D011 已在其上继续约 117 个提交。因此 SPI 分支已经从“开发分支”变成“历史里程碑”。

```text
feature/sh3673520-spi
        |
        v
feature/sh3673510-d011-bms   <-- D011 ACTIVE
```

建议：

- `feature/sh3673510-d011-bms`：**KEEP / ACTIVE**。
- `feature/sh3673520-spi`：**ARCHIVE/DELETE**，前提是没有外部 PR、CI、文档或本地脚本仍固定引用该分支。
- `codex-sh3673520-20s-bms`：**REVIEW FIRST**。它较老，但与当前 D011 存在少量独立提交，不能直接删除；先确认那几个提交是否已经以另一种形式进入 D011。

## 4. D013 分支关系

D013 当前唯一明确的产品主分支：

```text
feature/sh3673510-d013-bms   <-- D013 ACTIVE
```

D011 与 D013 的最新历史已经分叉：比较结果显示两边都有独立提交。当前原则是：

- D013 的产品、通信、保护和板级行为继续在 `feature/sh3673510-d013-bms` 开发；
- D011 的产品行为继续在 `feature/sh3673510-d011-bms` 开发；
- SOC、Flash、AFE 抽象、CI 等通用能力需要同步时，先对具体 commit 做 diff，再 cherry-pick/移植；
- 不做整分支互相 merge 来“统一产品”，避免把 D011/D013 的板级 GPIO、通信方式或产品参数交叉污染。

## 5. 其他近期分支分类

### 5.1 需要先审查，不建议现在删除

- `codex-mos-protection-coordination`：仍与当前模板存在较多独立历史，名称也指向保护/MOS 协同工作；先提取真正需要的提交。
- `codex-afe-hw-param-stage`：主要是 AFE 参数/资料阶段工作，偏旧，但在确认相关 machine-readable register schema 已迁移前保留。
- `codex-kv32-flash`、`feature/bms-nvm-storage`：与 Flash/KV 架构相关，清理前应确认当前 `flash_kv32`/Cold-KV/事件日志实现已经包含其有效成果。
- `ci-telink-bms-github-actions`、`ci/remote-runner-smoke-20260912`：在确认当前三个产品分支 CI workflow 已完全接管 runner/构建流程前先保留。

### 5.2 构建/交付型分支

- `build/bms-android-20260902`
- `build/bms-tool-20260902`
- `build/ota-delivery-20260901`

这类不属于产品固件主线。若产物已通过 tag/release/artifact 固化，可转为 tag 或删除长期分支；否则先保留作为交付快照。

### 5.3 旧通用开发分支

`dev`、`dev_test`、`dev-falsh`、`new-dev`、`new-dev-renzheng`、`new-master`、`new-new-master`、`new-new-new-renzheng`、`renzheng`、`renzheng-new-new-new`、`todo`、`test-nvm-2`、`codex`、`codex-dev` 等命名无法直接表达产品与目的。

这组建议作为第二轮清理对象：按“是否有主线未包含的 commit”自动分三类：

1. fully merged/ancestor -> 删除；
2. 有少量独立 commit -> 逐个判断 cherry-pick 后删除；
3. 有仍需长期维护的独立产品功能 -> 重命名为明确的 `feature/<product>-<topic>`。

## 6. 重命名迁移产生的临时别名

本次把 D008 主线切换到 `refactor/d008-bms-phase2` 时，以下引用不再用于开发：

- `feature/sh3673510-d013-bmsdvc`：旧错误命名，待本地 checkout/脚本迁移完后删除。
- `archive/feature-sh3673510-d013-bmsdvc-20260914`：临时安全引用，可在确认新分支稳定后删除。
- `tmp-ignore`：临时引用，应删除。
- `refactor/d008-bms-phase2-docs`：临时引用，应删除。

这些分支不是新的产品线，不允许继续提交新功能。

## 7. 后续命名约定

建议固定为：

```text
产品主开发： feature/<afe>-<product>-bms
架构重构：   refactor/<product>-<topic>
短期功能：   feature/<product>-<topic>
修复：       fix/<product>-<topic>
CI：         ci/<topic>
交付快照：   tag/release 优先，避免永久 build/* 分支
```

对当前三个产品，主开发入口只记这三个：

```text
D008 -> refactor/d008-bms-phase2
D011 -> feature/sh3673510-d011-bms
D013 -> feature/sh3673510-d013-bms
```

## 8. 清理顺序

不要一次性批量删除所有旧分支。推荐顺序：

1. 所有开发机切换 D008 remote tracking 到 `origin/refactor/d008-bms-phase2`；
2. 更新脚本、CI、文档中对旧 D008 分支名的引用；
3. 删除 D008 重命名产生的临时/旧别名；
4. 删除已经被明确后继分支完整包含的历史分支（如 `feature/dvc1124-22-bms`、`feature/dvc1124-config-registry`、`feature/sh3673520-spi`）；
5. 对 `codex-d008-d013-framework`、`refactor/bms-template-symbol-cleanup`、`codex-sh3673520-20s-bms` 等有独立提交的分支做 commit-level 审核；
6. 最后再处理 `dev*`、`new-*`、`renzheng*`、旧 `codex-*` 等历史分支。

原则：**只删除“已确认没有唯一有效工作”的分支；分支名像重复不等于 Git 历史真的重复。**
