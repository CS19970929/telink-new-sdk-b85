# SOC 模块审查与优化建议

## 文档目的

本文档用于整理当前项目 `vendor/ble_sample` 中 SOC 模块的现状、问题、准确性风险与可落地优化方案。

审查范围：

- `/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c`
- `/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.h`
- `/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/soc_kv_store.c`
- `/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c`

结论先行：

- 当前 SOC 模块能工作，但本质上是“简化库仑积分 + 电压端点硬钳位”。
- 模块没有形成完整的“状态机 + 可信输入 + 持久化 + 校准闭环”。
- 现有实现存在若干逻辑缺陷，会影响 SOC 准确性、一致性和后期维护。
- 建议把 SOC 模块重构为独立的 `soc_core`，采用“库仑积分为主、OCV 为辅、端点校准保底”的架构。

---

## 一、当前实现概览

### 1. 现有算法结构

当前实现主要由以下几部分构成：

1. 初始恢复
   - 从 `soc_kv_store` 恢复 `soc / dsg / cycle`
   - 入口位于 `soc_param_lib_init()`

2. 库仑积分
   - 充电路径：`SOC_Cont_AH_Int_CHG()`
   - 放电路径：`SOC_Cont_AH_Int_DSG()`
   - 通过 `u16Ichg` / `u16IDischg` 每秒累加或扣减容量

3. 端点修正
   - `CorrectionTerminal_CV()`
   - `soc_cali()`
   - 主要依据单体最高/最低电压在满电/空电附近做 SOC 拉升或拉低

4. SOH 简化估计
   - `bms_soh_from_cycle()`
   - 通过 cycle 次数做分段线性退化

5. 结果输出
   - `SOC_Result_Pass()`
   - 将内部状态同步到 `g_stCellInfoReport`

### 2. 当前设计优点

- 结构简单，容易跑起来。
- 没有引入复杂矩阵算法，对 MCU 资源友好。
- 已有基本的 Flash 持久化机制。
- 已经预留了 OCV 修正思路和循环寿命衰减思路。

### 3. 当前设计的根本短板

- 采样、积分、校准、持久化没有严格解耦。
- 多处单位和语义不统一。
- OCV 路径没有形成真正可用的闭环。
- 输入可信度缺少分级，异常测量会直接进入 SOC 计算。

---

## 二、SOC 相关问题审查

## 2.1 高优先级问题

### 问题 1：OCV 计算函数实际上失效

位置：

