# BMS monorepo：D008 / D011 / D013 / D014

四个产品直接编译同一份 `bms/core/` 和 `bms/app/` 源码。产品选择只改变板级数据、AFE 后端和构建输出目录。
新增或修改公共模块后，无需复制、同步或更新子模块版本。

架构、存储格式、边界和迁移说明见 [实现说明](docs/BMS_MONOREPO.md)。验证结果见 [验证报告](docs/BMS_MONOREPO_VALIDATION.md)。

```powershell
# 检查四个产品的实际源码清单
python bms_tools/bms.py --all-products sources --check

# 只生成对象文件，或者 ELF/MAP/LST；不生成 BIN
python bms_tools/bms.py --product d008 compile --jobs 4
python bms_tools/bms.py --all-products link --jobs 4
python bms_tools/bms.py --all-products resources

# Host 回归：需要 Python 3.11+ 和 CC 指定的本机 C 编译器
$env:CC='C:/qp/qtools/MinGW32/bin/cc.exe'
$env:PATH='C:/qp/qtools/MinGW32/bin;'+$env:PATH
python tests/run_host_regression.py

# 可移植核心库；构建目录放到源码树外
cmake -S . -B "$env:LOCALAPPDATA/CodexTemp/bms-core-host" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$env:LOCALAPPDATA/CodexTemp/bms-core-host"
ctest --test-dir "$env:LOCALAPPDATA/CodexTemp/bms-core-host" --output-on-failure
```

`--product` 默认是 D014，也可设置 `BMS_PRODUCT`。工具同时支持 `--all-products`。
构建产物默认位于 `%LOCALAPPDATA%/CodexTemp/bms-monorepo-build/<checkout-hash>/<mode-profile>/<product>/`，
可用 `BMS_BUILD_ROOT` 改变外部输出根目录。测试日志同样位于用户临时区，可用 `BMS_TEST_OUTPUT` 指定源码树外的位置。

只有明确需要固件镜像时才执行 `build`/`rebuild`、`check-fw`、`map`、`manifest`、`verify`。
它们必须带同一个产品选择。ELF/MAP 和 host 通过不等于已烧录或实板通过。

SDK 自带 Eclipse 示例工程的旧自动源码扫描不再是构建入口；使用此处的产品清单和命令。
VS Code 任务也调用同一个工具。Windows 上位机的单一来源继续是
`feature/windows-afe-hw-protection-editor-v2` 分支下的 `bms-tool-windows/`。

生产 ELF（不生成 BIN）使用已提交、干净工作区：

```sh
python bms_tools/bms.py --all-products --production --d008-profile 16s-lfp link --jobs 4
python bms_tools/bms.py --all-products --production --d008-profile 16s-lfp resources
```

D008 必须按实际电池显式选择 `16s-lfp`、`20s-nmc` 或 `24s-lfp`。
CI 同时构建这三种 D008 配置和 D011/D013/D014。Linux 通过 `TC32_BIN` 指定官方 TC32 的 bin 目录；
Windows 保留现有工具路径和可选 runner。构建策略不等于实板签核，见 [修复记录](docs/BMS_MONOREPO_REMEDIATION.md)。
