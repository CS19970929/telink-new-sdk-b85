# TEST_PENDING 提醒机制 Windows 版说明

这份说明专门给 Windows / PowerShell 环境使用，目标是让你在其他项目里快速复用 `TEST_PENDING.md` 的 `git push` 提醒功能。

## 1. 先说结论

- `git config core.hooksPath .githooks` 只负责告诉 Git 去哪里找 hook
- 它**不会自动创建** `.githooks` 目录
- 它**不会自动创建** `pre-push` 文件
- 它是**仓库级配置**，不是分支级配置
- 同一个仓库里的多个分支，不需要一个一个配置
- 如果旧分支没有 `TEST_PENDING.md`，那 hook 检查不到未完成项，就不会提醒

## 2. 你需要准备什么

最小需要两个东西：

1. 仓库根目录下的 `TEST_PENDING.md`
2. 仓库根目录下的 `.githooks/pre-push`

推荐目录结构如下：

```text
repo-root/
  .githooks/
    pre-push
  TEST_PENDING.md
  TEST_PENDING_WINDOWS_GUIDE.md
```

## 3. Windows 下不要用 `chmod`

在 PowerShell 里，下面这个命令不可用：

```powershell
chmod +x .githooks/pre-push
```

Windows 下通常这样处理就够了：

```powershell
git config core.hooksPath .githooks
```

如果你想确认 hook 目录是否存在，可以执行：

```powershell
Get-ChildItem -Force .githooks
```

## 4. 创建 hook 目录

如果 `.githooks` 目录还没有，就先创建：

```powershell
New-Item -ItemType Directory -Force .githooks
```

## 5. `pre-push` 最小实现

把下面内容保存为 `.githooks/pre-push`：

```sh
#!/bin/sh

REPO_ROOT=$(git rev-parse --show-toplevel 2>/dev/null)
PENDING_FILE="$REPO_ROOT/TEST_PENDING.md"

[ -f "$PENDING_FILE" ] || exit 0

if grep -q '^- \[ \]' "$PENDING_FILE"; then
    printf '\n'
    printf '========================================\n'
    printf 'WARNING: 发现未完成事项\n'
    printf '文件: %s\n' "$PENDING_FILE"
    printf '请先处理以下条目后再 push:\n'
    grep '^- \[ \]' "$PENDING_FILE"
    printf '========================================\n\n'
fi

exit 0
```

这个版本的行为是：

- 找不到 `TEST_PENDING.md` 就直接放行
- 找到 `- [ ]` 就输出提醒
- 不阻止 `push`

如果你想改成“禁止 push”，把最后的 `exit 0` 改成在检测到未完成项时 `exit 1`。

## 6. `TEST_PENDING.md` 最小写法

把下面内容保存为 `TEST_PENDING.md`：

```md
- [ ] 任务 1
- [ ] 任务 2
- [x] 任务 3
```

规则很简单：

- `- [ ]` 表示未完成，会触发提醒
- `- [x]` 表示已完成，不触发提醒

## 7. 启用步骤

在仓库根目录执行：

```powershell
git config core.hooksPath .githooks
```

然后确认当前配置：

```powershell
git config --get core.hooksPath
```

如果输出 `.githooks`，说明配置已经生效。

## 8. 多分支怎么理解

这套机制**不是按分支配置**，而是按仓库配置。

所以：

- 同一个仓库里的老分支
- 同一个仓库里的新分支
- 同一个仓库里的历史分支切换

都共用同一个 `core.hooksPath`。

你不用给每个分支单独加一次。

真正需要重新配置的情况是：

- 另一个独立 clone 的仓库
- 另一台机器上的仓库
- 另一个没有继承该配置的工作区

## 9. 旧分支没有 `TEST_PENDING.md` 怎么办

如果旧分支里没有这个文件，当前 hook 会直接跳过检查，不会提醒。

你有两种选择：

1. 给旧分支补上 `TEST_PENDING.md`
2. 如果旧分支只是历史分支，不再维护，那就不补

如果这个旧分支还要继续开发，建议直接把这套机制回补进去。

## 10. 团队项目推荐做法

如果这是团队项目，建议把下面几件事一起提交：

- `.githooks/pre-push`
- `TEST_PENDING.md`
- `TEST_PENDING_WINDOWS_GUIDE.md`
- `README.md` 中的初始化说明

团队成员首次使用时执行：

```powershell
git config core.hooksPath .githooks
```

## 11. 最小检查清单

启用前按下面顺序确认：

- `.githooks/pre-push` 已存在
- `TEST_PENDING.md` 已存在
- `git config --get core.hooksPath` 输出 `.githooks`
- 执行 `git push` 时能看到提醒输出

## 12. 一句话总结

**`TEST_PENDING.md` 负责记录未完成项，`.githooks/pre-push` 负责在 `git push` 前提醒，`core.hooksPath` 负责把它们连起来。**
