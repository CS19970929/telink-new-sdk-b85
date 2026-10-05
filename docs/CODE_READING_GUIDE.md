# 代码阅读与维护

从 `bms.code-workspace` 打开项目，默认聚焦 BMS 源码、产品数据、构建工具和测试。
SDK 仍是实际编译依赖，只在此阅读视图中隐藏；查启动、Flash、BLE ABI 时直接打开 SDK 路径。

| 要修改的内容 | 首先阅读 | 边界 |
|---|---|---|
| 项目差异、串数、引脚、Rsense | `bms/products/<产品>/` | 产品目录只有数据和源码清单 |
| OTA 是否覆盖参数 | 产品 `bms_parameter_policy.h`、`docs/OTA_PARAMETERS.md` | 九类独立编号，不能拿固件版本号代替 |
| 软件保护 | `bms/core/param.h`、`bms_sw_protection.c` | 默认值、校验、阈值/过滤/恢复；无寄存器操作 |
| AFE 硬件保护 | `bms_afe_hw_profile.c`、对应 `bms/afe/` 后端 | requested profile 与真实量化值分开 |
| 启动、采样、BLE、电源 | `bms/app/app.c` | 共用回调/初始化；DVC 与 SH 电源时序明确分支 |
| SOC/OCV/学习 | `bms/core/bms_soc.c`、`bms_soc_profile.h` | 样本驱动策略；OCV 曲线为数据 |
| 剩余充放电时间 | `bms/core/bms_soc_eta.c/.h` | 独立的输入/状态接口，无 SDK/Flash 依赖 |
| 加热/均衡/开线 | `bms/core/bms_features.c`、产品能力配置 | 硬件不存在的功能不会因参数更新而启用 |
| 输出安全授权 | `bms/core/bms_afe_guard.c` | 启动校验、通信失效、watchdog silence、新样本资格 |
| 参数与 Flash | `bms_config_store.c`、`bms_state_store.c`、`bms_event_log.c` | 语义数据归属；底层由 `storage_record` 和平台提供 |
| 工厂老化阶段 | `bms/core/bms_factory_mode.c` | 三天计时与工厂/正常模式 |
| DVC 固定启动配置 | `bms/afe/dvc1124/dvc1124_boot.c` | 每次初始化应用板级设置；不是另一套 Flash 参数存储 |
| 构建/资源 | `bms_tools/bms.py` | 同一入口、四产品源码清单和编译参数 |
| 静态检查 | `bms_tools/static_analysis.py` | 读取实际构建设置，维护分析覆盖及报告 |

## 本轮精简结果

基准为本轮修改前的 `3ee9bd0`，统计 `bms/` 下 C/H 物理行，不含 SDK、测试及文档：

| 项目 | 修改前 | 修改后 |
|---|---:|---:|
| 业务 C/H 总量 | 23966 | 23348 |
| `app.c` | 2490 | 1721 |
| SOC 主文件 | 2129 | 1976 |
| ETA 独立模块 | 含于 SOC | 147 |
| 构建入口 `bms.py` | 2359 | 1542 |
| 静态检查模块 | 含于构建入口 | 844 |

分文件本身不等于删代码。总量减少主要来自重复流程、旧测试命令、无用字段/宏、无调用的 SOC 接口和过时参数存储入口清理；
新增 OTA 编号、校验和独立 ETA 接口也计入修改后总量。代码可读性不以压缩成单行衡量。

已移除旧 EEPROM 参数选项、ParamVer 空壳、旧系统保留字段、假 watchdog 空宏、旧加热阈值分支、
以及应用层重复温度查表。低 SOC 字段及故障枚举按实际含义命名，不再叫 SOC High。
当前没有旧 schema 解码/迁移流程。原有协议地址和单位继续用于当前上位机，这些仍是正在使用的接口。

保留了现有 SOC 容量学习、ETA、D008 SIF、工厂运行及诊断能力，它们有当前调用方或明确测试用途。
容量学习默认关闭，但不是无用代码；今后若明确取消产品功能，再同时删除协议、算法和测试。
通信保护、存储提交、恢复条件及板级差异也不以减少行数为由合并掉。

## 修改后的验证

```sh
python bms_tools/bms.py --all-products sources --check
python tests/run_host_regression.py
python bms_tools/bms.py --all-products link --jobs 4
python bms_tools/bms.py --all-products resources
cmake -S . -B <源码树外目录> -DCMAKE_BUILD_TYPE=Release
cmake --build <源码树外目录>
ctest --test-dir <源码树外目录> --output-on-failure
```

生产模式需要干净提交，D008 必须指定实际装配 profile。`link` 只生成 ELF/MAP/LST。
软件温度组测试直接编译生产模块；ETA 测试及 CMake 核心测试使用实际源码/头文件。
SDK 相关调度及现有 SOC harness 仍需硬件桩和部分函数提取，不能据此声称全部测试都已无桩。
所有后续 Git 提交信息和 PR 变更说明使用中文。
