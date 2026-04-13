# TEST_PENDING 最小模板

这个文件给你一个可以直接复制到新项目里的最小版本。

## 1. `TEST_PENDING.md` 模板

把下面内容放到仓库根目录的 `TEST_PENDING.md`：

```md
# TEST PENDING

- [ ] 任务 1
- [ ] 任务 2
- [ ] 任务 3
```

如果你已经完成某项，就把它改成：

```md
- [x] 任务 1
```

## 2. `.githooks/pre-push` 模板

把下面内容保存为 `.githooks/pre-push`：

```sh
#!/bin/sh

REPO_ROOT=$(git rev-parse --show-toplevel 2>/dev/null)
PENDING_FILE="$REPO_ROOT/TEST_PENDING.md"

[ -f "$PENDING_FILE" ] || exit 0

if grep -q '^- \[ \]' "$PENDING_FILE"; then
    echo ""
    echo "========================================"
    echo "WARNING: TEST_PENDING.md 中仍有未完成事项"
    echo "文件: $PENDING_FILE"
    echo "以下条目需要先处理:"
    grep '^- \[ \]' "$PENDING_FILE"
    echo "========================================"
    echo ""
fi

exit 0
```

## 3. 启用命令

在仓库根目录执行：

```powershell
git config core.hooksPath .githooks
```

如果你需要手动创建目录：

```powershell
New-Item -ItemType Directory -Force .githooks
```

## 4. 推荐的最小使用流程

1. 新建 `TEST_PENDING.md`
2. 新建 `.githooks/pre-push`
3. 执行 `git config core.hooksPath .githooks`
4. 以后每次 `git push` 前检查提醒输出

## 5. 如果你想强制阻止 push

把 hook 里的提醒逻辑改成：

```sh
if grep -q '^- \[ \]' "$PENDING_FILE"; then
    echo "发现未完成事项，禁止 push"
    exit 1
fi
```

## 6. 复制时要记住的两点

- 这是**仓库级配置**，不是分支级配置
- `TEST_PENDING.md` 不存在时，默认不会提醒
