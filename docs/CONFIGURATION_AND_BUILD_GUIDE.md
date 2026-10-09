# 配置与简单修改指南

适用四产品 monorepo。[根 README](../README.md) 为总目录；环境和命令见 [构建与验证](BUILD_AND_TEST.md)。以下路径相对仓库根目录。

## 1. 三类数据与生效时机

| 类别 | 来源 | 何时生效 |
|---|---|---|
| 编译期产品/硬件输入 | `bms/products/<product>/` | 编译并运行新固件；固定 AFE 工作设置在 reset/init 时应用 |
| 运行参数 | Config journal，启动加载到公共模块 | 已保存值通常优先；按分组更新编号决定覆盖 |
| 运行状态 | State/Event journal 和 RAM | SOC、循环、事件由各模块维护 |

修改头文件不是在线修改设备。当前内部 journal schema 3 内，编号相同保留设备值，编号不同写入该类默认；旧 schema 拒绝且不迁移。八类及一个保留槽、回刷和失败处理见 [OTA_PARAMETERS](OTA_PARAMETERS.md)。

## 2. 修改位置

| 要改什么 | 入口 | 范围/更新编号 |
|---|---|---|
| 产品选择 | `bms.py --product`；D008 另用 `--d008-profile` | 不靠修改源码清单切产品 |
| 容量、编译名称/软件版本、通信模式 | 产品 `bms_product.h` | 容量为 BUSINESS；编译名称/版本不等于用户 SN/蓝牙持久后缀 |
| 电池类型及关联默认值 | 产品 `BMS_PRODUCT_CHEMISTRY`；D008 `d008_product_profile.h`；公共 `bms/products/bms_battery_defaults.h` | 联动 SW、AFE、BUSINESS、SOC 的编译默认；已保存值仍按各组编号处理 |
| SH GPIO、串数、Rsense、NTC、能力、AFE 默认 | 产品 `bms_product.h` | 固定设置直接编译；`SH3673510_HW_DEFAULT_*` 对应 AFE |
| DVC 固定配置和 SCD 种子 | D008 `bms_product.h` 包含的私有 `dvc1124_product_defaults.h` | 固定板级值或 AFE，按 default builder 区分 |
| 软件保护默认 | `bms/core/bms_config_store.c` 的 `s_default_protection`；电压常量来自 `bms_battery_defaults.h` | 公共默认影响四产品；CUV3 延时保持产品 `BMS_DEFAULT_CUV3_FILTER`；SW |
| heater/balance 默认 | `bms/app/bms_features.h`、`bms/products/bms_battery_defaults.h`、`bms_config_user_defaults()` | 均衡起始电压跟随类型；BUSINESS；能力禁用仍优先 |
| SOC 配置/OCV 曲线 | `bms/core/bms_soc.c`、`bms_soc_profile.h` | SOC，另评估 SOC_STATE |
| OTA 更新策略 | 共享 `bms/products/bms_parameter_policy.h` | 八类独立编号及一个保留槽，不用软件版本代替 |
| 开发日志 | `EXTRA_DEFINES`、`bms_debug_log_config.h` | 编译期，生产禁用 |

产品 include 路径由构建器选择。各模块显式引入所需产品配置；公共整数类型使用 `stdint.h`。State/Event 保存周期归各自实现，有限重试周期由 `bms_storage_platform.h` 定义。

**D008 特殊点：** `bms_afe_hw_profile_build_default()` 从编译期软件默认取初始种子，再规范化 DVC 数值；SH 使用独立 `SH3673510_HW_DEFAULT_*` 覆盖。运行时仍独立保存/修改。改公共软件保护默认值 可能同时改变 D008 新设备的 AFE 默认，不能只看 SW 编号。

### 编译时选择磷酸铁锂或三元锂

