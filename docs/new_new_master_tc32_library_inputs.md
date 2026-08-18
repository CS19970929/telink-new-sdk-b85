# `new-new-master` TC32 官方预编译库构建输入

## 背景

`project/tlsr_tc32/B85/825x_ble_sample/makefile` 的链接参数固定包含：

```text
-llt_825x -llt_general_stack
```

因此 `proj_lib/` 必须包含 `liblt_825x.a` 和 `liblt_general_stack.a`。该分支此前忽略了所有 `*.a` 文件且未纳入这两份链接输入，导致干净签出后链接器报：

```text
cannot find -llt_825x
```

## 受控输入

两份文件与受控功能安全工具链分支中的官方 SDK V3.4.2.8 Patch_0001 副本一致：

| 文件 | SHA-256 |
| --- | --- |
| `proj_lib/liblt_825x.a` | `3a51224d664f9d54e1746b5dc2d9b48eec727020d8dc038ea51b8b64390aae17` |
| `proj_lib/liblt_general_stack.a` | `e0331419862be31572b4df5b3a4ed4b82e8c2526bfba92ff7fc71d2c589c74a0` |

`.gitignore` 保留了通用的 `*.a` 忽略规则，但为上述两个精确路径添加反向例外。二者是 TLSR825x 固件的版本化构建输入，不得用其他 SDK 版本的同名库替换。

## 验证

在 Windows 当前 IDE 工程目录执行：

```powershell
$env:PATH = 'C:\TelinkSDK\opt\tc32\bin;' + $env:PATH
C:\qp\qtools\bin\make.exe -j 4 all
```

链接器成功生成 `tc_ble_single_sdk_B85.elf`，不再出现 `cannot find -llt_825x`。IDE 的 `tl_check_fw.sh` 在 Windows 直接启动时会产生 Error 193，但该后处理规则明确为忽略错误，且不影响本次链接验证；正式 BIN 后处理应使用项目固定的 Windows `tl_check_fw2.exe` 流程。
