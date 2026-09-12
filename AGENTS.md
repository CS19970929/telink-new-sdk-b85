# AGENTS.md — TLSR8251 BMS / HS-D008 / DVC1124 开发规则

当前目标硬件为 HS-D008 + TLSR8251 + DVC1124-2；默认 AFE 型号为 DVC1124-22，默认 24S，同时保留 20S 变体能力。本文档跟随项目主线能力，不绑定历史分支名。

本文件是 Codex / ChatGPT / 其他 AI Agent 修改本仓库时必须遵守的长期工程约束。若代码、原理图、AFE 手册和本文件发生冲突，不得自行猜测；先指出冲突及证据，再决定修改方案。

## 1. 平台与产品边界

- MCU：Telink TLSR8251 / TLSR825x B85 系列。
- CPU：TC32，不是 ARM Cortex-M；禁止使用 ARM GCC、普通 GCC 或 ARM 链接脚本。
- SDK：`tc_ble_single_sdk V3.4.2.8_Patch_0001`，禁止自动升级。
- 应用：`tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/`。
- 启动：`boot/B85/cstartup_825x.S`，入口 `__start`。
- 链接：`project/tlsr_tc32/B85/boot.link`。
- 当前产品板：HS-D008。
- 当前 AFE family：DVC1124-2。
- 默认 AFE：DVC1124-22。
- 默认串数：24S；20S 为受支持变体，必须通过配置实现，禁止复制一套驱动。

当前工程声明目标为 TLSR8251，但沿用 SDK 的 `MCU_STARTUP_8258` 启动配置；脚本会明确显示该目标配置风险。未完成真实硬件和内存边界复核前，不得静默修改启动宏或链接脚本。

## 2. DVC1124 权威依据

DVC1124 相关实现必须以以下材料为权威依据：

1. 用户提供的 HS-D008 原理图；
2. 用户提供的 DVC1124-2 数据手册；
3. 用户提供的 DVC1124-2 参考手册；
4. 已经通过实板/逻辑分析仪验证并在 Git 中有可追溯记录的行为。

禁止根据 SH367309、SH3673520、BQ769xx 或其他 AFE 的经验推断 DVC1124 的：

- I2C 地址和地址模式；
- CRC 覆盖范围；
- 寄存器地址；
- bit 定义和 W1C/W0C/RC/触发位语义；
- ADC 数据格式和符号位；
- 电压、电流、温度换算；
- COV/CUV/OCD/OCC/SCD 量化规则；
- GP 复用；
- CHG/DSG/PCHG/PDSG 行为；
- Sleep/Shutdown/WDT/故障锁存和恢复条件。

资料不能证明的结论必须标记 `TODO_VERIFY_HW`，同时保留 fail-safe 默认行为。不得把猜测写成量产默认值。

## 3. DVC1124 软件分层

DVC1124 新功能优先放置在：

- `vendor/ble_sample/dvc1124.c`：DVC1124 器件级驱动、I2C、CRC、寄存器、测量换算、基础 FET/均衡/断线/保护配置；
- `vendor/ble_sample/dvc1124_bms.c`：DVC1124 与现有 BMS 数据结构、故障、保护、MOS 许可逻辑之间的适配；
- `vendor/ble_sample/dvc1124.h`：公开 API 和数据类型；
- `vendor/ble_sample/dvc1124_project_config.h`：板型/型号/地址/串数/Rsense/NTC 等项目配置。

SH367309 遗留文件已从当前产品源码删除。通用 BMS 报告、系统状态、错误计数、温度查表和分级故障历史归 `bms_state.*` / `bms_error.h` 所有；禁止为新 AFE 恢复 `sh367309_datadeal.*` 或复制一套 BMS 业务代码。

## 4. DVC1124 I2C 地址规则

Telink B85 `i2c_master_init()` / `i2c_set_id()` 使用包含 R/W 位位置的 8-bit transfer address。本项目 DVC 驱动统一保存“写地址”（LSB=0），读地址由驱动生成 `addr | 0x01`。

当前普通单芯片模式默认：

- Write transfer address：`0x40`
- Read transfer address：`0x41`
- 默认型号：`DVC1124_MODEL_22`

地址、型号、串数等默认配置集中在 `dvc1124_project_config.h`。禁止在 `dvc1124.c`、`app.c` 或业务模块中散落硬编码 AFE 地址。

支持：

- `DVC1124_ADDR_FIXED`：普通固定地址模式；
- `DVC1124_ADDR_HARDWIRED`：按器件硬件地址脚配置计算目标地址；
- `DVC1124_ADDR_EXPLICIT`：仅改变 MCU 访问目标地址，用于硬件本身已配置成对应地址的板型。

