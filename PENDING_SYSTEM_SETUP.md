# 系统级待办提醒方案

这套方案把提醒机制做成全局入口，目标是让你在任何新项目里都能直接用，不再依赖仓库 hook 的 shell 兼容性。

## 方案结构

- `git.cmd` 作为全局包装器，拦截 `git push`
- `pending-init` 作为新项目初始化命令
- `TODO.md` 和 `TEST_PENDING.md` 作为待办模板

## 为什么这比 hook 更稳

- 不依赖 `sh.exe`
- 不依赖 Git hook 执行权限
- 对 VS Code 终端更友好
- 只要在 PowerShell 或 cmd 里输入 `git push`，都能先提醒

## 一次性安装

在这个仓库根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install-pending-system.ps1
```

安装后要做两件事：

1. 重新打开 VS Code 终端
2. 以后在新项目里直接运行 `pending-init`

## 新项目初始化

在新项目根目录执行：

```powershell
pending-init
```

它会创建：

- `TODO.md`
- `TEST_PENDING.md`

## `git push` 的行为

安装后，`git push` 前会检查这两个文件中的 `- [ ]` 条目，并打印提醒。

## VS Code 快捷键

如果你想一键初始化，可以在 VS Code 里绑定快捷键到终端发送：

```json
{
  "key": "ctrl+alt+p",
  "command": "workbench.action.terminal.sendSequence",
  "args": {
    "text": "pending-init\n"
  }
}
```

## 兼容性说明

- 这是 Windows 方案
- 在 PowerShell 和 cmd 里都能用
- 如果你在 Git Bash 里工作，需要单独做 Bash 版入口
