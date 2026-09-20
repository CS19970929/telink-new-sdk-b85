# SOC 工程架构

## 1. 范围与唯一实现

本轮将 D008、D011、D013、D014 统一到同一份无硬件寄存器依赖的 `SocEnhance.c/.h` 算法核心。四个产品继续保留各自 AFE、采样、保护、存储和 IO 适配；这里的“统一”不表示把 DVC1124 与 SH3673510 驱动合并，也不改变协议地址、Flash 分区、MOS 或保护行为。

核心入口为 `bms_soc_process_sample(const bms_soc_sample_t *)`。原有 `APP_SOC_IntEnhance_Ctrl()` 保留为硬件适配层，因此业务调用和既有公共 API 不需要另建第二套 framework。

```text
DVC1124 / SH3673510 sample + product features
                    |
                    v
            bms_soc_sample_t
                    |
                    v
     sample validity / elapsed / direction
          |          |          |
          v          v          v
       Coulomb    OCV band    Endpoint
          \          |          /
           \         v         /
            SOC estimate + capacity
                       |
              display soft landing
                       |
           diagnostics / State checkpoint
```

## 2. 标准输入与单位

`bms_soc_sample_t` 明确携带：

- `timestamp_32k`：无符号 32 位 32 kHz 工程时基；按无符号差处理回绕；
- `current_ma`：负值充电、正值放电；`abs(current_ma) <= 200 mA` 默认不积分；
- `pack_voltage_mv`、`cell_min_mv`、`cell_max_mv`、`cell_delta_mv`；
- `temperature_min_x10`、`temperature_max_x10`：协议编码为 `(degC + 40) * 10`；
- 样本、电压、温度有效性；均衡、加热、Open-Wire、AFE/温度/电流/总压故障；
- Cell OVP/UVP，以及 charger/load 的 known/present 状态。

无效样本、重复时标和大于 400 ms 的 gap 不补积分。恢复后的第一帧只重建时间基准，不能把未知时长当作静置或零电流。

## 3. 状态与所有权

- `SOC estimate`：内部算法值，供容量、告警和诊断使用；
- `SOC display`：面向用户的软着陆值，不能反向影响保护；
- `nominal_capacity`：产品额定容量；
- `effective_capacity`：当前算法使用的有效满容量；
- `remaining_capacity`：由 estimate 和 effective capacity 计算；
- `endpoint_state`：`NORMAL/FULL_APPROACH/CONFIRMED_FULL/EMPTY_APPROACH/CONFIRMED_EMPTY`；
- `OCV state`：`WAIT_CURRENT/PREPARE/READY/CORRECT_DOWN`；
- `learning_state`：`NONE/EMPTY_TO_FULL/FULL_TO_EMPTY`；默认关闭；
- `ETA state`：`INVALID/STABILIZING/VALID/LOW_CONFIDENCE`。

State schema 3 仅保存可证明的 estimate、cycle、accepted/candidate capacity 与学习元数据。display 过渡、OCV 静置资格、ETA 滤波、积分余数和未知掉电时长不持久化。

## 4. 算法策略

库仑积分是主线，OCV 是有限带宽纠偏：连续合格静置 10 min 后形成 OCV `[low, high]`；区间内不动，低于区间不向上修正，高于区间每 30 min 最多下降 1%。LFP 平台默认至少 ±7%，NMC 至少 ±5%。普通静置、开机高压与电压回弹永远不能把 SOC 上拉。

满电上拉只允许发生在已确认充电方向并满足 OVP 或满电电压/压差/稳定时间时；空电由可靠 UVP 或末端 knee 收敛。大电流 sag 设有 hold，安全 UVP 可以强锚定 0%，但低质量 endpoint 不能用于容量学习。

容量学习只有在显式开启后工作。完整周期产生 candidate；至少两个相互一致的独立 candidate 才接受，每组 accepted capacity 相对当前值最多改变 5%。均衡、加热、charger/load 状态变化、Open-Wire、温度/保护、AFE 故障、校准变化、反向或 gap 都有独立 reject reason。

TTE/TTF 基于 recent filtered current 和内部容量，方向稳定后才有效。CV/taper 或波动负载下降为低置信度/不可用，不输出伪精确值。

## 5. 本轮源码审核中修正的风险

- 原入口只在有效采样到来时推进，使丢样/gap 不能完整进入 SOC 状态机；现在有效与无效帧都提交标准样本。
- 样本状态原来分散读取多个全局量；现在算法先固定一份输入快照，核心不直接访问 AFE 寄存器。
- 方向切换判定使用更新后的电流会丢失 previous direction；现在 previous/current 的所有权和更新顺序明确。
- OCV 原资格缺少均衡、加热、温度有效性和 charger/load source change 门禁；现在这些事件清除静置资格。
- 容量学习原来无法区分均衡、加热、充电器变化和负载变化；Runtime v3 新增 15..18 reject reason。
- 电流校准读取被收敛到固定宽度 `int32_t/uint32_t` API；核心不依赖产品私有存储结构。

保留不变：通信寄存器、DVC/SH 驱动、保护阈值、MOS 策略、Flash 分区、额定容量 owner、`abs(current)<=200 mA` 死区和默认关闭学习策略。

## 6. 参数分层

| 层级 | 示例 | 变更要求 |
|---|---|---|
| 算法参数 | gap 400 ms、静置 600 s、OCV 纠偏 1800 s、显示速率、学习一致性/步长 | 必须做生产 C 数值回归；不能由 UI 随意改写 |
| 测量参数 | current offset/gain、采样有效性、温度/电压量化 | 由产品采样和校准 owner 提供；变化会中断学习 |
| 产品参数 | chemistry/profile、串数、额定容量、full/empty 窗口 | 明确产品 profile；不得由另一产品硬件假设推导 |

## 7. 产品边界

- D008：TLSR8251 + DVC1124；标准输入来自 `bms_afe_get_feature_snapshot()` 和 D008 protection/feature 状态。
- D011/D013/D014：TLSR8251 + SH3673510；共享纯 SOC 核心，但继续使用各自 SH 采样、IO 和产品宏。
- D014 已同步相同 core、State schema 3、Runtime Diagnostics v3 和 Config schema 3；没有复制 D008 的 DVC 寄存器或低边 FET 逻辑。

主机测试、PC 仿真和 TC32 build 只能证明软件路径；电流符号、ADC/AFE 精度、时钟误差、真实 OCV 曲线、Gate 行为、热行为与保护时序仍必须实板确认。