必须区分：

> 修改 MCU 目标地址 != 修改 DVC1124 芯片的物理硬件地址。

DVC1124-22 与 DVC1124-24 的硬件地址编码资源不同；不得把两者的 strap code 范围混用。任何新增型号必须先依据手册更新 `DVC1124_ResolveWriteAddress()` 和测试。

## 5. HS-D008 板级接口约束

当前分支的板级定义集中在 `conf.h` / DVC 项目配置中。修改 GPIO 前必须同时核对 HS-D008 原理图和当前代码，不允许沿用旧 SH367309 板的 GPIO 记忆。

当前关键网络包括：

- PD4：`MCC-EN-RF`
- PD7：`MCU-AFE-EN`
- PA0：`ACC-MCU`
- PA1：`MCC-EN-HT`
- PB1：`CHG-IN`
- PC0：DVC1124 SDA
- PC1：DVC1124 SCL
- PC2：`OWC-TX`
- PC3：`OWC-RX`
- PC4：`MCU-LDO`
- PA7：SWS 调试/下载脚，默认禁止普通业务复用

SOC/LED、NTC、GP5/GP6 等网络若原理图和现有代码仍有待确认项，必须保持 `TODO_VERIFY_HW`，不得仅凭旧项目 pin 名推断。

特别注意：旧项目的 `MCC_C_PIN`、`AFE_CTL_PIN`、`ADC_NTC_PIN`、`ADC_VBUS_PIN`、`ADC_NMOS_PIN` 等名称来自 SH367309 板型，已从 D008 应用路径删除。不得重新引入这些旧 pin 名或把它们映射到 HS-D008 的 PA1、PC4、SOC LED 等真实引脚；DVC 输出门控和辅助测量统一通过 `bms_afe.h`。

## 6. 禁止污染 Telink SDK 头文件

`app_config.h` 会通过 `gpio_default.h -> user_config.h` 在 Telink driver headers 解析期间被间接包含。

因此严禁在 `app_config.h` 中使用宏重定义官方 SDK 函数，例如：

```text
gpio_write
gpio_set_func
gpio_set_input_en
gpio_set_output_en
adc_base_init
adc_sample_and_get_result
i2c_*
uart_*
pm_*
```

否则会把官方头文件里的函数声明本身也做宏展开，导致编译器报错或产生更隐蔽的 ABI/调用问题。

当前应用路径不使用旧器件函数名、虚拟 pin 或 SDK API alias。不得通过大范围宏劫持 SDK API 实现新 AFE；必须通过明确的 board/AFE API 调用收口。

## 7. 测量与保护原则

DVC1124 采样与保护实现必须保持以下边界：

- Cell1..Cell24 只处理 `cell_count` 内有效通道；20S 模式不得让 Cell21..24 参与 min/max、保护、SOC、均衡。
- Rsense 必须由配置给出；当前 HS-D008 配置值为 `200 uOhm`，若 BOM/原理图变更必须同步修改并重新验证电流和 OCP 量化。
- 电流方向必须在文档和代码中统一；任何方向修改必须同时回归 SOC、保护、Modbus/BLE 上报和 MOS 恢复逻辑。
- 硬件 COV/CUV/OCD/OCC 配置应从现有 BMS 参数体系量化，禁止建立第二套无法同步的保护参数。
- 阈值量化必须明确采用向安全方向取整还是最近值；不能静默溢出、截断或把非法值编码成“关闭保护”。
- SCD 若没有已确认的产品阈值/延时，保持禁用并标记待验证，禁止猜一个量产值。
- DVC 的故障锁存/清除必须遵守手册；不能把“写 0”“进入 sleep”“电流归零”简单等价为故障已经消失。
- MOS 打开请求必须经过当前保护许可；存在适用方向的三级保护、SCD 或 AFE 通信异常时不得无条件重新打开 MOS。

任何保护修改都必须同时检查：检测条件、过滤时间、AFE 硬件动作、软件故障位、MOS 实际命令、恢复条件、故障日志。

## 8. 低功耗与失效安全

这是已出货/面向出货的电池产品。低功耗原则为：

> 无法证明可以安全休眠时，保持运行。Fail-safe toward RUN。

禁止普通业务模块直接新增 `pm_stop`、`cpu_sleep_wakeup(DEEPSLEEP...)`、AFE Shutdown、MCU-LDO release 等不可恢复路径。

任何 sleep/deep sleep 修改必须检查：

