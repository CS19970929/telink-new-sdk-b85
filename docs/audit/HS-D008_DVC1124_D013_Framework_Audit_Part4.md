# 21. 建议的自动化测试

## 21.1 Register Golden Vectors

```text
CST: reset=0x0D, sleep=0x0E, shutdown=0x0F
0x6D: CPVS=101 -> 0x28; COW=0x04; CMM=0x02; CVS=0x01
OC2: enable=0x40; code0=4mV; code63=256mV
SCD: enable=0x40; code1=10mV; code63=630mV
200uΩ: CC2 LSB=1.5625mA; OC1 code8=10A; OC2 minimum=20A; SCD code4=200A
COV 3750mV -> code3250
CUV 3000mV -> code3000
CUV delay request10s -> effective8s
OC1 delay100ms -> 96ms
OC2 delay100ms -> 100ms
```

## 21.2 Framework Contract

至少自动检查：

- app/BMS 不能 include `dvc1124_reg.h`；
- DVC register truth 只来自 `dvc1124_reg.h`；
- GP2/3 profile 未定义时不能默认为已装外部 NTC；
- D008 backend 默认必须是 DVC；
- high-side mask 必须为1；
- CWT=0 时 CAES 默认0；
- 24S/20S profile 不允许只差 cell_count；
- requested/effective 保护值都可读；
- RC register raw read 被拒绝；
- Factory raw write 有门禁；
- Flash 区域不重叠；
- 失联进入 inhibit，未达到连续有效帧不能恢复输出。

---

# 22. 实板验证矩阵

| 测试组 | 必测项 | 通过标准 |
|---|---|---|
| I2C | 正常、NACK、CRC错、SDA/SCL异常、AFE掉电、PD7 power-cycle | 有界退出；fault可见；MOS策略符合设计；可受控恢复 |
| Cell | Cell1/12/24，低/中/高电压，20S mask | 精度、位置、max/min、mask 正确 |
| Current | 0A、±1/5/10/20A及大电流 | 极性、增益、零偏、SOC方向正确 |
| NTC | 冷/热、开路、短路 | TEMP_BREAK、保护、恢复均正确 |
| COV/CUV | trip/recover/delay | requested/code/effective/实测一致 |
| OC1/OC2 | 充/放两个方向 | 量化、栅极、alarm、恢复正确 |
| SCD | 多档短路能量 | 阈值/延时/MOS/保险丝协同且不反复重开 |
| FET | OFF/OFF, ON/OFF, OFF/ON, ON/ON | 命令、0x51、CC2 flags、实际 Gate 一致 |
| WDT | 停I2C、恢复I2C | IWTS/IWTF、FET、软件 inhibit 与恢复无竞态 |
| Balance | 单节/相邻/60s | 自动清零、续期、温升、采样互扰可控 |
| Open Wire | 各通道故障注入 | 完整状态机检测，不把 trigger 当完成 |
| Sleep | 广播/连接/空闲/deep/低压 | 电流、唤醒源、AFE配置、SOC连续 |
| Flash | 写/擦/轮换时掉电 | journal 可恢复，不污染其它区域 |
| OTA | 10/50/99%中断、CRC失败 | A/B可恢复，不擦 0x74000..0x7FFFF |

---

# 23. 发布阻断项

在以下项目全部关闭前，不应把 D008 标记为 production-ready：

1. `D008_24S_LFP` 与 `D008_20S_NMC` 产品 profile 定稿；
2. 真实 DVC 型号/BOM确认（-22/-24，虽 fixed mode 当前不受影响）；
3. high-side output mask 改为符合 D008 低边拓扑；
4. AFE comm failure output inhibit / reinit / release 验证；
5. SCD 阈值和延时实板签核；
6. I2C WDT 是否启用及 CHG/DSG timeout policy 签核；
7. 0x53/0x54 MOS mask policy 显式化；
8. GP2/GP3 BOM/功能确认；
9. NTC R-T 曲线确认；
10. OC1/OC2/COV/CUV requested/effective + 实测矩阵；
11. Short/OC fault recovery 不会形成 OFF-ON-OFF 循环；
12. Flash rollback-fail 的 config-inconsistent 状态；
13. Balance/Open-Wire 完整状态机；
14. TC32 clean build + MAP/BIN + host tests + 实板记录绑定到具体 commit。

---

# 24. 推荐实施顺序

```text
Phase A  产品身份清理
  -> D008 profile / backend / board config

Phase B  寄存器产品配置修正
  -> HSFM / CAES-CWT / GP2-3 / INT mask / 0x53-54 policy

Phase C  D013 safety framework 移植
  -> output inhibit / reinit / valid streak / FET arbiter / short latch

Phase D  SOC/profile
  -> 24S LFP + 20S NMC

Phase E  OpenWire / Balance / atomic config

Phase F  TC32 CI + 实板验证 + release evidence
```

不要在 Phase A-C 之前大规模重写已经正确的 DVC I2C/CRC/measurement core。

---

# 25. 最终目标架构

