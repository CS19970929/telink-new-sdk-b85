# TLSR8251 源码与链接顺序管理

## 1. 为什么必须管理顺序

TC32 链接器按命令行给出的对象顺序放置输入 section。即使每个 `.o` 的内容完全相同，对象顺序变化也会改变函数和数据地址，继而改变重定位、MAP、ELF 和 BIN。通常这不等于功能变化，但在没有反汇编与硬件回归证据时不能把差异当作无影响。

本工程曾出现 IDE BIN 与命令行 BIN 不同。检查证明 81 个对象逐个哈希完全相同，根因仅是 `SocEnhance.o` 和 `div_mod.o` 的链接位置不同；按 IDE 顺序重链接后 ELF 和 BIN 都与 IDE 字节级一致。

## 2. 权威输入

`bms_tools/source_order.txt` 是无 IDE 构建的权威源码和链接顺序。`build/rebuild` 只读取这个文件生成 `project/tlsr_tc32/B85/825x_ble_sample_cli/sources.mk`，不会在构建期间改写顺序。该目录与 IDE 的 `project/tlsr_tc32/B85/825x_ble_sample/` 是同级独立目录，不共享对象或固件。

严格检查同时保证：

- 清单中的每个 `.c` / `.S` 都真实存在；
- 受管目录中没有未列出的新源文件；
- 没有重复项或仅大小写不同的冲突路径；
- 对象链接顺序与清单逐项一致。

## 3. 新增、删除和重命名源文件

新增文件后不写 Makefile。标准流程是：

```powershell
python bms_tools/bms.py sources --update
git diff -- bms_tools/source_order.txt
python bms_tools/bms.py sources --check
python bms_tools/bms.py rebuild --jobs 4
python bms_tools/bms.py manifest
python bms_tools/bms.py verify
```

`vendor/ble_sample/` 递归发现 `.c` 和 `.S`，支持项目代码使用子目录。新增全新的顶层源码组、头文件 include 路径、全局宏、链接库、C++ 文件或单文件特殊 flags 时，仍需修改工具链配置并评审；工具不能安全猜测这些语义。

修改任何头文件后使用 `rebuild`，因为当前 TC32 构建没有生成完整的头文件 `.d` 依赖。

## 4. 可选的 IDE 对照

IDE 不是构建依赖，仅可作为迁移审计的输入：

```powershell
# 只比较，不修改任何文件
python bms_tools/bms.py sources --compare-ide

# 明确采用当前 IDE 生成顺序；执行后必须审核 Git diff
python bms_tools/bms.py sources --import-ide
```

不进行后台自动导入。原因是 Eclipse CDT 的 `subdir.mk` 属于生成文件，可能陈旧、来自增量构建或被本地设置改变。无提示同步会使同一次源码提交生成不同固件，违反可复现和认证追溯要求。

如果以后完全删除 IDE 生成目录，不影响 `sources --check`、构建、manifest 或 CI；只有两个显式 IDE 对照命令不可用。

## 5. Manifest 与发布门禁

`manifest` 格式 v3 除固件哈希外，还记录：

- `source_order.txt` 文件哈希与规范化顺序哈希；
- 源文件数量、对象顺序哈希；
- 每个源文件对应对象的大小和 SHA-256；
- `build.mk` 与 `boot.link` 的 SHA-256；
- 两份 Telink Vendor `.a` 的大小和 SHA-256。

`verify` 会重新检查上述输入。对象被替换、漏编、重排，顺序文件、链接脚本、构建参数或 Vendor 库被改动，都会返回失败。清单生成以后若继续编译，必须重新生成 manifest，不能沿用旧清单烧录。

## 6. 何时允许顺序变化

新增、删除、重命名源文件或有意对齐新的已验证基线时可以改变顺序，但必须：

1. 审核 `source_order.txt` 的 Git diff；
2. 执行 `rebuild`、`check-fw`、`size`、`map`、`manifest`、`verify` 和 `static`；
3. 与发布参考 BIN/MAP 做基线比较并解释地址变化；
4. 在真实 TLSR8251 BMS 上完成受影响功能回归；
5. 将固件、ELF、MAP、manifest 和测试记录归档。

因此，工具链能自动处理新增源文件的编译规则，但不会自动批准可能改变固件布局的顺序变化。
