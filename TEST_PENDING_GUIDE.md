# TEST_PENDING 提醒机制使用说明

本文档说明如何在 Git 项目中实现 `TEST_PENDING.md` 的提交提醒功能，以及它在多分支场景下的行为。

## 1. 机制目标

这个方案的目标不是强制阻止提交，而是在你执行 `git push` 之前主动提醒：

- 还有未完成的测试项
- 还有未清理的待办项
- 还有应该在提交前确认的任务

它适合用来管理以下内容：

- 测试计划
- 功能验证清单
- 临时遗留事项
- 发布前检查项

## 2. 机制原理

核心做法很简单：

1. 在仓库根目录维护一个 `TEST_PENDING.md`
2. 在仓库里放一个 `pre-push` 钩子
3. 钩子在 `git push` 前检查 `TEST_PENDING.md`
4. 如果文件里存在未完成项，就输出警告信息

本方案的关键点是：

- 它是本地提醒，不依赖服务器
- 它不会自动修改你的代码
- 它只在 `push` 前提醒，不影响 `commit`

## 3. 目录结构建议

推荐在仓库中使用如下结构：

```text
repo-root/
  .githooks/
    pre-push
  TEST_PENDING.md
```

如果你的项目已经有自己的 hooks 目录，也可以直接复用，不一定必须叫 `.githooks`。

## 4. `TEST_PENDING.md` 写法规范

建议统一使用 Markdown 任务列表格式：

```md
- [ ] 待处理任务 1
- [x] 已完成任务 2
- [ ] 需要在发布前确认的任务 3
```

约定如下：

- `- [ ]` 表示未完成，会触发提醒
- `- [x]` 表示已完成，不会触发提醒

建议保持条目短、清楚、可执行，避免写成大段说明。

## 5. `pre-push` 钩子示例

下面是一个可直接使用的版本：

```sh
#!/bin/sh

REPO_ROOT=$(git rev-parse --show-toplevel 2>/dev/null)
PENDING_FILE="$REPO_ROOT/TEST_PENDING.md"

if [ ! -f "$PENDING_FILE" ]; then
    exit 0
fi

if grep -q '^- \[ \]' "$PENDING_FILE"; then
    printf '\n'
    printf '========================================\n'
    printf 'WARNING: 发现未完成的测试/待办项\n'
    printf '文件: %s\n' "$PENDING_FILE"
    printf '请先检查以下内容后再执行 push:\n'
    grep '^- \[ \]' "$PENDING_FILE"
    printf '========================================\n\n'
fi

exit 0
```

### 说明

- `git rev-parse --show-toplevel` 用来找到仓库根目录
- `TEST_PENDING.md` 不存在时直接放行
- `grep '^- \[ \]'` 用来找未完成项
- 这是提醒机制，不是阻断机制；如果你要改成强制拦截，可以在检测到未完成项时 `exit 1`

## 6. 启用方式

第一次在某个仓库中启用时，需要执行：

```bash
git config core.hooksPath .githooks
chmod +x .githooks/pre-push
```

如果你在 Windows 上工作，也可以只要保证：

- `core.hooksPath` 已配置正确
- `pre-push` 文件可执行或由 Git 正常识别

在本仓库中，`core.hooksPath` 当前已经指向 `.githooks`。

## 7. 多分支场景是否要一个一个加

结论：**不需要按分支一个一个加**。

原因是：

- `core.hooksPath` 是仓库级别的本地 Git 配置
- 钩子目录是仓库级别生效，不是分支级别生效
- 只要同一个仓库里已经设置了 `core.hooksPath`，这个仓库下的所有分支都会共用同一套 hook

### 具体说明

以下情况都不需要重复配置：

- 同一个仓库里的多个分支
- 同一个仓库里的历史分支切换
- 同一个本地仓库里的反复开发

以下情况通常需要重新配置一次：

- 另一个独立 clone 出来的仓库
- 另一台机器上的同名仓库
- 另一个没有继承本地 Git 配置的新工作区

### 一句话理解

**按仓库配置，不按分支配置。**

## 8. 如果团队里有多人协作

如果你希望整个团队都能用这套提醒机制，建议把下面几件事一起做：

