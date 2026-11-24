# D008 构建与测试

依赖 Python、GNU Make 和仓库指定的 Telink TC32 工具链。用 `TC32_BIN` 指向工具目录，把 `BMS_BUILD_ROOT` 指向源码树外的临时目录。Windows 工具为含空格的构建路径准备映射，Linux 目标构建使用无空格路径。

```text
python bms_tools/build_options.py show
python bms_tools/bms.py sources --check
python bms_tools/bms.py --production --d008-profile 16s-lfp link --jobs 4
python bms_tools/bms.py --production --d008-profile 16s-lfp resources
```

生产候选装配为 `16s-lfp`、`20s-nmc`、`23s-lfp`、`24s-lfp`、`custom`。自定义装配先保存类型和串数。直接链接任务只生成 ELF/MAP/LST；各生产装配分别执行资源门禁。生产 BIN 由产品签核门控制。

`custom` 的构建及发布目录带 `-custom-`，即使串数和化学体系与固定装配相同，也有独立的构建收据和输出目录。

## VS Code 配置窗口

`Ctrl+Shift+B` 打开 **BMS 源码配置与编译 · D008**。窗口恢复开发/生产模式、清理重编和按源码编译。串数、化学体系、额定容量和 OTA 策略以已保存的源码为准。旧板换串数或电芯类型使用出厂包。

- **保存到源码** 写回 D008 装配和参数头。参数更新包由工具生成一次性 ID。
- **仅链接 ELF** 按当前模式和装配生成 ELF/MAP/LST。勾选清理时，先清当前装配的构建输出。
- **按源码编译** 生成并验证 BIN。勾选 **生成后发送安卓并请求 OTA** 时，验证通过后调用现有 Android Sender。发送开关默认关闭。
- **保护参数 / AFE 位配置** 打开 [D008 源码参数和寄存器窗口](AFE_CONFIGURATION_EDITOR.md)。保存后回到主窗口自动读取新参数和更新标识。

`D008 生产链接（无镜像）` 仍可通过 VS Code 的运行任务菜单直接选择。终端也可用 `python -B bms_tools/configure_d008.py` 打开配置窗口。

## 软件验证

```text
python tests/run_host_regression.py --output <外部目录>
python tests/run_host_regression.py --with-targets --output <外部目录>
cmake -S . -B <外部构建目录>
cmake --build <外部构建目录> --parallel
ctest --test-dir <外部构建目录> --output-on-failure
```

主机回归生成断言、输入指纹、日志和报告。可用支持 ASan/UBSan 的编译器对 CMake 目标启用 `-fsanitize=address,undefined`。CI 链接全部装配，主机执行生产 SOC、校准、保护、Flash 循环、掉电模型、Modbus、SIF、UART/OTA 及休眠相关用例。

正式量产确认需要实装 MID、实板满空端和保护、两种 SIF、UART 接管、BLE、真实掉电和 OTA、睡眠电流与唤醒。主机测试和目标链接按各自可验证范围记录。