```text
                       BMS Application
                             |
        +--------------------+--------------------+
        |                    |                    |
     Scheduler           Protocol             Diagnostics
        |                    |                    |
        +--------------------+--------------------+
                             |
                         BMS Core
              Protection / FET / Power / SOC
                             |
                             v
                         bms_afe.h
                             |
                    compile-time backend
                  /                          \
          DVC1124 backend              SH3673510 backend
                  |                          |
              DVC driver                  SH driver
                  |
             Telink I2C
```

D008 产品固件只激活 DVC backend；保留多 AFE 框架的价值是让未来项目共享 BMS core，而不是在运行时动态选择 AFE。

---

# 26. 最终审核结论

### DVC 寄存器“写法”层面

目前关键寄存器真值整体已经正确，尤其：

```text
0x5E/0x5F OC2 bit6 enable
0x62 SCD code*10mV
0x6D CPVS/COW/CMM/CVS 位图
0x01 0xD/0xE/0xF reset/sleep/shutdown
0x76 RC safe access
0x90 address upper bound
CRC8 / fixed address / common-mode correction
```

不应该再为了风格重写这些已经建立证据链的核心。

### D008 产品配置层面

真正需要修的是：

```text
HSFM = 1
CAES/CWT 一致
GP2/GP3 profile化
0x79 mask语义修正
0x53/0x54 显式 policy
SCD/WDT 产品签核
24S LFP / 20S NMC 分 profile
历史 D3PRO 参数彻底退出 D008 产品身份
```

### 框架层面

D013 目前最值得 D008 复制的不是 SH3673510 driver，而是：

```text
compile-time AFE backend
board/project config separation
state/error ownership
AFE fail-safe inhibit
valid-snapshot release
reinit/cooldown
FET arbitration
SOC profile separation
hardware reference + development status + contract tests
```

完成这些后，D008 才真正与 D013 达到“相同框架、不同 AFE/不同硬件 Profile”的状态。

---

# 附录 A：建议新增/调整的关键宏

```c
/* D008 board policy */
#define DVC1124_DEFAULT_HIGH_SIDE_FET_MASK          1u
#define DVC1124_DEFAULT_CADC_WORK_ENABLE            1u
#define DVC1124_DEFAULT_CURRENT_WAKE_ENGINE_ENABLE  0u
#define DVC1124_CURRENT_WAKE_THRESHOLD_UV           0u
#define DVC1124_BODY_DIODE_THRESHOLD_UV             0u
#define DVC1124_DEFAULT_INTERRUPT_MASK               0xFFu

/* Product-safety values remain disabled until signed off. */
#define DVC1124_HW_SCD_THRESHOLD_MV                  0u
#define DVC1124_HW_SCD_DELAY_US                      0u
#define DVC1124_I2C_WATCHDOG_SECONDS                 0u
```

上面 SCD/WDT 的 0 是“未完成验证时的安全开发默认”，不是最终量产结论。

---

# 附录 B：禁止回归规则

```text
1. 不允许把 OC2 enable 写回 0x80。
2. 不允许整字节覆盖 0x5F bit7。
3. 不允许把 0x6D 恢复成旧 CPVS_MASK=0xE0 / COW=0x10。
4. 不允许把 0x0F 当 Sleep；它是 Shutdown。
5. 不允许保护寄存器写完不 readback。
6. 不允许 requested 15A -> hardware 20A -> 上位机仍谎报15A effective。
7. 不允许用 requested balance mask 永久代表 actual AFE mask。
8. 不允许 NTC invalid 时跳过温度保护。
9. 不允许 AFE 通信恢复第一帧就自动重新开 MOS。
10. 不允许 20S NMC 只改串数而复用 24S LFP profile。
11. 不允许复制 D013/D011 的 GPIO/SH 寄存器到 D008。
12. 不允许用历史 D3PRO 参数当 D008 产品签核参数。
```

---

# 附录 C：资料索引

## GitHub

- `refactor/bms-template-phase1` @ `dc230c36291956e69bf7c20f641a121c55d7b105`
- `feature/dvc1124-22-bms` @ `d32432a344a59ca8cebd07655979722092b76c71`
- `feature/sh3673510-d013-bms` @ `4e8193735d046645177e4ba0ec90c22b19693e0e`

## 用户资料 / AFE 资料

- `HS-D008-24S100A-V1(2).pdf`
- `DVC1124-2参考手册_V1.2.pdf`
- `DVC1124-2数据手册_V1.1.pdf` / `S05_DVC1124-2_数据手册_V1.1.pdf`

## 关键证据页

- V1.2 p16-18：FET / mask / CADC / OC1/OC2；
- V1.2 p19-22：SCD / wake / balance / CP；
- V1.2 p23-27：VADC / COV/CUV / GP / OT / WDT / timed wake / INT mask；
- V1.2 p30-31：CHG/DSG 驱动逻辑；
- V1.2 p32：Cell common-mode 二次校准；
- V1.2 p33：版本修订（CPVS/VAE reset 值）；
- 数据手册 V1.1 p23-24：I2C 100kHz、无 clock stretching、固定地址/级联地址、CRC8、64ms bus timeout。
