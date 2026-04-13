# TODO 一条命令初始化指南

这份说明告诉你如何在新的项目里，用一条命令把 `TODO.md` 提醒机制装好。

## 1. 这套方案做什么

- 在仓库根目录创建 `TODO.md`
- 创建 `.githooks/pre-push`
- 设置 `core.hooksPath`
- 在 `git push` 前提醒未完成事项

## 2. 一条命令

在项目根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\init-pending-hook.ps1
```

如果你把脚本放在别的位置，把路径换成对应位置即可。

## 3. 运行后会发生什么

脚本会自动：

1. 创建 `.githooks` 目录
2. 写入 `pre-push` hook
3. 创建 `TODO.md` 模板
4. 执行 `git config core.hooksPath .githooks`

## 4. 最小模板

脚本生成的最小结构如下：

```text
repo-root/
  .githooks/
    pre-push
  TODO.md
  tools/
    init-pending-hook.ps1
```

## 5. 兼容旧项目

如果旧项目已经有 `TEST_PENDING.md`，这套 hook 也会继续识别它。

也就是说：

- 以后推荐用 `TODO.md`
- 旧项目的 `TEST_PENDING.md` 仍然可用

## 6. 手动版

如果你不想用脚本，也可以手动做三步：

```powershell
New-Item -ItemType Directory -Force .githooks
git config core.hooksPath .githooks
```

然后再把 `TODO.md` 和 `.githooks/pre-push` 补上。
