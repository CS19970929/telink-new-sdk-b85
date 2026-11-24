# D008 构建与测试

依赖 Python、GNU Make 和仓库指定的 Telink TC32 工具链。用 `TC32_BIN` 指向工具目录，把 `BMS_BUILD_ROOT` 指向源码树外的临时目录。Windows 工具为含空格的构建路径准备映射，Linux 目标构建使用无空格路径。

```text
python bms_tools/build_options.py show
python bms_tools/bms.py sources --check
python bms_tools/bms.py --production --d008-profile 16s-lfp link --jobs 4
python bms_tools/bms.py --production --d008-profile 16s-lfp resources
```

生产候选装配为 `16s-lfp`、`20s-nmc`、`23s-lfp`、`24s-lfp`、`custom`。自定义装配先用 `build_options.py save` 保存类型和串数。默认构建任务只链接 ELF/MAP/LST；分别对每个装配执行资源门禁。已签核的发布才使用专用镜像入口。

`custom` 的构建及发布目录带 `-custom-`，即使串数和化学体系与固定装配相同，也有独立的构建收据和输出目录。

```text
python tests/run_host_regression.py --output <外部目录>
python tests/run_host_regression.py --with-targets --output <外部目录>
cmake -S . -B <外部构建目录>
cmake --build <外部构建目录> --parallel
ctest --test-dir <外部构建目录> --output-on-failure
```

主机回归生成断言、输入指纹、日志和报告。可用支持 ASan/UBSan 的编译器对 CMake 目标启用 `-fsanitize=address,undefined`。CI 链接全部装配，主机执行生产 SOC、校准、保护、Flash 循环、掉电模型、Modbus、SIF、UART/OTA 及休眠相关用例。

正式量产确认需要实装 MID、实板满空端和保护、两种 SIF、UART 接管、BLE、真实掉电和 OTA、睡眠电流与唤醒。主机测试和目标链接按各自可验证范围记录。