日常使用 **Ctrl+Shift+B** 单项目窗口选择装配、电池类型和容量，保护/均衡默认值自动联动，不需要输入编译命令或修改头文件。窗口操作、编号延续和 OTA 说明见 [构建指南第 6 节](BUILD_AND_TEST.md#6-ctrlshiftb-单项目配置与编译)。

底层唯一类型入口为 `BMS_PRODUCT_CHEMISTRY`，仅接受 `BMS_SOC_CHEMISTRY_LFP`（1）或 `BMS_SOC_CHEMISTRY_NMC`（2）。SOC profile 自动匹配类型；`AUTO`、未知类型或显式指定不匹配的 SOC profile 均在编译时报错。

- **D011 / D013 / D014：** 在对应产品 `bms_product.h` 的默认定义处选类型，或通过 `EXTRA_DEFINES` 加入 `-DBMS_PRODUCT_CHEMISTRY=BMS_SOC_CHEMISTRY_NMC`；选 LFP 时使用 `BMS_SOC_CHEMISTRY_LFP`。未覆盖时默认 LFP，电压保护及均衡保持原有数值。串数不随化学体系改变。
- **D008：** 继续选择编译宏 `D008_PRODUCT_PROFILE`，构建工具使用 `--d008-profile 16s-lfp` / `20s-nmc` / `24s-lfp`。`20s-nmc` 自动选择 NMC 的整套关联默认，另两个选择 LFP。不能额外用不匹配的类型宏覆盖 profile；例如 `16s-lfp` 配 NMC 会编译报错，防止镜像名称与实际配置矛盾。本次不新增装配组合。

共享参数表只有编译期常量，没有新增运行时状态或调度逻辑。默认值如下，均为 mV；软件一级、二级、三级分别列出：

| 参数 | LFP（保留现有值） | NMC（本次确认的 4.20 V 开发默认） |
|---|---|---|
| 单体过压 First / Second / Third / Recover | 3750 / 3750 / 3750 / 3500 | 4200 / 4200 / 4200 / 4100 |
| 单体欠压 First / Second / Third / Recover | D008：3000 / 3000 / 2800 / 3100；SH：3000 / 3000 / 3000 / 3100 | 3000 / 3000 / 3000 / 3100 |
| AFE 过压 / 恢复 | 3750 / 3500 | 4200 / 4100 |
| AFE 欠压 / 恢复 | D008：2800 / 3100；SH：3000 / 3100 | 3000 / 3100 |
| 总压过压 First / Second / Third / Recover，按每串折算 | 3500 / 3600 / 3650 / 3500 | 4200 / 4200 / 4200 / 4100 |
| 总压欠压 First / Second / Third / Recover，按每串折算 | 3000 / 3000 / 2900 / 3000 | 3000 / 3000 / 3000 / 3100 |
| 均衡起始电压 | 3400 | 4100 |
| 均衡启动 / 停止压差 | 50 / 30 | 50 / 30 |
| SOC 通用曲线 | `BMS_SOC_PROFILE_GENERIC_LFP` | `BMS_SOC_PROFILE_GENERIC_NMC` |

总压字段实际单位仍为 10 mV，默认初始化按表中单串值除以 10 再乘有效串数。AFE 默认请求仍经过现有后端量化/恢复逻辑，未修改寄存器编码。硬件范围依据：DVC1124-2 RM V1.2 第 23–24 页；SH36735XX CV1.0A 第 49 页；资料入口见 [AFE_REFERENCE_GUIDE](AFE_REFERENCE_GUIDE.md)。这些范围依据不代替电芯规格或产品签核。

均衡开关、压差、充电会话、温度和采样可信度门禁保持原逻辑；D013 的均衡能力仍为关闭。过流、短路、温度、滤波延时、Rsense、NTC 和休眠参数保持各项目原配置；容量未在窗口覆盖时使用产品原默认。更换实际电池时，这些板级/负载参数仍需独立核对。

**已有设备不会仅因类型宏变化就覆盖 Flash 参数。** 在同 schema、同串数设备上切换整套默认，在窗口勾选“本次 OTA 恢复配置默认值”，自动使用 `BMS_BUILD_PARAMETERS_REVISION` 给 `SW`、`AFE`、`BUSINESS`、`SOC` 四组设置同一更新编号，再编译部署并回读确认。窗口修改类型/容量时自动勾选此项；不勾选时延续本机此前编号，无记录则采用源码编号。维护者仍可在 `bms_parameter_policy.h` 分组维护默认编号。`BUSINESS` 整组还包含容量和加热默认，不能当作仅重置均衡的开关；需要保留设备自定义值时应使用现有参数接口分别配置。实际更换电池时，窗口可独立勾选“更换电池：重置 SOC / 循环”，通过 `BMS_BUILD_SOC_STATE_REVISION` 更新 `SOC_STATE`，避免沿用旧电池数据。窗口编号范围 1..65535，本机自动递增不能保证与其他电脑刷入的设备编号不同，须回读核对。串数变化仍按现有逻辑重建整套 Config（含校准/身份）；本次未改变 Flash 布局。

相关检查入口 `tests/d014_defaults_host_check.py` 已包含 D008 三个 profile、三个 SH 产品的默认 LFP/NMC 参数矩阵，以及非法类型/profile 组合拒绝检查。它提取生产默认初始化及 AFE builder 执行，证据低于完整生产 TU 和实板；按根协作规则，仅在明确要求测试时运行。本次实现只做源码审查及 Git 差异检查，未执行这些用例或目标编译。

## 3. 单位速查

| 字段/边界 | 单位/例子 |
|---|---|
| `BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH`、`0x2318` | 0.1 Ah；116 = 11.6 Ah；范围见 `BMS_SOC_CAPACITY_MAX_0P1AH` |
| 对外容量报告 | 0.01 Ah，与名义容量相差 10 倍 |
| 公共 `current_ma` | mA，正放电负充电；SH 已在测量边界转换，不再取反 |
| 软件/AFE `*_a10` | 0.1 A；100 = 10 A |
| 软件电压 | 单体 mV；总压保护/报告字段为 10 mV，逐字段核对 |
| 温度 `*_x10` / 保护温度 | `(°C+40)*10`；25°C = 650，0°C = 400 |
| 软件 `Filter` | 10 ms；100 = 1000 ms，算法按 200 ms 样本向上取整 |
| AFE `*_delay_ms` / `sc_delay_us` | ms / µs，实际量化后查 effective |
| `timestamp_32k` | 32000 tick/s，无符号回绕差值 |
| 不存在的电芯槽 | 61001，不纳入有效串数/算法 |

## 4. 练习 A：打开开发日志

不修改硬件参数、不生成镜像；先读 [运行日志](RUNTIME_DEBUG_LOG.md)。

```powershell
$savedDefines = $env:EXTRA_DEFINES
$savedBuildRoot = $env:BMS_BUILD_ROOT
try {
    $env:BMS_BUILD_ROOT = "$env:LOCALAPPDATA/CodexTemp/bms-onboarding/log-debug"
    $env:EXTRA_DEFINES = '-DBMS_DEBUG_LOG_ENABLE=1 -DBMS_DEBUG_LOG_LEVEL=4'
    python bms_tools/bms.py --product d014 link --jobs 4
    if ($LASTEXITCODE -ne 0) { throw 'link 失败，查看 build.log' }
    python bms_tools/bms.py --product d014 resources
    if ($LASTEXITCODE -ne 0) { throw '资源检查失败' }
} finally {
    $env:EXTRA_DEFINES = $savedDefines
    $env:BMS_BUILD_ROOT = $savedBuildRoot
}
```

完成标准：找到此配置 ELF 和资源报告，理解日志 RAM 成本，恢复环境变量。读取设备日志还需明确生成并运行对应镜像；普通串口终端不会直接收到文本日志。

## 5. 例 B：某产品容量

假设批准需求为 D014 12.0 Ah，这只是操作示例，不是新的产品参数：

1. 改 `bms/products/d014/bms_product.h` 的 `BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH` 为 `120`，不改公共倍率。
2. 只影响空白设备则保持编号；要覆盖同 schema 设备则改变 D014 `BMS_UPDATE_BUSINESS_REVISION`。该组还会恢复 heater/balance，不能当“只重置容量”开关。
3. 更换电池/容量要评估 `SOC_STATE`，它同时重置 SOC 和循环状态。在线容量接口 `0x2318` 走持久化事务及 `bms_soc_nominal_capacity_changed()`，不要直接写全局量。
4. 查看 diff，跑 source 检查、参数/storage host、目标 link/resources；共享默认或算法变化则验证四产品。
5. 真正上板后分别记录默认容量 `0x2E08`、设备容量 `0x2318`、更新编号和启动结果；再测重启与回退。

## 6. 例 C：软件保护与 AFE 保护

软件参数从 `bms_parameters.h` 找字段，核对高/低阈值顺序、Third/Recover 回差、Filter 和使用者。电池类型相关电压默认集中在 `bms_battery_defaults.h`；CUV3 延时仍用各产品 `BMS_DEFAULT_CUV3_FILTER`。只改一款需要新增差异时，明确增加产品输入并保留其他产品值，这属于代码修改。

SH AFE 电压默认由电池类型表提供，其他 `SH3673510_HW_DEFAULT_*` 保持原 SH/产品配置；已保存的 AFE profile 按 AFE 编号生效，运行时与软件参数独立。D014 默认 OCD1 requested=10 A，但 667 µΩ 下 effective=15 A；相同 requested 不代表不同产品的动作电流相同。编译/host 之后仍需 requested/effective/readback 与 MOS 波形验证。

D008 三个装配 profile 的 SCD 默认开启：200 A、请求延时 256 µs，200 µΩ 下对应 40 mV，硬件量化后名义延时 249.92 µs（整数读回 250 µs）。当前 AFE 更新编号以 `bms_parameter_policy.h` 为准；同 schema 设备按编号是否变化决定是否整组恢复默认，详见 [OTA 参数更新](OTA_PARAMETERS.md#d008-开启短路保护2026-10-08)。`20s-nmc` 已联动上述 NMC 电压保护、均衡和 SOC 默认；profile 选择及开发默认不代表 NMC 产品签核，现有生产批准门禁不变。

相关入口：`core_contract_check.py`、`sw_temperature_groups_host_check.py`、`d014_configuration_contract_check.py`、`afe_hw_transaction_host_check.py`、`d014_defaults_host_check.py`。产品选择方法见构建指南；保护配置必须按批准值和边界向量验收。

## 7. 参数没变化时排查

依次核对：运行固件 Build ID/产品/profile → 编译开关 → 设备 CFG2 和更新编号 → 启动诊断/inhibit → AFE requested/effective → 产品通道能力。常见原因是已有值被保留、编号未变、固件未部署、硬件量化或能力禁用。

软件版本、编译名称、用户 SN、持久蓝牙后缀是不同字段；改 `BMS_PRODUCT_BLE_NAME` 不会清除已有后缀。内部 tag 为 8/11/13/14；D011/D013/D014 wire ID 仍为 2，不能为显示方便擅自改变。

在线配置使用 `feature/windows-afe-hw-protection-editor-v2` 的 `bms-tool-windows/`，先核对固件/工具能力。AFE 写入使用授权及完整 35-word 事务。协议地址、缩放、Flash/OTA 布局、底层寄存器不属于随手试改范围。
