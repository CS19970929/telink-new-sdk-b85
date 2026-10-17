# BMS 全工程审查与分轮简化（2026-10-17）

## 源码身份与范围

基线为本机当前分支 `codex-bms-deep-simplification-20261016` 的 `b9491cbab374a6b08fa6a893702f145a27913f9f`，原工作区干净，origin 为 CS19970929/telink-new-sdk-b85。本任务按用户本次隔离要求建立独立 worktree；现有 `codex` 分支占据 refs 命名空间，因此使用 `codex-bms-audit-simplify-20261017`。没有合并、推送、修改原工作区或生成 BIN。

四产品生产归属以各自 sources.txt 为准：D008 102项，D011/D013/D014 各101项，包含固定 SDK。固件使用统一 Python 工具调用 TC32 GCC，link 仅 ELF/MAP/LST，resources 检查镜像投影、32 KiB SRAM与栈余量。根 CMake 是可移植核心的主机入口，不是固件构建替代品。

## 修改前审查结论

当前已是单份 app/core 加两个真实 AFE 后端，不能仅凭117个 C/H 文件认定架构过度。主要维护成本集中在采样有效性、保护恢复、持久事务和 SOC。此次按产品配置和真实调用链审查生产 BMS、平台端口、构建入口及相关测试；不声称逐行形式验证封闭 SDK、芯片电气行为或全部组合状态。

| 领域 | 实际调用链与所有者 | 判断 |
|---|---|---|
| 启动/主循环 | main → app_init → parameters/AFE/SOC；main_loop → power hold → BLE → sample → storage → mux/UART → PM | 顺序直接；保持失效启动阻断、先更新保护再执行输出 |
| 采样/输出 | app_sample_task → bms_afe_sample → driver；features → bms_afe_apply_outputs → SOC | 新鲜帧和有效缓存解决不同问题；保留三帧资格、config_inhibit与总线静默，不能用请求代替命令或回读 |
| DVC 后端 | CRC读清事件 → VADC/CC2资格 → 电压/温度/电流发布 → 公共保护与独立硬件锁存恢复 | 事件保存、NTC连续资格、有效串掩码和物理恢复有真实消费者；保留独立实现 |
| SH 后端 | SPI/CRC → 完整帧发布 → CRLD检测与短路/过流恢复 → FET命令/回读 | C+与LOAD检测模式切换、等待期和短路锁存是芯片/产品约束；不与DVC硬合并 |
| 加热/均衡 | features读取已接收快照、用户参数；板级GPIO和AFE回读确认输出 | 不可逆fuse、过热回差、OFF待确认具有独立语义；局部布尔镜像可进一步核对 |
| SOC | app_update_soc_from_sample → bms_soc_process_sample → 积分/端点/OCV；Config保存完整profile | 完整profile之外仍复制5个字段、两套reload和AUTO/OVP推断；schema4已要求明确LFP/NMC，存在可删除内部兼容流程 |
| 参数/存储 | Modbus参数验证 → Config临时next → journal CRC/commit → RAM发布；State/Event独立 | 三域具有不同寿命和提交语义，不能靠合文件消除；完整参数profile的重复快照值得减少 |
| 通信 | BLE/UART接收 → Modbus长度/CRC/范围 → 参数/AFE事务；D008 mux/SIF独立 | DMA所有权、一次待应答、50ms发送退出和SIF时序是真实硬件边界；保持地址/字节序/61001 |
| 低功耗 | app_power_process → 业务休眠资格 → 通信收尾 → 保存/AFE → 板级断电或DeepSleep | 普通Suspend和强制DeepSleep触发/失败策略不同；不增加空闲休眠入口 |
| 配置/构建/诊断 | 产品宏、sources、统一工具；生产诊断关闭 | 不复制产品业务源码，不另搭框架；测试中存在时基和接口变更后的失同步 |

有证据的原有缺陷：D013不支持加热和均衡，公共guard及SH后端因此裁掉feature快照；但app中的SOC始终需要其中电池温度。导致健康D013样本无法取得SOC温度资格。原ATOMIC测试也使用相同能力宏跳过断言，掩盖了问题。

必要复杂度包括：watchdog总线静默、三次新鲜样本、配置回滚/提交、均衡请求与已验证状态、关MOS后不能用零流恢复过流、Flash损坏/旧版输出禁止、低功耗恢复。没有证据支持删除这些链路。代码含历史CFG2/独立SOC持久域注释，当前实际是CFG4内完整profile；这是重复加载逻辑的演进痕迹，不是对历史开发原因的猜测。

## 分轮实施

1. 恢复验证能力：调度脚本错误嵌套、旧void采样契约、微秒/32k到毫秒转换后夹具未更新。保留原场景、Werror、精确边界及回绕断言；删除无消费者的时钟替身。
2. 温度数据流：移除guard和SH的能力裁剪，保留采样有效性与通信门禁；DVC复用已有电池温度范围函数。SH原子采样测试覆盖全部三产品，联合真实后端/guard/SW检查健康、NTC断线和ADC失效。
3. 后续以验证后的代码继续审查SOC与冗余状态；最终结果在本报告补充。

## 验证记录与边界

原始基线136组，17失败，四产品sources、link、resources通过。针对原失败项修复后20组复测加3组边界复测恢复通过；一次Windows临时EXE访问拒绝重跑通过。原失败和复测日志全部保留，没有把历史记录写成当前通过。

D013独立反例在修改前失败于 `bms_afe_get_feature_snapshot(&temperature) && temperature.battery_temp_valid`。SH联合测试和原子采样测试覆盖完整后端函数体；控制器/芯片读写由夹具提供，不能证明NTC精度、物理Gate或实板OCV效果。

旧板不迁移不兼容参数：普通镜像遇旧格式/非空损坏存储继续禁止输出；明确factory_reset或有线擦业务区后采用当前默认，三域完成持久提交才取得输出资格。未改schema4、Flash边界或外部协议。

实板未执行：物理保护恢复、Gate/Vgs、watchdog关断、Flash掉电、自然唤醒/休眠电流、OTA，以及D014既有硬件阻断仍待验收。本轮没有新增硬件策略或宣称产品安全验收通过。

温度轮稳定源码验证：四产品sources检查，14组相关host/契约加8组link/resources，共22组通过。初次DVC复用编辑遗漏条件块被Werror捕获后立即修复，完整复跑通过；未将运行中改动源码的初次收据计为通过。
