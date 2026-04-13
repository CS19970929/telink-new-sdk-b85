# 系统级安装方案

这份说明用于把 `TODO.md` / `TEST_PENDING.md` 提醒机制做成“装一次，以后直接用”的形式。

## 1. 目标

你希望达到的效果是：

- 新建项目后，不需要进仓库手动找脚本
- 在 VS Code 终端里直接输入一个命令就能初始化
- 初始化后自动生成模板文件和 hook
- 以后 `git push` 前自动提醒未完成事项

## 2. 结论

- **不必须使用 PowerShell**，但在 Windows 上它最稳
- VS Code 终端本身没有限制，关键是你在终端里能调用到哪个命令
- 最推荐的做法是：安装一个全局命令 `pending-init`

## 3. 现在提供的工具

仓库里已经有这些脚本：

- [`tools/pending-init.ps1`](./tools/pending-init.ps1)
- [`tools/pending-init.cmd`](./tools/pending-init.cmd)
- [`tools/install-global-pending-tools.ps1`](./tools/install-global-pending-tools.ps1)

## 4. 一次性全局安装

在当前仓库根目录执行一次：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\install-global-pending-tools.ps1
```

这个安装器会：

1. 把 `pending-init.ps1` 和 `pending-init.cmd` 复制到用户目录
2. 把安装目录写入用户 `PATH`
3. 让你以后在新项目里直接运行 `pending-init`

## 5. 新项目中怎么用

进入新项目根目录后，直接执行：

```powershell
pending-init
```

它会自动：

- 创建 `.githooks/pre-push`
- 创建 `TODO.md`
- 创建 `TEST_PENDING.md`
- 设置 `core.hooksPath = .githooks`

## 6. VS Code 快捷键方案

你可以把 VS Code 里的快捷键绑定到“终端发送命令”：

- 命令：`pending-init`
- 执行位置：当前项目根目录的终端

如果你想只按一个键，做法一般是：

1. 给 VS Code 配一个键位绑定
2. 让它向终端发送 `pending-init`

这部分属于 VS Code 用户设置，不在仓库里自动改。

一个可用的示例是把快捷键绑定到终端发送序列：

```json
{
  "key": "ctrl+alt+p",
  "command": "workbench.action.terminal.sendSequence",
  "args": {
    "text": "pending-init\n"
  }
}
```

说明：

- 这会把 `pending-init` 发送到当前活动终端
- 你需要确保当前终端所在目录是新项目根目录
- 如果你习惯用其他组合键，可以改成你自己的按键

## 7. 为什么建议保留 PowerShell 版本

因为 Windows 上做文件创建、路径处理、环境变量配置，PowerShell 最稳定。

但这不代表你必须一直手写 PowerShell 命令：

- 你可以在 VS Code 终端里直接敲 `pending-init`
- 你也可以把它绑定到快捷键
- 以后只需要记住一个命令名，不需要记脚本细节

## 8. 模板文件

初始化后会生成：

```text
repo-root/
  .githooks/
    pre-push
  TODO.md
  TEST_PENDING.md
```

其中：

- `TODO.md` 是主模板
- `TEST_PENDING.md` 是兼容旧项目的模板
- `pre-push` 会同时检查两个文件

## 9. 旧项目兼容

如果旧项目只有 `TEST_PENDING.md`，也没问题。

如果新项目只想用 `TODO.md`，也没问题。

当前 hook 会同时识别两者。

## 10. 你最常用的方式

如果你通常都在 VS Code 终端里工作，推荐流程是：

1. 先做一次全局安装
2. 以后每个新项目打开终端
3. 直接运行 `pending-init`
4. 再开始正常开发

## 11. 最小手工版

如果你不想装全局命令，也可以只在当前仓库执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\pending-init.ps1
```

这仍然可以在 VS Code 终端里直接运行。
