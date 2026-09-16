# D008 Flash 分域升级与寿命控制实现

本次基于 `2c6c2cc87a20cf68c0d93685208cfb205d57f3fd` 实现，适用 `refactor/d008-common-bms-features`。原始缺陷与旧布局计算保留在 [Flash 审核](D008_FLASH_STORAGE_AUDIT_2026-09-17.md)，本文描述新行为。项目仍处开发期：不实现旧参数迁移。

## 1. 分区与格式

物理地址、OTA A/B、Factory、SDK pairing/MAC 保留区全部保持 [STORAGE.md](STORAGE.md) 的布局。通用 Journal 仍为显式 little-endian、CRC32、sequence、commit-last。Config/State/Event 的 payload schema 升为 **2**；读不到当前格式时初始化该域默认值并持久化。首次从 schema 1 升级会重置三个域，**Config 中的旧蓝牙名称也不迁移**；后续同 schema 的分类 revision 更新保留名称。Factory 无 writer，不受影响。

| 域 | payload / 对齐后 slot | 每扇区记录 | 扇区数 | 理想完整轮转记录 |
|---|---:|---:|---:|---:|
| Config | 300 / 332 bytes | 12 | 4 | 48 |
| State | 32 / 64 bytes | 64 | 8 | 512 |
| Event | 406 / 440 bytes | 9 | 8 | 72 |

Config 保留 3 个不用的历史 control 槽位作为 reserved；SOC 状态、Event、runtime 的升级标记已由各自域拥有，不读取这些 reserved 槽位。保留源码 facade 名称不代表支持旧 Flash 格式。

## 2. OTA 独立更新操作

在 `conf.h` 修改下表对应 revision，并更新相应默认值，然后通过既有构建和 OTA 流程更新固件。所有 revision 初始为 1；相等保留已有值，不相等恢复该类默认。**0 也是有效 revision，不表示关闭更新。** 固件版本变化本身不触发重置；同镜像重复启动不会重复重置。主动回退 revision 也会重置该类参数，应在构建说明中明确。

| 宏 | 内容 | 标记/数据所在记录 | 默认值维护入口 |
|---|---|---|---|
| `FW_UPGRADE_RESET_PROTECT_EPOCH` | MCU 软件 First/Second/Third/Recover/Filter | Config | `param.h` / `E2P_PROTECT_DEFAULT_PRT` |
| `FW_UPGRADE_RESET_AFE_HW_EPOCH` | AFE Requested hardware protection | Config | `bms_afe_hw_profile_build_default()` |
| `FW_UPGRADE_RESET_SOC_CONFIG_EPOCH` | chemistry/profile、额定容量、deadband、OCV 静置时间/误差带、学习开关/显示策略 | Config | `bms_soc_get_default_config()`、`d008_product_profile.h`、`CapacityFactory` |
| `FW_UPGRADE_RESET_SYSTEM_EPOCH` | 其余 system 字段 | Config | `bms_config_store_get_default_system()` |
| `FW_UPGRADE_RESET_SOC_EPOCH` | SOC、DSG、cycle、learned capacity、learning flags | State | `BMS_STATE_DEFAULT_*` |
| `FW_UPGRADE_RESET_RUNTIME_EPOCH` | aging runtime minutes | State | 0 min |
| `FW_UPGRADE_RESET_EVENT_LOG_EPOCH` | 清空事件和重复次数 | Event | 空 ring |

例：只更新 AFE 参数时修改 AFE 默认生成函数和 `FW_UPGRADE_RESET_AFE_HW_EPOCH`；保持其他 revision 不变。同 schema 设备的软件保护、SOC 配置/运行状态、事件和名称保留。仅调整 SOC 算法配置不自动清除已有 SOC 或学习容量；若产品要求一并重置，显式提高 SOC 状态 revision。

AFE 默认构造仍复用当前已存在的编译期保护常量，并按原 DVC 边界进行归一化；它不再读取 `g_tParam.protect` 或旧 Flash。软件参数运行时写入不会联动硬件参数。改变共用默认常量后，已配置设备只有对应 revision 改变才更新。产品阈值仍需签核，不因本次存储改动增加猜测值。

DVC 型号、串数/Rsense、GPIO、WDT、Body-Diode 等固定板级配置仍在编译期拥有，每次 AFE init 重申；不进入此配置存储。`HW=0` 本身不修改 Requested profile。

## 3. 启动与失败一致性

启动顺序：安全 GPIO/输出禁止 → Config revision 事务 → State revision 事务 → Event revision 事务 → LoadParam 校验 → AFE init/固定配置/HW profile 应用与读回 → 新样本资格 → 输出仲裁。

- 每个域内，候选数据和相关 revision 位于同一条 CRC/commit 记录；Config 候选保存成功才发布 RAM。
- Config 多个分类同时更新时一次保存；State 两个分类同时更新时一次保存。不存在单独写标记的中间窗口。
- 三个物理域之间不是全局原子事务。若 Config 已提交而 State 失败，下一次启动保留 Config，继续未完成域；**当前启动的输出门禁保持禁止**。
- 门禁由 `s_storage_upgrade_valid` 与软件参数有效性共同决定；普通 `SaveParam()` 不能解除启动升级失败。修复存储后重新启动完成资格流程。
- 当前格式记录被 CRC 拒绝时回退到上一条有效记录；没有可读记录时应用默认。语义非法参数不伪装成有效配置。
- 移除旧温度参数兼容迁移、AFE 空 profile 的运行时迁移写入，以及分离的 `param_upgrade_mark_epoch()`。
- 单次写入返回失败且读回状态不确定时，不声称 Flash 必然未写入；重启由完整 CRC/commit 记录裁决。主机 byte-cut 用例验证完整旧/新记录，没有半套数据。