- CHG-IN、ACC 和其他唤醒源；
- GPIO 当前电平是否已经等于 wake active level；
- 是否会出现 sleep -> immediate wake -> sleep 的反复唤醒；
- BLE connected / advertising / disconnected 状态；
- UART/I2C/Flash 是否正在事务中；
- DVC1124 Sleep/Wakeup 后第一次通信是否可靠；
- MOS 和保护状态是否在模式切换前后保持正确；
- 是否存在用户无法通过充电器或 ACC 恢复的路径。

不要利用进入低功耗来“顺便清除”DVC 锁存故障。

## 9. 固定工具链

- `C:\TelinkIoTStudio\opt\tc32\bin\tc32-elf-gcc.exe`
- 编译器版本：GCC `4.5.1-tc32-1.3`
- 官方预编译库：`proj_lib/liblt_825x.a`、`proj_lib/liblt_general_stack.a`
- 官方固件后处理：`script/tl_check_fw/tl_check_fw2.exe`
- 烧录：Telink BDT；默认路径 `C:\TelinkIoTStudio\tools\libusbBDT\bin`

禁止擅自升级编译器、SDK、ABI、启动代码、链接脚本或替换预编译库。两份 `.a` 文件属于构建输入，必须保留在 Git 中并由 manifest 记录 SHA-256。

当前 `build.mk` 使用 `tc32-elf-ld` 直接链接。当前锁定的 Windows TC32 安装执行 `tc32-elf-gcc -print-libgcc-file-name` 只返回裸文件名，且安装中没有目标端 `libgcc.a`；因此量产构建不得依赖 `__muldi3`、`__divdi3` 等 64-bit runtime helper。禁止链接 host GCC、ARM GCC 或其他 TC32 版本的 `libgcc` 来掩盖问题。

新增代码应尽量避免不必要的 64-bit 乘除，尤其是高频采样路径；能在明确溢出边界下用 32-bit 定点算法实现时优先使用 32-bit。但优化前后必须有数值范围证明和回归测试，不能为了减代码尺寸破坏精度或溢出安全。

## 10. 唯一命令入口

在仓库根目录执行：

```powershell
python bms_tools/bms.py env
python bms_tools/bms.py sources --check
python bms_tools/bms.py build --jobs 4
python bms_tools/bms.py rebuild --jobs 4
python bms_tools/bms.py size
python bms_tools/bms.py map
python bms_tools/bms.py manifest
python bms_tools/bms.py verify
python bms_tools/bms.py static
python bms_tools/bms.py ci --jobs 4
```

`build` 和 `rebuild` 会自动完成 ELF、MAP、LST、raw BIN、官方后处理 BIN 的生成。命令行产物统一放在 `project/tlsr_tc32/B85/825x_ble_sample_cli/`，与 IDE 的 `project/tlsr_tc32/B85/825x_ble_sample/` 完全分离。

可烧录文件只有：

```text
project/tlsr_tc32/B85/825x_ble_sample_cli/825x_ble_sample.bin
```

`825x_ble_sample.raw.bin` 是中间产物，禁止当成量产固件烧录。

仓库路径含空格时，脚本会建立 `C:\opencode\bms_repo` junction 供 GNU Make 使用，不需要手工修改路径或系统 PATH。

## 11. 固定编译与链接参数

```text
tc32-elf-gcc -ffunction-sections -fdata-sections -Wall -O2
  -fpack-struct -fshort-enums -finline-small-functions
  -std=gnu99 -fshort-wchar -fms-extensions
  -D__PROJECT_8258_BLE_SAMPLE__=1 -DCHIP_TYPE=CHIP_TYPE_825x

startup assembly: -DMCU_STARTUP_8258

link: tc32-elf-ld --gc-sections -L proj_lib -T boot.link
      ... -llt_825x -llt_general_stack <matching tc32 libgcc.a when required>
```

不要仅为了“现代化”改用 CMake；`build.mk` 是保持旧固件编译语义的薄驱动，源文件规则由 Python 自动生成。

## 12. 源码与链接顺序

`bms_tools/source_order.txt` 是参与构建的 `.c` / `.S` 和对象链接顺序的权威输入。不得依赖文件系统枚举顺序，也不得让 `build/rebuild` 静默改写它。