1. 把 `.githooks/pre-push` 提交到仓库
2. 把 `TEST_PENDING.md` 的格式约定写进文档
3. 在 `README.md` 或 `CONTRIBUTING.md` 中补一句初始化命令
4. 给新同事一个明确的设置步骤

建议在项目说明里写成这样：

```bash
git config core.hooksPath .githooks
chmod +x .githooks/pre-push
```

如果项目会被频繁重新 clone，最好再提供一个一键初始化脚本。

## 9. 推荐的使用流程

建议按下面的节奏管理：

1. 开发过程中把未完成项写进 `TEST_PENDING.md`
2. 每完成一项就把对应条目改成 `- [x]`
3. 在 `push` 前看一次提醒输出
4. 确认没有未完成项后再推送

这样做的好处是：

- 不容易漏掉测试收尾
- 不需要额外工具
- 和 Git 工作流天然结合

## 10. 扩展成“强制阻止 push”

如果你不想只是提醒，而是想直接阻止 `push`，把钩子末尾改成：

```sh
if grep -q '^- \[ \]' "$PENDING_FILE"; then
    echo "发现未完成项，禁止 push"
    exit 1
fi
```

这会把提醒升级为硬性门禁。

### 适合强制拦截的场景

- 发布分支
- 高风险项目
- 严格交付流程
- 需要防止漏测的团队

### 不适合强制拦截的场景

- 个人实验仓库
- 原型开发
- 频繁试错的临时分支

## 11. 常见问题

### Q1: 只写了 `TEST_PENDING.md`，不写 hook 行不行？

不行。`TEST_PENDING.md` 本身只是一个文本文件，真正触发提醒的是 hook。

### Q2: 已经有多个分支了，要不要每个分支单独配置？

不需要。只要是同一个仓库，配置一次 `core.hooksPath` 就够了。

### Q3: 换一台电脑会不会自动生效？

不会自动生效。新机器或新 clone 需要重新配置 `core.hooksPath`。

### Q4: 可以检查别的文件吗？

可以。你可以把 hook 改成检查：

- `TODO.md`
- `REVIEW_PENDING.md`
- `TEST_PENDING.md`
- 多个清单文件

### Q5: 可以只对某个分支提醒吗？

可以，但需要额外在 hook 里判断当前分支名，再决定是否执行检查。

## 12. 最小可复制模板

如果你想在新项目里快速复制，直接照下面做：

### 第一步：创建 `TEST_PENDING.md`

```md
- [ ] 示例任务 1
- [x] 示例任务 2
```

### 第二步：创建 `.githooks/pre-push`

```sh
#!/bin/sh

REPO_ROOT=$(git rev-parse --show-toplevel 2>/dev/null)
PENDING_FILE="$REPO_ROOT/TEST_PENDING.md"

[ -f "$PENDING_FILE" ] || exit 0

if grep -q '^- \[ \]' "$PENDING_FILE"; then
    echo "WARNING: 未完成事项仍存在"
    echo "请检查: $PENDING_FILE"
    grep '^- \[ \]' "$PENDING_FILE"
fi

exit 0
```

### 第三步：启用 hooks

```bash
git config core.hooksPath .githooks
chmod +x .githooks/pre-push
```

## 13. 建议写入项目文档的说明

如果你要把这套机制放进团队项目，建议在 `README.md` 里补一段：

```md
### Git 提醒机制

本项目使用 `TEST_PENDING.md` 记录未完成事项。
在执行 `git push` 前，仓库会通过 `pre-push` hook 检查未完成条目并给出提醒。

首次使用请执行：

```bash
git config core.hooksPath .githooks
chmod +x .githooks/pre-push
```
```

## 14. 推荐实践

- 把 `TEST_PENDING.md` 当作“提交前检查清单”，不要当作长期杂项堆积区
- 每次提交前尽量清零或收敛未完成项
- 如果内容太多，拆分为“本次必须完成”和“后续跟进”两类
- 个人项目优先提醒模式，团队发布项目优先强制模式

## 15. 结论

这套机制的核心结论只有一句话：

**`TEST_PENDING.md` 是清单，`pre-push` 是提醒器，`core.hooksPath` 是统一入口。**

在同一个仓库里，多个分支不需要分别配置；只要仓库级 hook 配好一次，所有分支都会生效。
