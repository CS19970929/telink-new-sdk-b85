# Flash、SOC 持久化与低功耗

当前采用 Config/State/Event 三个 journal，内部 schema 4。**不读取或迁移旧参数格式**。
旧板刷入新程序时必须走明确的恢复出厂路径：有线烧录擦除业务扇区，或选择带一次性 ID 的
`factory_reset` 固件包。普通保参镜像遇非空白旧格式或损坏 Config 均禁止输出。
实施、逐轮提交及固定源码验证见 [存储精简记录](STORAGE_SIMPLIFICATION_20261017.md)。

## 数据与所有者

```text
Config（全部低频参数） / State（SOC/循环） / Event（历史）
    → storage_record（CRC32、序号、最终 commit、轮换与失败游标）
    → storage_port → bms_storage_platform_telink → SDK Flash
```

| 域 | 当前数据 | magic / schema / payload | 写入时机 |
|---|---|---|---|
| Config | 软件保护、AFE requested、容量/产品身份、完整 SOC 曲线与参数、四项休眠、业务许可、校准/SN/BLE 后缀、OTA 标记 | CFG4 / 4 / 414 bytes | 完整候选有变化，或启动默认/选中更新待提交 |
| State | 整数 SOC、放电累计、cycle、本域 OTA 标记 | ST4 + product ID / 4 / 24 bytes | 一秒任务检查，通常 60 s checkpoint，或显式设置/休眠 |
| Event | 写位置及 100 条事件 ID/时间码 | EV4 + product ID / 4 / 202 bytes | 新事件 dirty 且检查点到期，或入睡前 |

Config 的 chemistry/profile 和算法阈值只保存一次。SOC 消费 Config 内稳定只读视图，曲线提交改变后
代次递增，作废旧 OCV 静置资格。默认曲线集中定义在 `bms_soc_profile.c`；其校验及 word 映射不访问 Flash。
`bms_sleep_config.c` 只处理业务默认、校验和纯休眠策略；固定 AFE 异常 30 分钟只用编译常量。
没有独立 SleepConfig/SocProfile 缓存或 journal。

在线 Config 候选保存成功才发布缓存；启动候选须校验、提交成功后才取得安全配置资格。
State 只保留最后已提交值，最新业务值由 SOC 所有者提供，失败后的下一轮重新取当前值。
显式 SOC/循环设置先保存成功再修改算法。Event RAM 先记事件，写失败保留 dirty。
三个域彼此独立，不假定跨域原子性。getter 和协议读取只读 RAM，不初始化或擦写。
Flash 业务写入在主循环，ISR 不擦写。

## 分区与槽位

TLSR8251 512 KiB 分区：

| 区域 | 地址 | 大小 |
|---|---|---|
| Firmware A / OTA A meta | 0x00000..0x1EFFF / 0x1F000..0x1FFFF | 124 / 4 KiB |
| Firmware B / OTA B meta | 0x20000..0x3EFFF / 0x3F000..0x3FFFF | 124 / 4 KiB |
| Config | 0x40000..0x43FFF | 16 KiB / 4 sector |
| State | 0x44000..0x4BFFF | 32 KiB / 8 sector |
| Event | 0x4C000..0x5BFFF | 64 KiB / 16 sector |
| 未分配 | 0x5C000..0x73FFF | 96 KiB |
| SDK 保留 | 0x74000..0x7FFFF | 48 KiB |

原 SleepConfig/SocProfile 共 16 KiB 释放，不自动擦除。释放业务区不增加 APP 槽位。
1 MiB 的 Config/State/Event base 为 0xB0000/0xB4000/0xBC000；2 MiB 为
0x1B0000/0x1B4000/0x1BC000，仍使用 4/8/16 个 4 KiB sector。
地址只由 `flash_store_cfg.h` 定义，保留原 OTA 布局门禁及 SDK 地址边界。

32-byte 记录头加 payload，按四字节编程单位对齐后：

| 域 | 槽大小 | 每 sector 槽数 | 全区域槽数 |
|---|---:|---:|---:|
| Config | 448 B | 9 | 36 |
| State | 56 B | 73 | 584 |
| Event | 236 B | 17 | 272 |

Config 是低频参数，其槽数从 44 降至 36；State 从 424 增至 584，Event 从 144 增至 272。
无变化不写 Flash。寿命仍须按实装器件擦写保证、温度、电源、写频率和保持要求计算，不承诺固定年限。

## 启动、恢复出厂与故障

1. 当前 CFG4/schema 4 完整记录：加载最新有效记录；普通 OTA 只根据勾选组和固定 ID 修改选中参数。
2. Config 整区空白：初始化当前产品默认并提交；State/Event 分别按其默认策略加载或创建。
3. 非空白旧格式、未知格式、产品不符或损坏的 Config：普通镜像拒绝启动输出，不识别 CFG2。
4. **明确的 `factory_reset` 出厂包**（更新位 9，必须单独选择）：无论旧 Config 的格式和内容，
   都构造全新 CFG4 默认保护/AFE/容量/化学体系/完整 SOC 曲线/休眠/校准/SN/BLE 名称和业务参数。
   先持久化 Config 内本包 ID 的 `pending` 标记，重置 State（默认 SOC、累计、循环及 ID），
   再重置 Event（空历史），最后提交 Config `done` 标记。任一步失败或中途掉电都保持
   输出禁止，下一次使用相同出厂包启动继续完成；提交完成后的普通重启不重复恢复。
   如果回刷普通镜像时 Config 仍是 `pending`，同样禁止输出。
