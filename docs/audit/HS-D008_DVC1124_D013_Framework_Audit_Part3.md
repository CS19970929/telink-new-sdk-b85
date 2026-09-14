# 14. Balance / Open Wire

## Balance

0x67..0x69 约 60s 自动清零，当前报告实际 AFE mask 是正确的。

仍缺完整策略：

```text
BMS requested mask
 -> 检查充电状态/电压差/温度/采样有效
 -> 下发
 -> <60s 周期再次评估
 -> 仍满足才续期
```

禁止为了“保持均衡”无条件高频反复写寄存器。

## Open Wire

当前 `StartOpenWireCheck()` 只正确置 COW。完整功能应是状态机：

```text
IDLE
 -> TRIGGER COW
 -> WAIT ~1s
 -> read cells
 -> evaluate criteria
 -> report diagnostic
 -> restore normal state
```

所以 API 能触发 COW ≠ Open-Wire 功能完成。

---

# 15. Sleep / Wake / I2C Watchdog

## 15.1 DVC Sleep

```text
CST = 0xE
```

正确。`0xF` 是 Shutdown，不可再沿用厂家旧 Demo 的错误命名。

## 15.2 Current Wake

当前应在“关闭”和“启用”之间形成完整语义，不允许 CAES/CWT 半开状态。

## 15.3 I2C WDT

当前 WDT=OFF 不是寄存器错误，但属于未完成的 safety decision。

若启用，需要完整测试：

```text
停止有效I2C
 -> 到 WDT timeout
 -> 记录 IWTS / IWTF
 -> 观察 CHG/DSG 实际栅极
 -> 恢复第一笔通信
 -> 软件 inhibit 仍保持
 -> 重新配置/验证
 -> 连续有效样本
 -> 受控恢复输出
```

特别要防止“第一笔 I2C 恢复即 IWTS 消失，硬件自己重新允许输出，而软件还没检查完故障”的窗口。

---

# 16. Flash / 配置事务

当前 DVC 配置 store 的方向正确：

```text
requested BMS protection -> cold KV
dvc-only operating config -> DVC KV
SOC -> SOC KV
runtime -> runtime area
event -> event log
```

单字段配置：

```text
validate
 -> apply AFE
 -> readback
 -> persist
 -> success
```

persist 失败要 rollback。

仍必须补一个状态：

```text
CONFIG_INCONSISTENT
```

用于：

```text
AFE apply success
 -> persist fail
 -> rollback fail
```

此时禁止继续假装正常；应 inhibit 输出、记录事件、重置并完整恢复 AFE 配置。

---

# 17. BLE / UART / Raw Register

D008 已经形成正确方向：BLE SPP 与 UART 共用 Modbus/semantic configuration service。

推荐继续保持：

```text
0x28xx semantic config/diagnostics
0x284x requested protection
0x285x effective protection
0x29xx factory raw mirror
```

Raw register：

- 只在 Factory 模式开放写；
- RC 寄存器不允许普通 raw read；
- W0C/self-clear/runtime control 使用专用 API；
- raw write 也必须进入 semantic candidate + validation + apply/persist，不可绕开安全模型。

---

# 18. SOC：按 D013 抽离 Profile

D013 引入 `bms_soc_defs.h` / `bms_soc_profile.h` 的做法应迁移到 D008。

D008 至少要有：

```text
D008_24S_LFP_SOC_PROFILE
D008_20S_NMC_SOC_PROFILE
```

算法只处理：

```text
real SOC = coulomb + endpoint + OCV correction
display SOC = smoothed(real SOC)
```

化学体系数据放 profile：

```text
OCV table
full endpoint
empty endpoint
sag thresholds
rest qualification
```

不能让 20S NMC 继续使用当前 24S LFP 的 3.75V COV / LFP OCV 逻辑。

---

# 19. D008 建议默认板级配置（不含未签核产品阈值）

| 配置 | 建议 |
|---|---|
| Address mode | FIXED |
| I2C transfer address | 0x40 W / 0x41 R |
| Cell count | 由 `D008_24S_LFP` / `D008_20S_NMC` profile 决定 |
| Shunt | 200uΩ |
| High-side FET mask | **1** |
| CADC work | 1 |
| Current wake engine | 0（直到 CWT 有已验证非零值） |
| CC1 work/sleep | 4ms / 32ms，保持当前基线 |
| Charge pump | 10V，保持当前基线并实测功耗 |
| Cell signed mode | 0 |
| VADC | enable=1, sync=1, period=1 CC2, time=1.54ms |
| GP1 | NTC（板级确认） |
| GP2/GP3 | profile/BOM 决定；无外部 NTC则 OFF |
| GP4 | NTC |
| GP5 | low-side CHG |
| GP6 | low-side DSG |
| V3P3 work/sleep | 暂保持1/1，实测低功耗和依赖 |
| I2C WDT | 默认0直到 safety 签核；最终量产值需明确 |
| Timed wake | off |
| Interrupt mask | 不用INT时建议0xFF |
| Core OT | 0，直到热测试签核 |
| SCD | 0，直到短路测试签核；**发布阻断** |
| Body diode | 0，先使用软件 FET FSM |

---

# 20. 代码迁移文件清单

## P0：先做安全和产品身份

1. `bms_afe_backend.h`
   - 采用 D013 compile-time backend 模式；
   - D008 默认 `BMS_AFE_BACKEND_DVC1124`。
2. `conf.h`
   - 删除 D3PRO 作为 D008 入口；
   - 引入真正 D008 product profile；
   - 保留 D008 GPIO，不复制 D011 GPIO。
3. `dvc1124_project_config.h`
   - HSFM 默认改1；
   - CAES/CWT 语义一致；
   - GP2/3 profile 化；
   - INT mask 明确；
   - 0x53/0x54 policy 显式化。
4. `dvc1124_bms.c`
   - 移植 D013 output inhibit / valid streak / FET arbitration；
   - 增加通信失败/恢复 FSM；
   - SCD/OC fault latch 与受控恢复。
5. `dvc1124.c`
   - 保留已验证 I2C/CRC/换算核心；
   - 逐步移除 `g_tParam/g_stCellInfoReport` 业务依赖。
6. `app.c`
   - 只迁移 D013 通用 PM/AFE 调用改进；
   - D008 wake source 继续使用 D008 的 SW/CHG-IN；禁止复制 D011 PB1/PC0/PC1/PD3 等语义。

## P1：框架一致

- `bms_soc_defs.h` / `bms_soc_profile.h`；
- D008 LFP/NMC profile；
- D008 integration tests；
- AFE config atomic multi-write；
- Open-Wire state machine；
- Balance refresh policy；
- CONFIG_INCONSISTENT fault。

## P2：纯化和维护性

- `dvc1124.c` 完全去 BMS global；
- `dvc1124_bms.c` 负责唯一映射；
- app.c 进一步拆分 power/fet/scheduler；
- legacy protocol units 只在 transport boundary 转换。

---
