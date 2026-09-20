# 构建、测试与发布门禁

## 1. 固定目标环境

正式固件使用项目锁定的 `tc32-elf-gcc 4.5.1-tc32-1.3`、Telink B85 Vendor 库、现有 `boot.link` 和 `tl_check_fw2.exe`。不得用 host GCC/clang、ARM GCC 或其他 ABI 的成功结果替代 TC32 production build。

```powershell
python bms_tools/bms.py env
python bms_tools/bms.py sources --check
python bms_tools/bms.py rebuild --jobs 4
python bms_tools/bms.py check-fw
python bms_tools/bms.py size
python bms_tools/bms.py map
python bms_tools/bms.py manifest
python bms_tools/bms.py verify
python bms_tools/bms.py static --no-report
```

## 2. Source-order

`bms_tools/source_order.txt` 是版本化链接顺序清单。源码集合改变时才运行 `python bms_tools/bms.py sources --update`，并人工检查 diff。

### 多 worktree 并行构建

`bms.py` 会按当前 worktree 绝对路径生成独立的 `C:\opencode\bms_repo_<hash>` junction。不同产品分支可以并行构建，不能改回所有分支共用一个固定 junction，否则 Make 可能读取另一 worktree 的源码并污染 OBJ/MAP/BIN。单个 worktree 的输出目录仍是唯一的，同一 worktree 不应同时启动两个 clean/build。

## 3. Host contracts：按产品运行

### D008 / DVC1124
`dvc1124_config_quick_check.py`、`d008_framework_contract_check.py`、`d008_20s_profile_contract_check.py`、software protection、AFE HW profile/access、SOC、Flash。

### D011 / SH3673510 10S
`sh3673520_contract_check.py`、D011 integration、protection-mode、temperature、comm-mode、software protection、AFE HW profile/access、SOC、Flash。

### D013 / SH3673510 4S

使用当前 `.github/workflows/bms-ci.yml` 里的 D013 integration contract，加 SH family、software protection、AFE HW profile/access、SOC、Flash contracts。工作流文件是测试入口的最终事实；不要因为源码仍有 `D011_*` 命名而运行错产品逻辑。

## 4. CI 的含义

Host contracts 主要验证源码结构、常量、映射和协议契约；TC32 job 验证真实目标编译/link/check-fw/MAP/manifest/verify/cppcheck。两者都不能证明 D013 的 GPIO/AFE/NTC/串数/Rsense 与真实板一致；在 D013 原理图缺失时尤其不能把 CI 绿色当硬件验收。

## 5. 发布门禁

1. `sources --check` 和当前产品 Host contracts 通过；
2. 固定 TC32 clean rebuild 通过；
3. `check-fw`、size/MAP、manifest/verify、cppcheck 通过；
4. 先补齐 D013 原理图/BOM，再完成 `HARDWARE_VALIDATION.md`；
5. 实测记录绑定原理图/BOM版本、板号、AFE型号、固件commit、requested/effective、仪器和结论。

Flash/OTA布局以当前 linker、`bms_tools` 和 `STORAGE.md` 为准，不按 D011/D008 历史文档猜地址。
