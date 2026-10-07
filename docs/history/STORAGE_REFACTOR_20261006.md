> 历史记录：仅代表本文标注的日期和固定提交。当前实现见 [文档导航](../README.md)，当前验证以本轮报告为准。

# 四产品存储与 Event 优化实施记录

起点：`codex-bms-monorepo` / `3caed1d0ef8aee509c59beca0196087ef1a477b6`，工作区初始干净。用户授权全部实施并允许重划开发期业务 Flash、不迁移旧数据；要求外部协议保留，取消不再需要的事件清空命令。本次直接使用当前仓库，未另建工作树。

## 实施结果

1. 四产品删除 `bms_factory_mode.c/.h`、实际 sources 成员、Runtime 调度/休眠调用、State 老化分钟数及更新宏。保留 SN/校准等当前生产功能，敏感写继续要求 AFE 已授权会话。原 `0x2E10=6` 经授权后空操作，`0x2E87=1` 保留原位置。
2. 业务域收敛为 Config/State/Event，512 KiB 地址分别为 `0x40000`、`0x44000`、`0x4C000`，4/8/16 sectors；1/2 MiB 同样比例。APP A/B、OTA meta 与 SDK 保留区未搬动。内部 journal schema 3，State 44-byte payload，CFG2/事件协议格式保留。
3. 记录读取区分端口 I/O 失败；失败槽位后续利用并保留 RAM 游标；连续三次实际擦写失败后本次启动停止物理写入；门禁拒绝不计失败预算。Config/State 按编码比较，不使用 struct padding 判变化。缓存 getter 不隐式初始化或擦写。
4. Event 使用实际 tick 时间、保留合并窗跨 checkpoint、重复计数变化标 dirty、CBC 只记发生沿、语义校验 payload、休眠中止解除 latch。仍保留 100 条及原 ID/时间码/最新在前读取；无新增日志状态命令或物理 MOS 事件。
5. 删除 `0x1007` 固件处理及内部 Windows 工厂工具清空按钮；读取 `0xC008..0xC06B`、大端 event/time word 与 Windows 解码保留。
6. 删除老化计时后，SH 入睡前显式保存当前 SOC/循环/学习 State；保持已有保存失败仍允许低功耗的优先级。D008 保持保存成功后才主动深睡/断电。未修改保护、MCU reset、物理恢复或低功耗入口策略。

完整布局、失败预算、寿命估算、SOC 保存/恢复及 PM 条件见 [STORAGE](../STORAGE.md)，更新编号见 [OTA_PARAMETERS](../OTA_PARAMETERS.md)。

## 验证与复现

第二轮统一软件回归 `175 组、0 失败`。新增用例包含连续 120 次失败请求预算、擦除失败预算、门禁拒绝后恢复、部分尾槽重用、I/O 错误不写默认、checkpoint 前后事件合并、真实时间与回绕、CBC 恢复不误记、取消睡眠再尝试，以及 `0x1007` 单写/块写/广播非法地址应答。其余逐字节中断、更新组合、协议 corpus、SOC/AFE/保护/PM 回归保留。

四产品开发 TC32 ELF 链接零错误/零警告，资源占用均在 APP slot 与 TLSR8251 SRAM 范围内；D008/D011/D014 开发镜像投影的 Flash 余量低于 8 KiB，生产配置须另走严格门禁。提交后再验证最终固定源码、干净生产配置的四产品及 D008 三 profile，结果保存于树外证据目录。

Windows 单一维护分支 `feature/windows-afe-hw-protection-editor-v2` 提交 `3bb8c0319b54bdb9efc776727137fc6f86984a50`，事件脚本通过，内部 WPF Release 编译零错误/零警告；未安装或发布上位机。

本次树外证据根：`%LOCALAPPDATA%/CodexTemp/bms-storage-implementation-20261006`，包括 `host-round2/report.md`、后续固定提交回归、`development-link.log`、`target-build` 下 ELF/MAP/资源收据和 Windows 构建结果。不在源码树保存临时编译中间产物。

```powershell
python tests/run_host_regression.py --output <源码树外空目录>
python bms_tools/bms.py --all-products sources --check
python bms_tools/bms.py --all-products link --jobs 4
python bms_tools/bms.py --all-products resources
# 已提交且干净时：
python bms_tools/bms.py --all-products --production --d008-profile 16s-lfp link --jobs 4
python bms_tools/bms.py --all-products --production --d008-profile 16s-lfp resources
# 20s-nmc / 24s-lfp 再各运行 D008 link / resources
```

## 尚需硬件验收

首次运行新布局会按开发策略建立默认数据，旧 SOC/参数/事件不迁移。State/Event 突然掉电仍有未提交窗口；Event 约秒采样可能漏瞬态，Sleep 是尝试记录。实装 Flash 寿命、VDD 下降、真实写中断、watchdog/BLE 擦写延迟、ACC/AFE/PM 时序仍需实板验证。SH 默认固定 UART 门禁仍阻止低功耗。当前保护状态不作为持久 latch 保存，重启后的输出策略未改。

此次仅源码、host、ELF/MAP 与 Windows 编译验证；未生成固件 BIN、烧录、OTA 或连接操作设备。