改变 24S/20S 编译装配属于产品更换。若保留同格式 Flash，必须显式更新 SOC-config revision/相关保护 revision，不能只改 firmware version 后假定所有参数同步改变。

## 4. SOC、事件与掉电取舍

正常主循环将最新 SOC/DSG/cycle 合并到 State pending，默认至少 60 s 才尝试一次有变化的 checkpoint。学习结果也加入 pending。`write_all()` 同步刷新时包含 pending learning；runtime 保存也带上当前 pending 值。未变化不写。通信显式保存、工厂复位、runtime 的既有定时保存和受控关机不受普通 SOC 合并间隔限制，但受失败退避限制。

事件在 RAM ring 中先接受，latch 表示已接受到 pending。相邻同类事件在未保存且 60 s 窗口内合并，重复计数饱和到 65535；多个不同事件保留顺序。默认每 60 s 保存脏快照，写失败保留 pending；最多 100 条，持续风暴可能覆盖更早事件。原 Modbus event code/interval 编码不变；读取展示 RAM 当前快照，未必已持久化。`bms_event_log_read_repeat()` 为 C 诊断入口，本次未新增通信寄存器。

受控断电仍严格执行：同步 State → 同步 Event/sleep-attempt → AFE shutdown → 最后 PC4 低。保存失败不切断电源；重复失败不重复追加 sleep-attempt。sleep 事件表示关机尝试，不证明实际掉电。

正常且存储成功时，非受控断电可能丢失最近不足约 60 s 的 pending SOC/事件；发生写失败、OTA 占用或调度延迟时窗口可能更长，**不保证 60 s 是绝对最大丢失量**。断电后的未知时间不补积分或静置计时。

SOC 参数由 Config 持久化，`bms_soc_configure()` 保存成功才发布运行配置；`bms_soc_set_product_config()` 复用同一事务。实际额定容量读取 Config.system，范围 1..10000（0.1 Ah），学习容量同样限制到 10000，以约束既有 32-bit SOC 运算。OCV 曲线仍为编译期数据，OTA 替换代码即可更新，未扩展为可写曲线协议。历史 `0x2009/0x200A` 未接入，不能当作本次新增接口。

电流策略保持：`abs(current_ma)<=200 mA` 双向显示为零、SOC 不积分，但允许作为静置候选，须满足有效新鲜电压和稳定时间；原始 mA 诊断保留。suspend 退出为双向 >=500 mA。

## 5. 写入退避、诊断与寿命

State/Event 保存失败后至少 5 s 再尝试。平台层对 program/erase 校验失败统一退避至少 5 s，保护其他同步写入入口；OTA 工作或 SDK Flash session 活跃时拒绝业务写入。异步 pending 留待后续保存，同步 API 返回失败。所有业务写入仍在主循环上下文。

新增 `bms_storage_platform_get_diagnostics()`：本次上电以来 program/erase 次数、校验失败次数、推迟次数，以及 program/erase（含读回校验）最大 SDK 32k tick 耗时。计数饱和，诊断不自己写 Flash。可用调试器/C API 采集；不增加 Modbus 接口。时间基准沿用 `pm_get_32k_tick()`，不是假设 32768 Hz。

以原审核的 **100k 擦除寿命假设**计算，新几何理想预算为 Config 4.8M、State 51.2M、Event 7.2M 条成功记录。若 State/Event 持续每分钟写一次，数学磨损时间约 97.4/13.7 年；按 10% 工程折减约 9.74/1.37 年。该折减不是厂商保证，也不能把数学值理解为超过保持寿命的产品寿命。事件风暴、失败后的脏 slot、反复上电、手动参数写入都会改变结果。应根据实测日写入/擦除次数评估，不宣称已验证整机寿命。

本次未实现预擦除或 BLE 安全时间窗调度，未改 SDK Flash driver；增加测量入口后，应先在实际 Flash MID/BOM 和温度/电压条件下测最坏擦除延迟。SDK 擦除可能影响 200 ms 采样和 BLE，软件已有 >400 ms 样本间隔拒收不等于实时性风险已消除。

## 6. 验证与交付门禁

- `tests/d008_storage_host_check.py` 编译执行实际 Config/State/Event/param 和 Journal C；遍历每个元数据/payload/commit 字节中断点，验证独立 revision、保留无关参数、重复启动、失败不发布、门禁、pending/关机刷新、回绕。参数默认构造/语义校验在该 harness 中为明确 stub，不冒充产品阈值验证。
- 平台 adapter 通过另一个 C harness 验证分页、OTA/session 拒绝、失败退避/回绕和诊断；耗时为 mock 时间，不是实测。
- `tests/flash_quick_check.py` 执行实际 Journal：三域当前 slot 几何/轮转擦写分布、CRC 回退、sequence 回绕和掉电点。
- 既有 SOC/PM/guard/current C 测试与 Host contracts 保留；固定 source order 仍为 93 个输入。
- GitHub Actions 对本次提交执行 Host 测试和 24S/20S × SW/HW 1/1、1/0、0/1、0/0 clean build、check-fw、size、MAP、manifest/verify；默认 24S 1/1 cppcheck。实际结果以对应 commit 的运行记录为准。
- 实板 OTA A/B 断电、Flash 延迟、PC4 掉电保持、AFE readback/三帧资格、200/500 mA 精度仍见 `HARDWARE_VALIDATION.md`，不能由 CI 关闭。
