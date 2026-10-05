# 资源预算与验证口径

适用四产品 TLSR8251，32 KiB SRAM、124 KiB APP 槽；构建命令见 [BUILD_AND_TEST](BUILD_AND_TEST.md)。历史资源数字只适用于各报告明确记录的提交/模式/profile。

## ELF 与 BIN 分开验收

- `link` + `resources`：使用 ELF/MAP/LST 和输入收据；镜像字节数是含预计 16 B 对齐及 4 B CRC 的投影，不生成 BIN。
- 镜像流程的 `map` + `manifest` + `verify`：核对实际 BIN、CRC、长度、输入和产物 hash。
- `_ram_use_end_ - 0x840000` 是静态地址跨度，包含 RAM code、SDK/cache、data、BSS、对齐等，不能用 data+bss 替代。
- `__SRAM_SIZE` 必须为 `0x848000`，静态终点必须低于栈顶减 3072 B。IRQ 栈另为 384 B，已计 BSS。
- 生产 Flash 余量低于 8192 B 为失败；开发配置为告警。预留主栈后的 RAM 余量低于 2048 B 为告警，不能当实测栈安全证明。
- 生产仅公共 core 使用 `-Os`，SDK/AFE/平台保持 `-O2`；均保留 TC32 ABI 和零编译 warning 门禁。

`gen/resources.json` 记录占用、余量及配置；`--baseline` 可比较旧报告，但必须匹配产品、profile、模式和工具口径。单次较大增长应定位 MAP 符号与真实可达路径，删源文件不必然节省 Flash。

`compile-inputs.json` 记录源/头文件、库、宏、Git 身份及工具指纹；`link-completed.json` 对 ELF/MAP/LST 绑定成功链接，镜像流程另有 `build-completed.json`。源码、宏、工具或身份改变会使旧收据失效，失败构建不能为旧 ELF 背书。

## 主栈与 IRQ 栈观测


启动汇编在 BSS/data 初始化后、首次调用 C `main` 前，分别以 `0xA5A5A5A5` 填充 `_bms_main_stack_bottom_..__SRAM_SIZE` 和 `bms_irq_stack_bottom..bms_irq_stack_top`。不覆盖活跃 C 帧、持久 no-init 或 SDK cache。

`bms_stack_monitor_poll()` 在主循环处理后执行，每次每个栈最多扫描 64 个连续 word，另检查底部 4 个 guard word。它不擦写 Flash、不修改活跃栈、不在 ISR 中扫描。

调试器读取 `g_bms_stack_watermark`：

| 字段 | 意义 |
|---|---|
| `valid_mask` | bit0 主栈、bit1 IRQ；对应位为 1 才完成过一轮扫描 |
| `main_free_min_bytes` | 主栈从底部起连续未被改写的最小观测字节数 |
| `irq_free_min_bytes` | IRQ 栈对应指标，单独计算 |
| `overflow_flags` | bit0 主栈、bit1 IRQ 底部 guard 被触碰，复位前保持 |

主栈观测使用量 = `__SRAM_SIZE - _bms_main_stack_bottom_ - main_free_min_bytes`；IRQ 对应 `384 - irq_free_min_bytes`。这是“被写过的深度”：仅调整 SP 而未写入的帧、碰巧等于填充值的数据、尚未运行的路径均可能使水位低估需求。必须结合反汇编和压力测试，不能称为完整内存自检或栈溢出隔离。guard 被破坏也不能保证系统仍可继续可靠运行。

水位通过 ELF 符号/调试器采集。正常 suspend 保持观测历史；完整重启/普通 deep sleep 重新初始化。当前四产品 `PM_DEEPSLEEP_RETENTION_ENABLE=0`；将来开启 retention 必须重新审查启动恢复路径和水位生命周期。


## 共享与持久化开销

公共修改直接进入四产品，清单以各产品 `sources.txt` 为准；无需向旧分支同步补丁。D008 开发默认 16S LFP，其他 profile 必须按实际装配选择。

Config/State/Event 使用公共 journal。State/Event 周期 checkpoint 约 60 s，失败退避约 5 s；事件先进入 RAM 并更新边沿状态，掉电/深睡可能丢失未落盘窗口。运行调试日志是另一份纯 RAM 环，与 Flash 事件区分，见 [STORAGE](STORAGE.md) 和 [运行日志](RUNTIME_DEBUG_LOG.md)。

## 实板证据

主栈与 IRQ 栈压力、最坏 Flash 阻塞、200 ms 采样 gap、BLE/OTA、保护时序和故障风暴必须实测。记录准确 commit、模式/profile、板号、仪器、环境和波形；已生成镜像时再附 BIN hash。官方容器、Windows、host 和实板证据分别保留，不把“链接成功”写成设备安全验收。
