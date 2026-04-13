# README 可粘贴段落

下面这段可以直接放进项目 `README.md`，用于说明 `TEST_PENDING.md` 提醒机制。

## Git 提醒机制

本项目使用 `TEST_PENDING.md` 记录未完成事项，并通过 `.githooks/pre-push` 在 `git push` 前给出提醒。

### 启用方式

在仓库根目录执行：

```powershell
git config core.hooksPath .githooks
```

如果 `.githooks` 目录不存在，先创建：

```powershell
New-Item -ItemType Directory -Force .githooks
```

### 文件约定

`TEST_PENDING.md` 中：

- `- [ ]` 表示未完成，会触发提醒
- `- [x]` 表示已完成，不触发提醒

示例：

```md
- [ ] 任务 1
- [x] 任务 2
```

### `pre-push` 行为

当 `TEST_PENDING.md` 中存在未完成条目时，`git push` 前会输出提醒信息。

如果希望改成“禁止 push”，可以将 hook 中的提醒逻辑改为在检测到未完成项时 `exit 1`。

### 旧分支说明

`core.hooksPath` 是仓库级配置，不是分支级配置，所以同一个仓库里的多个分支共用同一套 hook。

如果旧分支没有 `TEST_PENDING.md`，则不会触发提醒。需要的话，可以把该文件和 hook 一起回补到旧分支。

### 最小模板

```text
repo-root/
  .githooks/
    pre-push
  TEST_PENDING.md
```
