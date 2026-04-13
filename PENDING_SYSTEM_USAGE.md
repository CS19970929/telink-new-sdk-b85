# 待办提醒系统使用说明

这份文档说明如何使用当前这套“系统级待办提醒”方案。

## 1. 这套方案做什么

它会在你执行 `git push` 前检查仓库里的待办文件：

- `TODO.md`
- `TEST_PENDING.md`

只要里面存在 `- [ ]` 条目，就会先输出提醒。

另外，它还提供一个初始化命令：

- `pending-init`

这个命令用于在新项目里自动生成待办模板。

## 2. 先决条件

当前方案已经安装到用户目录：

- `C:\Users\Administrator\.codex-pending\bin`

并且该目录已经加入到系统 `PATH`。

如果你刚安装完，建议：

1. 关闭当前 VS Code 终端
2. 重新打开一个新的终端

这样新 PATH 才会生效。

## 3. 新项目怎么初始化

进入一个新的 Git 项目根目录后，直接执行：

```powershell
pending-init
```

它会自动创建：

- `TODO.md`
- `TEST_PENDING.md`

如果文件已经存在，脚本不会覆盖它们。

### 初始化后的结果

初始化完成后，仓库里会有类似这样的内容：

```text
repo-root/
  TODO.md
  TEST_PENDING.md
```

## 4. `TODO.md` / `TEST_PENDING.md` 怎么写

这两个文件都支持同样的格式：

```md
# TODO

## Pending items

- [ ] item 1
- [ ] item 2
- [x] item 3
```

规则很简单：

- `- [ ]` 表示未完成，会在 `git push` 前触发提醒
- `- [x]` 表示已完成，不会触发提醒

## 5. `git push` 时会发生什么

当你执行：

```powershell
git push
```

系统会先检查当前仓库的：

- `TODO.md`
- `TEST_PENDING.md`

如果发现未完成条目，会打印类似这样的提醒：

```text
WARNING: unfinished items found
Please review the following list before push:
File: ...\TODO.md
- [ ] item 1
...
```

然后再继续执行真正的 `git push`。

## 6. 在 VS Code 里怎么用

你平时如果用的是 VS Code 终端，直接在终端里执行：

```powershell
pending-init
```

即可。

如果你想绑定快捷键，可以把按键配置成向终端发送 `pending-init`。

示例：

```json
{
  "key": "ctrl+alt+p",
  "command": "workbench.action.terminal.sendSequence",
  "args": {
    "text": "pending-init\n"
  }
}
```

这样在当前终端所在的新项目里，按快捷键就能初始化。

## 7. 旧项目怎么处理

如果旧项目已经有 `TODO.md` 或 `TEST_PENDING.md`，不需要额外处理。

如果旧项目两个文件都没有，而你又想启用这套机制，直接在该项目根目录执行：

```powershell
pending-init
```

## 8. 常见问题

### Q1: 需要手动配置 `core.hooksPath` 吗？

不需要。当前这套方案走的是系统级命令包装，不依赖仓库 hook。

### Q2: 还会不会碰到 `sh.exe`、`chmod` 这类问题？

不会。现在不依赖 Git hook 的 shell 解释器。

### Q3: `pending-init` 找不到怎么办？

先关闭当前终端，再开一个新的终端。

如果还是找不到，说明当前会话没有拿到新的 `PATH`。

### Q4: `git push` 没有提醒怎么办？

先确认当前终端里执行的是系统里的 `git`，并且仓库根目录存在 `TODO.md` 或 `TEST_PENDING.md` 的 `- [ ]` 条目。

## 9. 推荐工作流

推荐你以后按这个流程使用：

1. 打开一个新项目
2. 在终端里运行 `pending-init`
3. 开发过程中把未完成事项写进 `TODO.md`
4. 提交前把不需要提醒的项改成 `- [x]`
5. 执行 `git push`

## 10. 一句话总结

`pending-init` 负责新项目初始化，`git push` 前的提醒负责帮你盯住未完成事项。
