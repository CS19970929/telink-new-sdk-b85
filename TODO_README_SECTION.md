# README 可直接粘贴段落

把下面这段放到项目 `README.md`，就能说明 `TODO.md` 提醒机制。

## Git 提醒机制

本项目使用 `TODO.md` 记录未完成事项，并通过 `.githooks/pre-push` 在 `git push` 前给出提醒。旧项目中的 `TEST_PENDING.md` 也兼容识别。

### 一条命令初始化

在仓库根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\init-pending-hook.ps1
```

### 文件约定

- `- [ ]` 表示未完成，会触发提醒
- `- [x]` 表示已完成，不触发提醒

示例：

```md
- [ ] 任务 1
- [x] 任务 2
```

### 目录结构

```text
repo-root/
  .githooks/
    pre-push
  TODO.md
  tools/
    init-pending-hook.ps1
```

### 旧分支说明

`core.hooksPath` 是仓库级配置，不是分支级配置，所以同一个仓库里的多个分支共用同一套 hook。

如果旧分支没有 `TODO.md` 和 `TEST_PENDING.md`，则不会触发提醒。需要的话，可以把该文件和 hook 一起回补到旧分支。