- 新增、删除或重命名源文件后，先执行 `python bms_tools/bms.py sources --update`，审核并提交 `source_order.txt` 的 Git diff，然后执行 `sources --check` 和 `rebuild`。
- `vendor/ble_sample/` 递归发现项目源文件；新增其他顶层源码组时需审核并修改 `SOURCE_GROUPS`。
- 若保留 IDE 生成文件，可用 `sources --compare-ide` 只读对照；只有明确决定采用 IDE 顺序时才能执行 `sources --import-ide`。IDE 文件不是构建依赖。
- 修改任何头文件后必须执行 `rebuild`；当前构建不依赖自动生成的头文件 `.d` 文件，不能用旧对象判断成功。
- 顺序变化会改变链接地址和 BIN。重大变化必须比较 ELF/MAP/BIN 并做真实硬件回归。

## 13. 修改边界

- `vendor/ble_sample/` 是项目代码，可按需求修改，但应保持最小侵入。
- `vendor/common/`、`drivers/B85/`、`common/` 是官方 SDK 边界，原则上不修改。
- `boot/B85/`、`boot.link`、`proj_lib/*.a` 禁止随意修改。
- 不允许为了适配 DVC1124 去修改官方 `drivers/B85/adc.h`、`gpio.h`、`i2c.h` 等 SDK 头文件。
- 不引入 RTOS、C++、动态内存或与当前工具链不兼容的语言特性。
- 不做与当前任务无关的大规模格式化、目录重排、变量改名或架构重写。
- 不改变既有 BLE/Modbus 协议、Flash 参数地址、OTA 布局、参数迁移行为，除非任务明确要求并完成兼容性验证。

## 14. 编译与验证门禁

任何 `vendor/ble_sample/` 代码修改后至少执行：

```powershell
python bms_tools/bms.py sources --check
python bms_tools/bms.py rebuild --jobs 4
python bms_tools/bms.py check-fw
python bms_tools/bms.py size
python bms_tools/bms.py manifest
python bms_tools/bms.py verify
python bms_tools/bms.py static
```

若 `rebuild` 失败，必须先修复编译/链接错误，不得把“代码已提交”描述成“功能已完成”。

AFE/保护/MOS/低功耗相关修改还必须做真实硬件验证。至少覆盖：

1. DVC1124 I2C 时钟、地址、CRC 帧；
2. CHIP_VERSION / 基础寄存器读取；
3. 24S 电芯采样和 20S mask；
4. VTOP / PACK / LOAD；
5. CC2 电流零点、方向、线性和大电流范围；
6. GP/NTC 温度；
7. CHG/DSG 实际栅极动作；
8. COV/CUV/OCD1/OCC1/OCD2/OCC2 触发、延时、锁存、恢复；
9. SCD 在产品参数明确后单独验证；
10. 均衡、断线检测；
11. AFE Sleep/Wakeup；
12. BLE/Modbus/SOC/Flash/OTA 回归。

在 CHG/DSG 极性、保护参数和故障恢复未验证前，不要直接使用真实大电流电池包做首次 bring-up；优先使用电池模拟器、限流电源、电子负载和逻辑分析仪。

## 15. 临时文件边界

- 禁止在仓库内创建或使用 `.codex_tmp/` 存放 Codex/ChatGPT 的日志、截图、文档渲染、表格审计、解压文件或其他中间产物。
- 此类临时文件统一放到 Windows 用户临时目录，例如 `$env:LOCALAPPDATA\CodexTemp\tlsr8251-bms\<task-id>`；任务结束后可按需清理。
- 工程命令自身的可复现构建和静态分析证据仍按既定规则写入 `project/tlsr_tc32/B85/825x_ble_sample_cli/`。
- 需要交付给用户的正式文件应写入用户明确指定的位置；未指定时先说明目标位置，不得为了中间处理在仓库根目录新增临时目录。

## 16. 静态分析与发布

静态分析由 cppcheck 执行，只检查 `vendor/ble_sample` 应用层。官方 SDK 头文件仅为保持真实类型、宏和条件编译而解析，其诊断按可追溯范围清单排除，不纳入问题统计。结果输出到 `project/tlsr_tc32/B85/825x_ble_sample_cli/static/`。Cppcheck 结果不能描述为完整 MISRA-C 合规结论。

烧录前先执行 `verify`，禁止全片擦除或擦除 `0x74000..0x7FFFF`。重大修改后用 `baseline <reference.bin>` 比较尺寸、SHA-256 和首末差异位置，并在真实 TLSR8251 板上完成现有功能冒烟测试。

构建、源码顺序和发布门禁见 `docs/BUILD_AND_TEST.md`；架构与 AFE 移植边界见 `docs/ARCHITECTURE.md`；HS-D008 / DVC1124 硬件基线见 `docs/DVC1124_HS_D008.md`；未完成的实板项目统一记录在 `docs/HARDWARE_VALIDATION.md`。