- [SocEnhance.c#L229](/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c#L229)

现象：

- `Get_OpenCircuit_Value_new()` 主体被 `#if 0` 屏蔽。
- 函数最终没有有效 `return`。
- 后续 `get_soc_from_openVol_onlyDec_new()` 仍然调用该函数。

影响：

- OCV 修正链路实际上不可用，或者行为未定义。
- 如果后续有人重新启用 `SOC_OCV_Fix2()`，将直接把未定义值引入 SOC 修正。
- 这意味着当前 SOC 不能依赖 OCV 校准，只能依赖积分和端点硬修正。

结论：

- 这是一个明确 bug。
- 需要立刻修复，即使当前 OCV 流程暂未启用，也应该避免保留未定义行为函数。

### 问题 2：`soc_param_lib_init()` 初始化顺序错误，SOH 与容量恢复存在时序缺陷

位置：

- [SocEnhance.c#L213](/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c#L213)
- [SocEnhance.c#L150](/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c#L150)

现象：

- `soc_param_lib_init()` 先调用 `set_calsoc(_soc->soc)`。
- `set_calsoc()` 内部会立刻使用 `SOC_Calculate_Element.u32Cycle_times` 计算 `soh`。
- 但此时 `u32Cycle_times` 还没有从 `_soc->cycle` 恢复。
- 之后才赋值 `u32Cycle_times = _soc->cycle`，再重新算一遍 SOH。

影响：

- 启动初始化过程出现短暂的错误中间态。
- 当前因为随后又重算了一次，最终结果大概率能被纠正，但设计上不严谨。
- 后续如果在 `set_calsoc()` 内增加更多依赖 cycle 的逻辑，会直接放大问题。

结论：

- 这是初始化时序设计缺陷。
- 应改成“先恢复所有输入状态，再统一计算派生量”。

### 问题 3：SOC 库仑积分依赖 `u16Ichg/u16IDischg`，精度和单位链路不清晰

位置：

- [SocEnhance.c#L446](/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c#L446)
- [SocEnhance.c#L495](/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c#L495)

现象：

- 每秒积分一次，直接使用 `g_stCellInfoReport.u16Ichg` 或 `u16IDischg`。
- 该值来自 AFE 电流换算后的整型输出，并且已经经过阈值截断。
- 低于 2 的电流直接清零。

影响：

- 小电流段的累计误差较大。
- 休眠前后、轻载待机、涓流充电、微弱自放电都难以准确累计。
- 如果电流采样有偏置，误差会长期积分。

结论：

- 当前不是“真正的高精度库仑计数”，更接近“低分辨率容量粗积分”。
- 这会导致 SOC 长期漂移。

### 问题 4：充放电状态切换状态机过于脆弱

位置：

- [SocEnhance.c#L525](/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c#L525)

现象：

- `SOC_State_Transfer()` 仅通过 `u16Ichg >= 2` 或 `u16IDischg >= 2` 连续 3 次判断状态切换。
- 没有单独的静置确认时间。
- 没有对方向反转、脉冲电流、继电器切换、启动浪涌做抗抖。

影响：

- 充放电边界附近可能来回切状态。
- 会导致积分节奏不稳定。
- 校准策略也会频繁被中断。

结论：

- 当前状态机过于简化，建议引入 `REST / CHARGE / DISCHARGE / UNKNOWN` 四态模型，并加入进入/退出滞回。

### 问题 5：端点校准是硬钳位，缺乏上下文条件，容易误修正

位置：

- [SocEnhance.c#L822](/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c#L822)

现象：

- 充电时只要 `VCellMax >= 4180` 且 `VCellMin >= 4000` 就直接把 SOC 拉到 100%。
- 放电时只要 `VCellMin <= 3100` 持续一段时间就直接拉到 0%。
- 没有要求静置、没有要求小电流 CV 尾充、没有要求温度有效区间。

影响：

- 高内阻、瞬时极化、负载跳变、温度偏低时可能误判满电或空电。
- 一旦误钳位，SOC 会被强行重置，后续需要很长时间才能重新修正。

结论：

- 端点校准可以保留，但必须增加进入条件。

## 2.2 中优先级问题

### 问题 6：Cycle 统计逻辑过于粗糙

位置：

- [SocEnhance.c#L512](/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c#L512)

现象：

- 放电累计 `u8DSG_SOC_Int` 达到 80 就 `cycle +1`。
- 该值不是基于真实“等效全循环容量 EFC”，而是基于整数百分比变化估算。

影响：

- Cycle 数会随 SOC 估算误差联动漂移。
- SOH 由 cycle 推导，因此 SOH 也会被放大误差。

建议：

- 改为按“累计放电 Ah / 额定 Ah”统计 EFC。
- `cycle_times` 应基于总 throughput，而不是基于 SOC 百分比变化。

### 问题 7：SOC 持久化粒度过粗，且写入策略与算法状态不一致

位置：

- [soc_kv_store.c#L244](/Users/cs/Downloads/work/todo/tc_ble_single_sdk-V3.4.2.8_Patch_0001%20(1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/soc_kv_store.c#L244)

现象：

- 只保存 `soc / dsg / cycle`。
- 没有保存 `u32CapNow / u32CapFull / offset / last calibration confidence` 等关键内部状态。
- 每当 `soc` 有 1% 变化就写 Flash。

影响：

- 断电恢复只能恢复离散百分比，恢复精度有限。
- 内部积分状态会丢失。
- SOC 变化频繁时会产生较高写入频率。

建议：

- 保存“核心状态快照”，而不是只保存几个离散输出量。
- 写入触发从“每 1%”改为“静置窗口、关机前、明显变化、周期性保底”。

### 问题 8：存在未使用/半使用代码路径，增加维护风险

现象：

- `SOC_OCV_Fix2()` 相关逻辑保留，但 `APP_SOC_IntEnhance_Ctrl()` 并未调用。
- `SOC_EEPROM_Deal_Monitor()` 被注释。
- `Inc_real_soc()`/`Dec_real_soc()` 与主积分路径并存。
- 大量 `#if 0` 和历史代码残留。

影响：

- 后续维护者很难判断“哪些是正在使用的真实逻辑”。
- 容易在修改时误触死代码。

结论：

- SOC 模块可维护性偏差，建议清理死代码并拆分模块。

---

## 三、为什么当前 SOC 准确性上限不高

### 1. 没有真实零点校准

当前系统没有看到对电流零漂进行系统性标定与补偿。

结果：

- 小电流长期积分会漂。
- 待机状态下可能缓慢爬升或下降。

### 2. 没有温度补偿

当前 SOC 主要按固定容量和固定电压阈值处理，没有把温度引入容量修正或 OCV 曲线修正。

结果：

- 低温下会高估剩余容量或误判空电。
- 高温下曲线偏移也无法反映。

### 3. 没有老化闭环

当前 SOH 仅由 cycle 次数估算，且 cycle 本身又不精确。

结果：

- 容量衰减估计不可信。
- 实际满充容量变化无法反映到 `u32CapFull`。

### 4. OCV 校准链路没有真正启用

当前没有可工作的“静置足够久 -> 采信 OCV -> 平滑修正 SOC”闭环。

结果：

- 库仑积分一旦漂移，只能靠硬端点拉回。

---

## 四、建议的优化方向

## 4.1 总体架构建议

建议将 SOC 模块拆为 4 层：

1. `soc_input`
   - 负责输入采样整形
   - 输出统一的 `voltage/current/temp/state/time` 数据

2. `soc_core`
   - 负责 SOC 主状态计算
   - 包含积分、状态机、端点校准、OCV 校准、SOH 更新

3. `soc_store`
   - 负责状态持久化
   - 只处理快照读写，不参与算法

4. `soc_diag`
   - 负责输出置信度、校准原因、误差标志、调试信息

目标：

- 高内聚：每层只做一件事。
- 低耦合：SOC 算法不依赖外部全局结构细节。
- 易维护：算法与硬件采样解耦。

## 4.2 算法主线建议

主线建议采用：

- 主估计：库仑积分
- 辅修正：OCV 静置校准
- 保底修正：满充/欠压端点校准

即：

`SOC = Coulomb_Counting + OCV_Trim + Endpoint_Clamp`

其中：

- 库仑积分负责短时连续性
- OCV 校准负责中长期漂移修正
- 端点校准负责极端情况下的安全收敛

---

## 五、建议加入的校准策略

## 5.1 电流零点校准

### 目标

修正 AFE 电流测量在零电流附近的 offset。

### 触发条件

- CHG MOS/DSG MOS 状态稳定
- `|current| < I_zero_window`
- 持续静置超过 `T_zero_cal`
- 温度变化不剧烈

### 策略

- 在静置窗口内对原始电流做均值滤波
- 更新 `current_offset`
- 积分时使用：
  `current_corrected = current_raw - current_offset`

### 推荐参数

- `I_zero_window = 0.2A ~ 0.5A`
- `T_zero_cal = 30s ~ 120s`

## 5.2 OCV 静置校准

### 目标

在真正静置后，用单体最低压或 pack OCV 对 SOC 进行小步修正。

### 前提

- 必须先修复当前 OCV 函数失效问题。
- 必须区分化学体系，不能把三元/LFP 混用。

### 触发条件

- `|I| < I_rest`
- 持续静置时间 `T_rest >= 30min`
- 电压变化速率小于阈值
- 不处于保护恢复、MOS 切换、充电插拔等扰动阶段

### 修正方式

- 不要直接覆盖 `soc_now`
- 改为：
  `soc_now = soc_now + alpha * (soc_ocv - soc_now)`
- `alpha` 建议 0.1 ~ 0.3

### 收益

- 能持续消除长期积分漂移
- 不会像硬跳变那样影响用户体验

## 5.3 满充校准

### 当前问题

当前只看电压阈值，条件过弱。

### 建议触发条件

- 进入 CV 段
- `VCellMax >= V_full`
- `I_chg <= I_taper`
- 持续时间 `T_cv_tail >= 10min`
- 所有单体压差小于阈值

### 校准动作

- `soc = 100%`
- `cap_now = cap_full`
- 记录本次满充输入容量，可用于更新 `cap_full_est`

## 5.4 空电校准

### 建议触发条件

- `VCellMin <= V_empty`
- 放电电流小于某一上限，避免大电流极化误判
- 持续时间满足阈值
- 温度在可信区间

### 校准动作

- `soc = 0%`
- `cap_now = 0`

## 5.5 SOH 容量校准

### 当前问题

SOH 仅由 cycle 次数线性推断，不够可靠。

### 建议

在“有效满充 -> 有效放空”或“有效放空 -> 有效满充”窗口中，统计实际可用容量：

- `cap_full_est = filtered(measured_throughput)`

再由：

- `soh = cap_full_est / cap_factory`

做平滑更新。

### 推荐原则

- 只在高置信度完整周期更新
- 使用一阶低通滤波
- 不允许单次大跳变

---

## 六、建议的简化重构方案

如果目标是“一方面简化，一方面提升准确性”，建议不要继续在当前 `SocEnhance.c` 上堆逻辑，而是做一次小重构。

## 6.1 推荐数据结构

```c
typedef struct {
    int32_t current_ma;
    uint16_t cell_vmin_mv;
    uint16_t cell_vmax_mv;
    uint16_t pack_temp_dC;
    uint32_t timestamp_s;
    uint8_t charge_present;
    uint8_t valid;
} soc_input_t;

typedef enum {
    SOC_STATE_REST = 0,
    SOC_STATE_CHARGE,
    SOC_STATE_DISCHARGE,
    SOC_STATE_UNKNOWN,
} soc_state_t;

typedef struct {
    uint8_t soc_pct;
    uint8_t soh_pct;
    uint32_t cap_nominal_as;
    uint32_t cap_full_est_as;
    uint32_t cap_remain_as;
    uint32_t throughput_dsg_as;
    uint32_t cycle_efc_x100;
    int32_t current_offset_ma;
    soc_state_t state;
    uint8_t confidence;
} soc_ctx_t;
```

## 6.2 推荐接口

```c
void soc_init(soc_ctx_t *ctx, const soc_store_snapshot_t *snap);
void soc_step_1s(soc_ctx_t *ctx, const soc_input_t *in);
void soc_apply_ocv_calibration(soc_ctx_t *ctx, uint8_t ocv_soc, uint8_t confidence);
void soc_apply_full_charge_calibration(soc_ctx_t *ctx);
void soc_apply_empty_calibration(soc_ctx_t *ctx);
void soc_export(const soc_ctx_t *ctx, struct stCell_Info *out);
```

优点：

- 每秒一步，时基清晰。
- 所有修正都围绕 `ctx` 进行，避免到处操作全局变量。
- 后续增加校准策略不会破坏接口。

---

## 七、建议分阶段实施

## 第一阶段：止血

目标：

- 先消除明确 bug 和未定义行为。

建议动作：

1. 修复 `Get_OpenCircuit_Value_new()`，若暂不使用则明确返回固定值并禁用调用链。
2. 统一 SOC 内部单位。
3. 重构初始化顺序，先恢复状态再计算派生值。
4. 把端点硬钳位增加最基本的时间与电流条件。
5. 持久化增加 `cap_now` 快照。

## 第二阶段：提准

目标：

- 建立可用的准确性闭环。

建议动作：

1. 增加电流零点校准。
2. 增加静置 OCV 平滑修正。
3. 用等效全循环替代当前 cycle 统计。
4. 引入 `confidence` 机制。

## 第三阶段：长期维护

目标：

- 让 SOC 成为独立可维护模块。

建议动作：

1. 拆分 `SocEnhance.c`
2. 增加单元测试样例
3. 增加离线回放验证工具
4. 用真实充放电日志做标定

---

## 八、建议的测试策略

## 8.1 功能测试

- 上电恢复 SOC
- 充电过程中 SOC 单调递增
- 放电过程中 SOC 单调递减
- 静置阶段 SOC 不跳变
- 满充校准触发正确
- 空电校准触发正确

## 8.2 异常测试

- 电流抖动
- AFE 电流瞬时异常
- 掉电重启
- Flash 快照损坏
- 低温大电流放电
- 高温 CV 尾充

## 8.3 精度测试

- 与标准库仑计对比
- 与充放电台架累计 Ah 对比
- 不同温度下误差对比
- 不同老化阶段误差对比

---

## 九、本次审查结论

### 事实

- 当前 SOC 模块可运行，但并不完整。
- OCV 路径存在明确失效代码。
- 库仑积分分辨率偏低。
- 端点校准条件不足。
- 持久化状态不完整。

### 判断

- 目前实现适合“先跑通、先显示”的阶段，不适合把 SOC 准确性作为强指标。
- 如果目标是长期稳定、可维护、准确性可持续提升，必须做结构性优化。

### 建议

- 不建议继续在当前文件里零散补丁。
- 建议采用“保留现有接口，对内重构”的方式迭代。
- 优先修复 bug，再上校准策略，最后拆模块。

---

## 十、和前一轮 BMS 审查的合并问题清单

### BMS 通信与保护问题

1. Modbus 写寄存器可直接切工厂模式或强制休眠，缺少鉴权。
2. 保护参数写入只改 RAM，不持久化，也不下发 AFE。
3. AFE I2C 读接口无真实错误返回，故障会被掩盖。
4. AFE CRC 失败后仅部分清空数据结构，可能产生混合旧数据。

### SOC 模块问题

1. OCV 函数失效，存在未定义行为。
2. 初始化恢复顺序不严谨。
3. 库仑积分输入精度不足。
4. 状态机过于简单。
5. 端点校准条件过弱。
6. Cycle 与 SOH 估计粗糙。
7. 持久化状态不完整。
8. 死代码较多，可维护性差。

如果下一步需要，我可以继续输出一版：

- `SOC 重构设计文档`
- `SOC 校准参数表`
- `SOC 模块接口头文件草案`
- `第一阶段修复补丁方案`