5. 恢复出厂不等于 AFE 已允许输出：仍需安全配置验证及原 AFE 应用/硬件回读/新鲜样本资格。
   物理 I/O 读取失败仍拒绝恢复，不能把 Flash 故障当成正常空白。

**有线烧录旧板**：优先擦除整个业务存储区后再写新 BIN、核验 Flash 已擦除为 `FF`。
512 KiB 型号为 `0x40000..0x5FFFF`（32 个 4 KiB sector）；1 MiB 为
`0xB0000..0xCFFFF`；2 MiB 为 `0x1B0000..0x1CFFFF`。包括旧版释放的两个业务分区。
另一选择是直接烧入独立 `factory_reset` 出厂包，由新固件完成三域逻辑初始化。
**不能整片 Chip Erase，也不能擦 APP A/B、OTA meta、MAC、配对及 SDK 射频校准区。**
出厂包仅使旧 Event 不再可见，不承诺对旧 Flash 扇区做信息安全意义上的全区域物理擦除。

Config 失败持续阻断输出；普通 State/Event 持久化故障维持独立降级，
但出厂事务的 State/Event 失败属于整板初始化未完成，必须阻断输出直至成功。

32-byte 头包含格式、schema、长度、sequence、CRC 和最终提交标记。记录显式 little-endian 编码；
平台按 256-byte page 分段编程并回读，擦除也验证。轮换不擦除最后有效记录所在 sector。
保存失败保留重试游标，跳过部分写入槽，不反复擦同一 sector。
每域连续三次实际写失败后，本次启动停止物理写入；OTA/锁/退避拒绝不消耗预算。
成功清连续失败数，MCU reset 重建预算，频繁 reset 下的寿命仍须单独评估。

State/Event 的 60 s checkpoint / 5 s 失败退避和平台的全 Flash 5 s 故障退避作用不同，继续分别维护。
业务布尔失败返回、诊断原因和记录层物理失败预算保留，未引入新管理层或结果分发框架。
三个 `storage_record_store_t` 均独立四字节对齐，避免 TC32 `-fpack-struct` 下端口指针误读。
SDK `flash_read_page` 没有错误返回；当前端口不能识别所有物理读故障，CRC/host 不能代替硬件故障诊断。

## 协议、OTA、事件和 SOC

外部参数协议仍为 v2，`0x2E05=2` 保留，与内部 schema 4 独立。
35-word AFE 事务、50-word SOC profile、五 word 休眠、地址、缩放、字序与授权均保留。
旧修订编号只读槽有现有客户端，保留但不参与 OTA 决策。
同格式普通 OTA 按 BIN 的选择和一次性标记更新；SOC 组恢复完整曲线和算法默认，未选中保留定制曲线。
恢复出厂更新组与所有普通组互斥，清空可见事件历史及全部个性化参数。
BUSINESS 保留均衡永久许可、校准与身份；休眠不随 BUSINESS/SOC 更新。
类型切换只提交一条完整 Config；之后 AFE 硬件应用、读回、失败 inhibit 及新样本资格仍必须完成。

Event 保持 100 条、原 ID、最新在前和 `0xC008..0xC06B` 窗口，ID 高字节、时间码低字节。
相邻同 ID 在首条之后 60 s 内合并，不累计隐藏重复次数，不因此产生 dirty。
Startup/Sleep 不合并，重启不合并上一启动；正常均衡开启不记持久历史，旧 ID 3 保留。
时间按真实 32K 差累计并处理小数秒/回绕，相邻更新时间须短于约 37 小时。
Sleep 是进入低功耗的尝试，不证明硬件已睡；同次失败重试不重复追加。
采样约一秒，可能漏掉更短瞬态；突然 reset 会丢失未提交 RAM 窗口。详细运行日志仍仅存 RAM。

State 保存 SOC 0..100、放电累计 0..100、cycle 0..65535。
积分余量、显示跟随、OCV 静置与 ETA 重新建立；未持久化每笔库仑积分或当前保护 latch。
突然断电可能丢失约 60 s 检查点窗口及不足整数百分比的积分余量。State/Event 历史不替代当前保护恢复资格。

## 低功耗与验收边界

普通 Suspend 保留 RAM，不为每次 Suspend 擦写。D008 ACC 深睡保持 PC4 高、ACC 低唤醒；
显式/保护性断电最后拉低 PC4。SH 强制请求提交后进入 MCU DeepSleep。
这些强制路径均在非 OTA 且 Flash 可写时尽力保存 State/Event 一次，保存/AFE 失败不取消电源动作。
普通 Suspend、UART/BLE 门禁、真实唤醒和 watchdog 策略保持。

源码、host、ASan/UBSan、TC32 ELF/MAP 和实板证据分别留存。详细结果见本轮精简记录。
物理 Flash 掉电、低电压写入、复位风暴、最大擦写时间/BLE/watchdog、Gate/Vgs、
低功耗电流/唤醒、真实 OTA 与动态栈水位仍需实板验收；本轮没有生成 BIN、烧录或执行 OTA。
