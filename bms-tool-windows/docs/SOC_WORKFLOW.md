# SOC 输入记录、离线分析与算法比较

继续使用既有 Windows 两版、CLI 和产品仓库中的 `tools/soc_simulator/soc_simulator.py`；不新增独立上位机。

普通 GUI / `record soc` 是多窗口、稀疏观察，适合趋势和现场诊断。电流现直接使用 diagnostics word196 的 signed mA，不再从0.1A显示值反推。它不是完整算法输入。

`record soc --inputs` 对开发固件读取每次 SOC 调用保存的完整输入，包括 tick、mA、电压、温度valid、fault、balance/heater/openwire、charger/load known、容量/化学体系/配置。三产品固件必须以 `EXTRA_DEFINES=-DBMS_SOC_RECORD_ENABLE=1` clean build，默认release没有此窗口和RAM缓冲；不支持时明确失败，不自动降级成伪Replay。

```powershell
bms-cli record soc --inputs --serial COM7 --count 300 --output C:\capture\soc-input.csv --json
# BLE替换为 --mac 已确认MAC。必须显式端点，拒绝 --auto。
```

文件为追加帧并逐帧flush的CSV，加 `.csv.json` 身份/build ID/完成状态。不会覆盖已有CSV。32帧环形区只有约6.4秒余量；出现覆盖、读帧不一致、复位/无输入/通信超时即结束。0=完成请求帧数；50=部分，130=取消，JSON ok=false并保留路径。旧固件/协议错误使用既有CLI错误处理。没有自动连接其他设备、修改配置或OTA。

传输只用现有 `ReadRegistersAsync` 与BLE通知分包。完整记录在一次32-word MCU主循环事务中读取，另校验首尾sequence。UART/BLE真实持续吞吐仍为TODO_VERIFY_HW，不能用模拟transport通过宣称实板已验证。

在D008/D011/D013产品工作树：

```powershell
python tools/soc_simulator/soc_simulator.py audit C:\capture\soc-input.csv --output C:\capture\quality.json
python tools/soc_simulator/soc_simulator.py replay C:\capture\soc-input.csv --output C:\capture\host.csv --metrics C:\capture\metrics.json
python tools/soc_simulator/soc_simulator.py ab C:\capture\soc-input.csv --baseline-runner C:\capture\old.exe --candidate-runner C:\capture\new.exe --output C:\capture\ab.json
```

runner由各待比较revision的 `python tests/soc_core_host_check.py --compile-soc-executable <绝对路径>` 编译，使用生产C而不是Python重写算法。完整输入缺失、非法数值、>400ms间隔或duplicate会被默认拒绝；探索性 `replay --allow-incomplete` 保留质量报告，不能拿来声称精度。没有独立true_soc时误差为null，固件soc_est绝不是独立真值。A/B报告提供输入/runner哈希和逐字段差异。

两版GUI使用“载入 SOC CSV”查看输入或host输出，显示间隔/duplicate数，避免把画历史曲线误称为执行算法。真正的C Replay/A/B由上述脚本执行，可供人、自动化和AI共用。无需连着设备分析已保存CSV。

当前是seeded Replay：起点的SOC相同不意味着细容量余数、rest、display、ETA和学习历史相同。尤其已学习容量与重启前checkpoint不完整恢复，因此不能声称任意现场bit-exact复现。输入CSV的soc_est/display为调用前状态，host输出为调用后状态，不应直接按行相减当成算法错误。

默认算法、OCV曲线、保护参数、Flash布局和OTA均保留。实板精度需独立电流/容量参考与完整电池轨迹，当前自动化测试中没有真实golden trace。
