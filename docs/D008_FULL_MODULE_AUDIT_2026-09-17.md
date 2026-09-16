# D008 BMS 全模块逻辑审核报告

审核日期：2026-09-17（Asia/Shanghai）  
仓库：CS19970929/telink-new-sdk-b85  
分支：refactor/d008-common-bms-features  
基线：`cf9293e92e61a5001feaf87b9ee7e157e6e98cbe`，审核开始与结束均确认远端未变化。  
产品：HS-D008 / TLSR8251F512ET32 / DVC1124-2。

## 1. 结论

当前 BMS 可按功能划分为 **10 个主要模块**，另有贯穿各模块的构建与验证体系。模块并不与单个 `.c` 文件一一对应；本报告依据固定 source order、实际调用链及配置所有权划分。

基础架构已有明确边界：DVC 固定配置与运行时保护参数分离，软件保护与 AFE 硬件保护独立，存在公共 AFE guard、requested/feedback 分离和统一 Flash journal。当前主要问题集中在**模块之间的异常恢复与输出授权**，不能仅凭编译和源码契约检查认定产品安全。

本次确认 **8 项软件问题：3 项 P1、5 项 P2**。其中 6 项用原始函数加模拟外设/状态的 C 程序复现，2 项由确定的协议长度与调用次数分析确认。P1 表示应优先修复的安全控制缺陷；P2 表示功能、异常恢复或时序问题。未把缺少实板证据直接标成软件 P0 缺陷。

**发布建议：暂不据此提交签核量产。** 优先关闭 F02、F08、F01，并完成仓库现有硬件发布阻断清单。这里的阻断建议基于已证实控制路径问题和缺失的硬件签核，不表示已观测到实板损坏。

审核阶段仅只读检查产品仓库。现按用户要求提交审核文档；未修改产品代码、接口、Flash 布局或上位机。本文结论和构建证据仍绑定上述审核基线，不代表文档提交自身已完成目标构建。模拟程序和完整证据包保留在独立审核目录。

## 2. 模块关系与逐模块评估

```text
main / app 初始化、200 ms 调度
  ├─ AFE guard → DVC采样 → 软件保护 → 合并AFE硬件告警
  │                 └─ 有效采样 → 加热 / Open-Wire / 均衡
  │    └─ Requested + 输出许可 + 故障/功能门控 → DVC R81模式
  │                                         └─ 下次采样R6反馈
  ├─ SOC积分、端点/OCV修正 → State持久化
  ├─ BLE / UART / Modbus → 参数事务、控制与诊断
  ├─ 事件记录 → Event持久化
  └─ 低功耗 / 唤醒 / OTA

软件保护参数、system、AFE requested profile → Config
SOC、循环、学习容量、运行时间              → State
Config / State / Event → storage_record → Telink Flash
```

