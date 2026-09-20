# D011 上位机诊断协议适配

## 范围

D011 固件通过现有 BLE/串口 Modbus RTU 通道提供只读 Diagnostics schema 1，供统一的
`BmsTool.Windows`、`BmsFactoryTest.Windows`、Android App 和 `BmsTool.Cli` 使用。上位机唯一真源仍是
`feature/windows-afe-hw-protection-editor-v2` 分支的 `bms-tool-windows/`；本分支只实现固件侧数据源。

## 地址与能力

| 地址 | 长度 | 内容 |
|---|---:|---|
| `0x2A00` | 256 words | 启动、存储、FET、运行态、SOC、保护快照 |
| `0x2B00` | 768 words | 64 条 RAM Trace，每条 12 words |
| `0x2E00` | 结束边界 | 不属于诊断窗口；保留现有产品参数区 |

头部固定为 magic `0x4447`、schema `1`。D011 声明 Boot、Trace、Storage、MOS 和 Runtime 能力，
AFE/MCU 标识分别为 `0x3510` / `0x8251`，Runtime 版本为 `2`。窗口只读；`0x06`、`0x10`
写入只要与该范围重叠就返回 Modbus illegal address。

## 数据语义

- Current：SH3673510 已换算业务电流、采样有效位和 32 kHz 时间戳。当前没有第二路独立 raw current，
  因而 raw/current 两项相同，客户端必须按 AFE 型号说明这一点。
- SOC：发布现有 SOC 算法确实维护的 chemistry/profile、estimate/display、OCV band/confidence、
  静置时间、学习状态、nominal/effective/remaining capacity 和 SOH。当前算法没有 ETA 与 v3 最近决策记录，
  因而 ETA 明确为 unavailable，Runtime 不冒充 v3。
- FET：区分 Requested、最近成功写入的 SH FET Command，以及 `BSTATUS1` 的 AFE FET status。
  这些都不是 Gate/Vgs 物理反馈。
- Protection：直接发布 First/Second/Third 三级软件/AFE合并报告位。
- Storage：记录 CONFIG、STATE、EVENT 的区域、启动尝试、默认值使用及最近结果；FACTORY 当前没有独立 owner，
  保持 `NOT_RUN`。
- Trace：记录启动、存储初始化、FET变化、采样有效性和保护变化。断电即丢失，不写 Flash。
- Build ID：构建脚本注入 Git HEAD 前 32 bit 和 dirty 标志。dirty 固件不能只靠 Build ID 唯一复现。

## 推荐命令

```powershell
bms-cli info --mac <MAC> --json
bms-cli health --mac <MAC> --output .\D011_health.zip --json
bms-cli diag --mac <MAC> --quick --json
bms-cli diag --mac <MAC> --output .\D011_diag.zip --json
bms-cli soc --mac <MAC> --json
bms-cli test connection --mac <MAC> --count 20 --json
bms-cli test soc --mac <MAC> --count 10 --output .\D011_soc_test.json --json
bms-cli test diag --mac <MAC> --count 3 --full --output .\D011_diag_test.json --json
```

完整诊断还会独立读取软件保护 `0x2100..0x2140`、AFE Requested `0x2500`、Meta `0x2523`
和 Effective `0x2540`，不依赖 D008 `0x2E00` 参数能力窗口。

## 验证边界

Host contract、TC32 编译、BIN 检查和 MAP 只能证明协议与构建闭环，不能证明实板 SPI、保护动作、FET 导通、
睡眠唤醒或 BLE 稳定性。实板至少需要执行上面的 connection/SOC/diag 多轮测试，并结合实际触发条件验证保护恢复。