当前顺序是先 `bms_afe_sample()`（含保护和输出 apply），再 SOC，最后 `mos_update()` 更新产品请求。因此产品输入变化可能在本次 apply 之后才传入 guard，这一顺序放大了 F01 的影响。[app.c:990](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c#L990)

| 模块 | 逻辑完整性 | 异常恢复 | 兼容性 | 可维护性 | 验证覆盖与判断 |
|---|---|---|---|---|---|
| 1. 初始化与调度 | 启动先参数、AFE、SOC，再输出许可；运行按名义200 ms采样 | I2C和Flash同步操作可拖延主循环；F07 | 保留既有SDK、ISR和工具链 | `app.c`同时负责BLE、产品请求、休眠，耦合偏多 | 目标编译通过；缺少最坏执行时间、抖动和实时性测量 |
| 2. AFE驱动与采样 | 长度/CRC、20-bit符号扩展、Rsense换算、20S屏蔽已有实现 | 整帧失败会使snapshot无效；读成功/写失败的非对称故障未闭合，F08 | 芯片配置固定、保留只读诊断窗口 | 底层、backend、guard边界清楚，但部分辅助访问失败未上送guard | 配置契约通过；电流、NTC、通道映射与采样新鲜度需实板 |
| 3. 软件三级保护 | 覆盖单体/总压/电流/温度/压差；Third参与MOS阻断 | 触发采用递减累计去抖，恢复连续确认；无效温度另走TEMP_BREAK | `g_tParam.protect`独立，写入先校验再保存 | 公共算法可复用；SOC Low由SOC模块另管，需统一说明 | 现有测试以源码契约为主，尚不能证明全部阈值/恢复时序 |
| 4. AFE硬件保护与配置 | 35-word事务、会话、量化、requested/effective与逐寄存器读回已有 | apply失败后的回滚与授权恢复存在P1缺陷F02 | 编译期固定策略未恢复旧operating-config KV；参数仍独立 | 校验、应用、协议事务跨多层，错误状态未形成统一门控 | 四种SW/HW构建通过；缺少事务阶段故障注入与掉电端到端测试 |
| 5. MOS与故障仲裁 | guard保存Requested；驱动反馈仅由采样更新；单侧AUTO_DIODE一次组合写 | app入口仍比较反馈决定是否更新目标，F01；写失败恢复见F08 | 维持CHGF/DSGF反馈语义、无虚构物理反馈 | 底层模式选择明确，但产品请求与资格状态跨模块 | 原始函数模拟已复现F01/F02/F08；实际栅极及续流波形待测 |
| 6. SOC与容量 | 积分、显示SOC、LFP/NMC数据、OCV向下修正及学习框架分开 | 无有效采样门控，旧电压仍可触发空电校准，F05 | 当前Storage V1保存整数SOC等；学习默认关闭 | 数据表与算法分层较好；名义周期与真实耗时未绑定 | SOC契约通过，但缺数值轨迹回放、丢样与调度延迟测试 |
| 7. 均衡/断线/加热 | 已有充电均衡、45s续期、Open-Wire互斥、GP1加热安全策略 | Open-Wire无总超时F03；最终断线判据尚未启用 | 只使用D008后端和板级GPIO；不可移植别板参数 | 均衡有公共层和DVC层两级状态；加热与不可逆熔断需明确签核 | 功能契约通过；断线检测只提供诊断，不能宣称已完成保护 |
| 8. 通信 | RTU长度/CRC、参数事务、只读窗口和授权会话存在 | BLE接收无重组F06，发送无队列重试；effective读取重复I2C见F07 | 不改既有寄存器/字段；本轮未审核Windows实现 | 读寄存器与外设访问耦合，UART DMA生命周期需强化 | 缺BLE长帧/拥塞、UART背靠背帧、畸形帧动态测试 |
| 9. 存储与事件 | Config/State/Factory/Event域明确，CRC、sequence、commit-last、跨sector轮换 | 通用journal已有断电模拟；业务层State保存失败被忽略且无退避 | 当前Storage V1明确不迁移旧KV；并非对所有历史固件无损升级 | 公共record engine较清晰，业务模块不直接操作Flash | RAM模拟覆盖普通写入、提交中断和轮换；不覆盖全部业务事务与电气掉电 |
| 10. 低功耗/唤醒/OTA | 存在AFE sleep、GPIO wake与SDK OTA集成 | OTA未阻止显式deep sleep，F04；休眠失败后AFE恢复需验证 | 保留既有A/B与保留区；未修改启动/升级格式 | suspend策略和显式deep sleep分散，条件不一致 | 目标构建通过，缺OTA中断点、GPIO竞态、睡眠电流及复位实测 |

评价中的“已有”表示代码路径存在并经阅读；不等于实板验收完成。

## 3. 确认缺陷（按整改优先级）

### F02 — P1：AFE保护回滚被guard阻断，不一致配置仍可恢复输出

**证据：** apply失败立即调用 `note_invalid()` 置 `comm_inhibit`；回滚又调用同一受该标志阻断的 apply 接口。因此 previous profile即使保存成功，也无法在这次回滚中重新应用到AFE。随后三帧有效采样会直接解除inhibit，并不检查 `CONFIG_INCONSISTENT` 或重新验证完整保护配置。底层 `DVC1124_ApplyProtectionConfig()` 失败也没有设置 `s_need_config` 强制重配。[bms_afe_guard.c:228](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_afe_guard.c#L228) [modbus_rtu.c:161](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c#L161) [bms_afe_guard.c:197](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_afe_guard.c#L197) [dvc1124.c:745](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/dvc1124.c#L745)

**触发：** 一次合法35-word写入已持久化，AFE某个保护寄存器写入/读回失败，后续测量读取恢复正常。

**影响：** Flash可以已回退，但AFE仍保留部分candidate/部分previous；软件报告INCONSISTENT时，旧ON请求仍可能被重新下发。若回退持久化本身失败，重启后也没有独立事务标志说明这次失败。

**复现：** 原始guard与原始rollback函数，模拟一次apply部分更新后失败：rollback的backend apply未被调用；三次有效采样后仍输出ON，INCONSISTENT保持。

**最小修复方向：** 配置不一致必须成为独立输出锁止条件；在保持OFF及通信恢复约束下，用明确的恢复流程完整应用previous并读回验证。仅采样恢复不能解除配置锁止。覆盖persist/apply/readback/rollback各失败点及重启；持久事务状态如需改变布局须另审兼容性。

### F08 — P1：持续FET写失败被成功采样清零，无法进入watchdog静默路径

**证据：** 每次 `AFE_AUX` 成功立即清零 `comm_failures`，而FET apply在其后；apply失败置inhibit后，接下来只凭三帧采样重新放行。持续“测量可读、FET写/验证失败”时失败次数无法累计到2。[bms_afe_guard.c:180](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_afe_guard.c#L180) [bms_afe_guard.c:156](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_afe_guard.c#L156)

**触发：** 输出原先ON，新的OFF或保护模式写入持续失败，而测量帧仍正常。

**影响：** 最后的有效硬件命令可能保持ON。MCU继续周期访问AFE，预期的5s总线静默不会启动；硬件watchdog最终关闭的保证不能从当前控制流成立。具体读访问是否刷新DVC watchdog、实际栅极动作仍需按官方手册与实板确认。

**复现：** 模拟FET后端持续返回失败、采样持续成功，100次采样后仍未bus_silenced，最后有效命令仍为ON。

**最小修复方向：** 区分采样资格与控制/安全写入资格；成功读样本不能清除尚未恢复的控制故障。达到界限后统一停止会刷新watchdog的访问；恢复须验证安全命令和固定配置，而不只是收到样本。

### F01 — P1：以反馈决定是否提交请求，可能遗漏关钥匙后的OFF目标

**证据：** `mos_update()`在目标与 `b1Status_MOS_CHG/DSG` 不同时才调用guard。guard内部虽然另存Requested，但这个入口没有比较Requested。[app.c:258](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c#L258) [bms_afe_guard.c:238](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_afe_guard.c#L238)

**触发：** 原Requested=ON/ON，保护或Open-Wire等使反馈OFF/OFF；此时关钥匙且无充电器，产品目标也是OFF/OFF。

**影响：** 因目标与反馈相同，OFF请求被跳过，guard保留ON/ON。限制解除后，采样内apply可恢复ON；本轮 `mos_update()`仍可能看到刚采到的OFF。直至后续采样看到ON才再关断，形成不应出现的重新开输出窗口；持续时间受调度影响，不能承诺固定200 ms。

**复现：** 原始 `mos_update()`与guard组合，关钥匙后Requested仍1/1，解除阻断的下一次采样再次发ON。

**最小修复方向：** 始终提交当前产品目标，复用guard已有的Requested去重；或只与独立Requested比较。新增“保护中关钥匙→保护恢复”“Open-Wire中关钥匙”“恢复资格期间关钥匙”测试。

### F03 — P2：Open-Wire等待无总超时，可能永久阻断充放电

**证据：** 超过settle时间后，COW未清除便直接return，没有deadline；公共层收到BUSY也没有超时或取消分支，active持续门控充放电。[dvc1124.c:1104](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/dvc1124.c#L1104) [dvc1124_feature_backend.c:107](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/dvc1124_feature_backend.c#L107) [bms_features.c:178](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_features.c#L178) [bms_features.c:282](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_features.c#L282)

**触发：** I2C可读，但COW持续为1，或检查迟迟不能满足新快照条件。

**影响：** 功能保持WAITING、均衡/输出持续受限，正常通信不一定触发guard恢复。

**复现：** 原始poll函数在成功I2C但COW恒1的模拟输入下，720秒仍WAITING。

**最小修复方向：** 按已确认手册时序增加总超时，返回明确错误并受控清理状态；是否保留安全锁止需明确，禁止无条件重新开MOS。测试COW卡住、丢样、插入充电器及重复start。

### F04 — P2：OTA期间仍可执行显式deep sleep

**证据：** `blt_pm_proc()`前半段无充电器/钥匙累计3s后调用deep sleep；后半段才检查 `ota_is_working` 并禁用BLE suspend。sleep入口本身也没有OTA检查。[app.c:546](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c#L546) [app.c:145](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c#L145) [app.c:659](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c#L659)

**触发：** OTA进行中，充电器和钥匙均不活动；或另一个显式电压休眠条件到达。

**影响：** OTA连接和传输可能被中断。不能据此断言镜像损坏或变砖，A/B断电恢复需单独实测。

**复现：** 原始PM函数在OTA=1、连接=1、无charger/key、elapsed=3s时仍调用显式sleep。

**最小修复方向：** 在显式休眠入口统一定义OTA与业务状态门控；如安全电压要求中止OTA，应先明确终止升级，再进入受控休眠。覆盖开关变化、欠压、OTA失败及正常完成。

### F05 — P2：AFE无效样本仍驱动SOC空电校准

**证据：** 采样失败只将snapshot invalid及电流清零，报告电压保留旧值；主循环仍无条件执行SOC。`soc_apply_idle_empty_anchor()`只检查静置和电压，没有采样有效性/年龄检查。[dvc1124.c:1278](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/dvc1124.c#L1278) [app.c:995](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c#L995) [SocEnhance.c:993](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c#L993) [SocEnhance.c:1236](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/SocEnhance.c#L1236)

**触发：** 最后一帧电压较低，随后AFE失联；或启动就未取得有效电压。

**影响：** 无法判断真实电压/电流时仍逐步降低SOC，随后State持久化错误校准结果。其他OCV资格也只检查数值范围，不检查样本年龄。

**复现：** 原始idle-empty函数输入采样失败后的“旧2900mV、零电流”状态，75次调用使SOC从60降至50。此测试只证明算法对失效状态的响应，不模拟真实电池。

**最小修复方向：** SOC明确消费有效且新鲜的测量；失效时冻结积分及端点/OCV修正，并重置资格计时，恢复后重新确认。补启动失败、连续丢样和恢复测试。

### F06 — P2：默认BLE传输无法提交完整AFE保护事务

**证据：** 本固件ATT MTU=23，单次普通ATT Write有效载荷20字节。接收回调把每个Write直接交给 `modbus_on_frame()`，没有应用层帧拼接；35-word事务必须一次完整写入。构建日志未提供MTU覆盖。[app_buffer.h:81](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/common/app_buffer.h#L81) [app.c:736](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c#L736) [app_att.c:397](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app_att.c#L397) [modbus_rtu.c:669](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c#L669)

**触发：** 通过当前SPP普通Write路径发送35-word AFE配置，或把该请求拆成多个20字节Write。

**影响：** 请求需要 `1+1+2+2+1+70+2=79` 字节，单包装不下；拆包又无法在固件重组，无法完成这条BLE写路径。UART缓冲可容纳，不受这个长度限制。此结论不涉及Windows是否有提示、另选串口或其他流程，本轮未审核Windows。

**最小修复方向：** 先明确产品支持的transport；若必须BLE写，应提供经SDK支持的完整接收能力或有界帧重组，保留35-word事务语义，不能通过单寄存器写绕开事务。本轮不修改协议。

### F07 — P2：读取35-word effective反复读取整套AFE，阻塞超过采样周期

**证据：** 每读取一个effective寄存器都调用一次 `bms_afe_hw_profile_get_effective()`，该函数读取至少12项硬件值。Modbus 0x03逐word循环调用，35-word请求产生至少420次I2C读取。[modbus_rtu.c:114](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c#L114) [bms_afe_hw_profile.c:324](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_afe_hw_profile.c#L324) [modbus_rtu.c:632](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c#L632) [dvc1124_config_service.c:46](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/dvc1124_config_service.c#L46)

**触发：** 正常读取 `0x2540..0x2562` 全部effective profile，默认SCD关闭。

**影响：** 按当前100kHz总线和每字节CRC，4个双寄存器读取各7个线上字节、8个单寄存器读取各5字节：`35×(4×7+8×5)×9/100000 = 214.2 ms`，尚不含软件与start/stop开销。一次诊断读取的总线占用已超过名义200ms采样周期。异常重试会进一步延长；同一响应中的字段也并非一次采集快照。

**验证性质：** 源码调用次数和总线位数推导，非实测时延。

**最小修复方向：** 每个Modbus请求获取一次完整effective快照再序列化，避免按word重复整表读取；明确失败返回和访问预算。测试I2C调用次数、响应一致性及采样抖动。

## 4. 待验证风险与实现边界

以下不计入上面的8项确认缺陷，避免将硬件未知或尚未闭合的推断当作确定结论。

| 项目 | 已观察证据 | 需要补的验证 |
|---|---|---|
| 加热与不可逆熔断 | GP1≥95°C且heater命令OFF持续10s会拉高PD4；源码声明该动作为不可逆。`board_init()`旧注释却称量产不得触发 | 原理图/BOM、熔断驱动极性、热惯性、故障判据与产品授权签核；不能仅以“温度仍高”证明驱动已短路。[dvc1124_project_config.h:97](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/dvc1124_project_config.h#L97) [bms_board.c:93](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_board.c#L93) [bms_features.c:80](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_features.c#L80) |
| Open-Wire保护完成度 | backend固定 `determinate=0`、`open_cell_mask=0` | 真实断线夹具和阈值签核；当前只能称原始诊断，不能声称已有有效断线判定。[dvc1124_feature_backend.c:121](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/dvc1124_feature_backend.c#L121) |
| AFE样本资格 | CRC通过就视为有效；generation是软件读取计数 | AFE转换冻结但I2C正常、转换完成时序、读期间一致性；三次读取不自动等于三次新转换 |
| 均衡错误传播 | 低层BalanceService是void，部分刷新失败被忽略；公共层可能回报旧mask或清除错误 | 注入balance写/读失败，核对硬件实际mask、错误发布及guard策略。[dvc1124.c:1032](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/dvc1124.c#L1032) [dvc1124_feature_backend.c:76](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/dvc1124_feature_backend.c#L76) |
| UART DMA生命周期 | 单RX/TX缓冲；主循环直接消费DMA指针，发送前未检查busy/返回值 | 背靠背帧、解析中到帧、DMA写入边界及发送缓冲重用实测；不凭volatile推断安全。[modbus_uart.c:80](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/modbus_uart.c#L80) [modbus_uart.c:102](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/modbus_uart.c#L102) |
| BLE发送拥塞 | notify循环遇到首个失败即返回，调用者忽略结果 | 满队列/断连/最大响应时部分帧丢失，主机超时与重试行为。[app_att.c:371](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app_att.c#L371) |
| State持久化失败 | 主循环每轮调用changed-save，返回值被丢弃，没有错误上报/退避 | Flash持续失败时是否高频重试/擦写、阻塞以及SOC恢复；与journal掉电原子性分开验证。[bms_state_store.c:138](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/bms_state_store.c#L138) |
| 休眠未成功 | AFE先sleep，MCU若GPIO条件导致未入睡，仅更新runtime tick | 注入GPIO临界变化，核对AFE重新唤醒、资格恢复、OFF维持和重试。[app.c:145](https://github.com/CS19970929/telink-new-sdk-b85/blob/cf9293e92e61a5001feaf87b9ee7e157e6e98cbe/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/app.c#L145) |
| 数值时间基准 | SOC、保护和资格确认按调用次数折算200ms | Flash操作、诊断负载、重配、睡眠造成的实际时间差；积分应使用经校验的实际时间或明确丢样策略 |
| 运行时参数替换 | 软件参数写后保留原滤波状态；effective enable_mask由requested复制 | 阈值更改中滤波计数继承的产品语义；硬件独立reset/漂移时启用状态是否能被诊断发现 |

硬件真值资料限制：本次远端快照未发现D008原理图PDF/BOM原件或DVC1124-2 V1.2完整手册；只能核对仓库配置、文档和厂商示例。未重新独立认证寄存器公式、GPIO电气连接及芯片时序；这些记为 `TODO_VERIFY_HW`，未用其他AFE的经验补值。

## 5. 验证证据

### 5.1 本地只读快照检查

在该SHA的独立下载快照中运行以下 **13个检查入口，退出码全部0**：source-order；tooling unittest；DVC config；D008 framework；common-port FET；20S profile；AFE access；documentation；software protection；common features；AFE profile；SOC；Flash。原始检查日志摘录见附录 A。

必须区分测试类型：多数保护/SOC/框架contract是源码字符串、常量和结构检查，不能证明故障状态机行为；`flash_quick_check.py`另编译并执行真正的 `storage_record_host_test.c`，覆盖RAM模拟Flash的普通写入、header/payload/commit中断与sector轮换。该测试不包含AFE事务回滚、全产品掉电或硬件电气时序。

新增审核模拟测试 `reproduce.py` 与 `reproduce.log` 保存在此前交付的独立证据包 `D008-audit-cf9293e9.zip` 中；本次仅提交文档，不加入模拟程序、源码副本或二进制产物。复现结果见附录 B。脚本从固定SHA源码副本提取原始函数，模拟外设返回值及环境状态；确认F01/F02/F03/F04/F05/F08的控制流。它们不是TC32或实板测试，且不能证明硬件一定呈现注入故障。F06/F07为静态可确定的长度/访问次数分析。

### 5.2 当前SHA的远程目标构建

[GitHub Actions run 35143263641](https://github.com/CS19970929/telink-new-sdk-b85/actions/runs/35143263641) 对应本审核SHA，Host与TC32 jobs均成功。实际TC32执行机器是 **DESKTOP-UU5VC9R**，runner名 `DESKTOP-UU5VC9R-telink-tc32`，属于Windows self-hosted；不是GitHub-hosted TC32编译。工具链 `tc32-elf-gcc 4.5.1-tc32-1.3`，SDK保持锁定版本。

| 检查 | 证据与限制 |
|---|---|
| clean rebuild | 默认24S LFP；SW/HW=1/0、0/1、0/0和最终1/1均实际执行成功 |
| 20S NMC | 有20S源码contract；本run没有20S TC32构建步骤，不能声称20S已完成目标编译 |
| check-fw / size / MAP | 均成功；text=96,692、data=4,000、bss=4,656字节；日志data+bss=8,656不是含ram_code/stack的完整RAM峰值 |
| BIN | 100,900字节；126,976字节slot尚余26,076字节；这是镜像容量余量，不是运行时资源安全证明 |
| manifest / verify | 当前commit、工具链、源序、93个对象、vendor库等验证通过 |
| cppcheck | 30个C单元、41个应用头文件，SDK依赖头仅解析；90条style，MISRA未执行；不能将“coverage gaps=0”理解成整个SDK均完成同级分析 |
| Artifact | BIN、ELF、MAP、build.log、manifest和static输出存在；14天保存，预计2026-09-30到期，并非正式发布永久归档 |

已下载当前run的 [Artifact 10466222311](https://github.com/CS19970929/telink-new-sdk-b85/actions/runs/35143263641/artifacts/10466222311)，独立计算ZIP及BIN哈希，与GitHub元数据和manifest一致：

```text
ZIP SHA-256
4a468640ea20db9591f3901d58b5d01ee12858f9cdf1d453fe3967eafee29f9c
BIN SHA-256
e3a46a34ae1b484f4dc6d1d1b9f1d07acc33400e0af1974c85c23e79d3a68e76
```

本次没有重新触发CI，复核的是**完全相同SHA**已完成的远端run。没有进行烧录、硬件在环或实板运行，也没有宣称本地host测试与TC32二进制等价。

## 6. 实板未决与整改顺序

**先修软件P1：** F02配置事务锁止 → F08读/写故障资格分离 → F01产品请求始终传递。每项以故障注入重现原问题、验证修复，并在同一候选远端SHA完成生产和隔离模式构建。

**再关闭P2：** F03诊断总超时、F04 OTA显式休眠门控、F05 SOC采样有效性、F06 BLE完整事务能力、F07单请求快照读取。相应测试应验证行为而非只匹配字符串。

**量产前硬件证据：**

1. 24S/20S逐通道、GP1/2/3/4装配与NTC曲线、电流方向/零点/增益/温漂。
2. 保护阈值与延时从requested→code→effective→实际driver/gate波形的全链路；SCD未签核前不擅自启用。
3. SDA/SCL卡死、NACK/CRC、AFE掉电，以及“测量可读但控制失败”的注入；验证4s watchdog、mask和恢复竞态。
4. 单侧保护反向电流AUTO_DIODE、关钥匙、双侧保护、通信恢复与Open-Wire期间MOS行为。
5. 加热输出、95°C/10s不可逆熔断策略和热惯性，先用安全夹具完成判据签核。
6. AFE profile事务各阶段掉电/Flash失败；State/Event存储失败与重启；OTA A/B中断点及参数保留。
7. 正常、连接、OTA、deep sleep、wake的电流和时序，尤其GPIO导致休眠失败后的恢复。

记录必须绑定板号/BOM、芯片版本、固件SHA、24S/20S profile、requested/effective参数、仪器与波形。未提供记录的项目保持 `TODO_VERIFY_HW`。

**维护建议：** 修正文档与当前源码的明显差异：软件保护文档称触发连续样本，但实现是leaky debounce；SOC文档仍描述已移除的旧KV追加key；HARDWARE_VALIDATION中Body-Diode“当前关闭”和GP1/GP4旧角色与最新配置不一致；app中PD4旧注释与运行时熔断路径矛盾。以既有真源文档更新，不另造长期架构说明或重构框架。

后续代码修复按用户要求在GitHub远端实施；本报告不构成自动合并、发布或烧录授权。

## 附录 A：本地检查日志

```text
COMMAND: python3 bms_tools/bms.py sources --check
[bms] source order OK: 93 entries, sha256=4c9ec17fa2bddfda5cd9138f70c36f549c8c926b9d84181d0ea6f6354934d54f

EXIT: 0
COMMAND: python3 -m unittest tests.test_bms_tools -v
test_qt_project_path_follows_repository_tools_layout (tests.test_bms_tools.ClientAssetPathTests) ... ok
test_only_development_toolchain_commands_are_exposed (tests.test_bms_tools.CommandSurfaceTests) ... ok
test_crc32_known_vector (tests.test_bms_tools.IntegrityPrimitiveTests) ... ok
test_raw_image_has_no_telink_trailer (tests.test_bms_tools.IntegrityPrimitiveTests) ... ok
test_telink_crc_rejects_corruption (tests.test_bms_tools.IntegrityPrimitiveTests) ... ok
test_telink_crc_trailer_contract (tests.test_bms_tools.IntegrityPrimitiveTests) ... ok
test_cli_outputs_are_inside_project_and_separate_from_ide (tests.test_bms_tools.OutputPathTests) ... ok
test_discovery_is_case_sensitive_and_interleaves_c_and_assembly (tests.test_bms_tools.SourceOrderTests) ... ok
test_parse_ide_order_follows_makefile_include_and_objs (tests.test_bms_tools.SourceOrderTests) ... ok
test_read_rejects_duplicate_case_collision (tests.test_bms_tools.SourceOrderTests) ... ok
test_validation_rejects_unlisted_new_source (tests.test_bms_tools.SourceOrderTests) ... ok
test_extracts_real_compile_settings_without_manual_flag_lists (tests.test_bms_tools.StaticAnalysisPrimitiveTests) ... ok
test_function_locator_handles_multiline_c_function (tests.test_bms_tools.StaticAnalysisPrimitiveTests) ... ok
test_report_runtime_is_isolated_from_static_evidence_directory (tests.test_bms_tools.StaticAnalysisPrimitiveTests) ... ok
test_scope_exclusion_partition_never_suppresses_application (tests.test_bms_tools.StaticAnalysisPrimitiveTests) ... ok
test_static_scope_accepts_only_ble_sample_paths (tests.test_bms_tools.StaticAnalysisPrimitiveTests) ... ok
test_unmodified_sdk_style_is_not_auto_approved_or_auto_deviated (tests.test_bms_tools.StaticAnalysisPrimitiveTests) ... ok
test_canonicalises_windows_path_key (tests.test_bms_tools.ToolchainEnvironmentTests) ... ok
test_does_not_prepend_when_tool_is_already_resolvable (tests.test_bms_tools.ToolchainEnvironmentTests) ... ok
test_tc32_tool_prefers_pinned_absolute_path (tests.test_bms_tools.ToolchainEnvironmentTests) ... ok
test_external_fork_prs_cannot_reach_tc32_runner (tests.test_bms_tools.WorkflowSecurityTests) ... ok

----------------------------------------------------------------------
Ran 21 tests in 0.013s

OK

EXIT: 0
COMMAND: python3 tests/dvc1124_config_quick_check.py
................
----------------------------------------------------------------------
Ran 16 tests in 0.005s

OK

EXIT: 0
COMMAND: python3 tests/d008_framework_contract_check.py
........................
----------------------------------------------------------------------
Ran 24 tests in 0.005s

OK

EXIT: 0
COMMAND: python3 tests/d008_common_port_fet_contract_check.py
.......
----------------------------------------------------------------------
Ran 7 tests in 0.001s

OK

EXIT: 0
COMMAND: python3 tests/d008_20s_profile_contract_check.py
D008 20S/product safety completion contract: PASS

EXIT: 0
COMMAND: python3 tests/afe_hw_access_contract_check.py
AFE hardware access/transaction/effective-profile contract: PASS

EXIT: 0
COMMAND: python3 tests/d008_documentation_contract_check.py
D008 documentation contract: PASS

EXIT: 0
COMMAND: python3 tests/sw_protection_contract_check.py
Unified software protection contract: PASS

EXIT: 0
COMMAND: python3 tests/common_feature_policy_contract_check.py
common feature policy contract: PASS

EXIT: 0
COMMAND: python3 tests/afe_hw_profile_contract_check.py
D008 independent AFE hardware protection profile contract: PASS

EXIT: 0
COMMAND: python3 tests/soc_contract_check.py
test_coulomb_integration_and_deadband (__main__.SocContract) ... ok
test_diag_reports_profile_identity_and_version (__main__.SocContract) ... ok
test_display_soc_is_separate (__main__.SocContract) ... ok
test_dual_chemistry_profiles_are_data_not_algorithm (__main__.SocContract) ... ok
test_endpoints_and_lfp_terminal_knee_are_chemistry_specific (__main__.SocContract) ... ok
test_explicit_profile_wins_and_mismatches_are_rejected (__main__.SocContract) ... ok
test_learning_is_state_data_and_default_disabled (__main__.SocContract) ... ok
test_ocv_requires_ten_minutes_and_uses_band (__main__.SocContract) ... ok
test_product_chemistry_and_profile_are_config_fields (__main__.SocContract) ... ok
test_soc_low_faults_are_implemented_without_mos_policy (__main__.SocContract) ... ok
test_upward_calibration_requires_confirmed_charging_full_anchor (__main__.SocContract) ... ok

----------------------------------------------------------------------
Ran 11 tests in 0.000s

OK

EXIT: 0
COMMAND: python3 tests/flash_quick_check.py
test_config_and_state_have_explicit_little_endian_formats (__main__.ArchitectureTests) ... ok
test_event_latch_is_only_advanced_after_successful_persist (__main__.ArchitectureTests) ... ok
test_flash_protection_session_contract_remains (__main__.ArchitectureTests) ... ok
test_old_kv_engines_are_out_of_build_and_removed (__main__.ArchitectureTests) ... ok
test_record_core_has_crc_sequence_and_commit_last (__main__.ArchitectureTests) ... ok
test_record_core_is_platform_independent (__main__.ArchitectureTests) ... ok
test_runtime_is_part_of_state_not_a_second_flash_engine (__main__.ArchitectureTests) ... ok
test_semantic_stores_share_record_engine (__main__.ArchitectureTests) ... ok
test_state_keeps_changed_value_write_semantics (__main__.ArchitectureTests) ... ok
test_telink_flash_access_is_confined_to_platform_adapter (__main__.ArchitectureTests) ... ok
test_portable_record_engine (__main__.HostPowerLossTest) ... ok
test_512k_layout_is_the_reviewed_storage_v1_map (__main__.LayoutTests) ... ok
test_domains_do_not_overlap (__main__.LayoutTests) ... ok
test_domains_end_before_sdk_pairing_identity_area (__main__.LayoutTests) ... ok
test_every_domain_has_power_loss_rotation_room (__main__.LayoutTests) ... ok
test_ota_guards_are_still_explicit (__main__.LayoutTests) ... ok

----------------------------------------------------------------------
Ran 16 tests in 0.830s

OK

EXIT: 0
```

## 附录 B：模拟复现结果

```text
REPRO F01: key OFF + feedback OFF leaves request=1/1; next safe sample commands ON=1/1
REPRO F02: rollback backend blocked, persisted=7 hardware=99; after 3 valid samples command=1/1 while INCONSISTENT
REPRO F08: 100 successful snapshots + persistent FET-write failures never silence bus; last hardware command remains ON=1/1
REPRO F03: COW stuck high with successful I2C remains WAITING after 720 simulated seconds
REPRO F04: OTA active + charger/key OFF calls explicit deep sleep after 3 seconds
REPRO F05: failed AFE sampling leaves stale 2900mV + zero current; idle-empty strategy changes SOC 60 -> 50 in 75 calls
```

## 附录 C：证据哈希记录

```json
{
  "commit": "cf9293e92e61a5001feaf87b9ee7e157e6e98cbe",
  "zip_sha256": "4a468640ea20db9591f3901d58b5d01ee12858f9cdf1d453fe3967eafee29f9c",
  "bin_sha256": "e3a46a34ae1b484f4dc6d1d1b9f1d07acc33400e0af1974c85c23e79d3a68e76",
  "manifest_bin_sha256": "e3a46a34ae1b484f4dc6d1d1b9f1d07acc33400e0af1974c85c23e79d3a68e76",
  "source_copies": {
    "bms_afe_guard.c": "2c97b3ff82e8202028081060463a2cd8d410be73a831754db12cd16ead2e17a3",
    "SocEnhance.c": "6fbd49f68ec5604042bd87df10a5794586f2ed3ec74f813ce61e5b1c846c0161",
    "modbus_rtu.c": "6414bd0d0e1f2104d41effc02f11c2dcbb86b140c127e00426cad6e5ce0a7e22",
    "dvc1124.c": "f5cd2c0f37bd8be4e3ec04a9297b6e01e120453bed42c25e4215223c6cd42727",
    "app.c": "8152e746120064bf56a2024dea53a2c90c0dc427fbe8b895c40651f56b097231"
  }
}
```
