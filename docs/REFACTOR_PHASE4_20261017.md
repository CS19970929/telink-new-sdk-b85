# 有效配置、SOC 与协议读取的四项简化

基线为 `b76b8a763dfe0a6141f3fd3b2ad03be0690d62e5`，分支 `codex-bms-deep-simplification-20261016`。本轮按用户授权分四次提交；保留在线 SOC 曲线编辑、放电末端补偿、现有上位机协议和安全行为。四产品 `sources.txt` 仍是编译归属依据。

## 1. DVC 有效保护配置

核心整组查询直接调用 DVC 后端整组回读，不再逐字段经过语义窗口。后端集中解码，旧 `0x2850..0x285D` 字段共用相同规则、保持原读取范围。成功整组读取由 12 次（启用 SC 时 14 次）变为三个连续块：`0x70..0x73`、`0x59..0x5C`、`0x5E..0x61`（SC 时至 `0x63`）。全部通过底层 CRC/有限重试后才填写实读字段；任一块失败不发布部分值，总线静默门禁保持。SH 查询仍只用已验证的缓存，不增加 SPI。

依据为项目原件 DVC1124-2 DS V1.1 PDF 第 23 页（连续读及 CRC），RM V1.2 第 18–20、23–24 页（R/W 配置、阈值、延时）。这三个块不包含 RC 状态；不改寄存器写入、SC/OCD/OCC 恢复、默认参数和 OTA 策略。型号后缀、板级装配与实测 Rsense 的未决项见 [AFE 资料指南](AFE_REFERENCE_GUIDE.md)。

新增 host 场景用原 RM 公式核对全部字节编码、四种 shunt（含零和饱和）、三个块逐次失败、总线静默和 35-word 未覆盖字段保持；三个 D008 profile × HW_PROTECT 0/1 均通过。相关 `dvc_register_scenarios`、`dvc1124_fixed_policy`、`dvc_startup_protection`、`afe_hw_transaction`、D008 `modbus` 共五组既有 host 入口通过；D008 development 目标链接为 0 error/0 warning。最终固定提交与四产品资源结果在完成后追加。

## 2. SOC 已提交曲线只有一个所有者

默认曲线、journal 与算法共用原 100-byte/50-word 配置表示，算法通过只读视图使用存储模块已提交对象，删除另一份运行 profile、15 点转换表及逐成员搬运。stage 候选仍单独保存，写入失败不发布；只读对象地址固定，generation 改变时仍撤销旧 OCV 资格。所有调用在主循环同步执行，不把该指针当作 ISR 并发快照。AUTO、类型不匹配及无可用曲线仍走原默认回退。

整套 Config 已提交后的 SOC 加载入口改为 `bms_soc_reload_configuration()`，直接读取、校验并发布，不再次调用 `bms_config_store_set_soc()`。已确认旧 `bms_soc_configure()` 唯一生产调用者就是该提交后路径；线上参数写入仍经过原 journal/授权事务。

四产品 SOC 长时回归、7 天 10081 行轨迹及每产品三种诊断模式的 91201 行业务轨迹均通过，16 个 CSV 与基线逐字节相同。新增同地址 generation 刷新、自定义曲线插值及读配置失败场景；真实 profile/journal TU 覆盖 LFP/NMC 各 132 个写入字节断点、候选隔离、写失败/重启保持旧曲线、100-byte little-endian payload、未使用点零值、重复保存去重、身份不匹配和读失败禁写。既有语义存储与更新后的源码契约检查通过。

另从固定基线 `git show` 提取旧默认构造与旧曲线数据，与新构造在四产品分别编译，LFP/NMC 全部 100 个 payload 字节完全一致；不只比较新实现自身编码/解码往返。阶段四产品 development 链接均为 0 error/0 warning；最终提交后再执行统一链接收据。

## 3. 参数块读取复用本请求快照

Modbus 先验证完整地址，再把连续业务参数交给 `bms_parameter_read_block()`。请求内按需读取 SOC profile、user、system、aux 和 sleep 各一次，包含读取失败结果；没有全局缓存或跨请求过期状态。协议字直接写入原响应缓冲区，保留普通寄存器与参数混读顺序、非法尾地址无部分读取、`0xFFFF` 未知值及大端字节序。删除唯一生产消费者原来的逐字参数入口；写入、授权、generation、50-word stage 和 commit 未修改。

现有客户端继续每次读 7 word、写 4 word。50-word 曲线一次读取由 50 次 100-byte 复制变为 1 次；7-word 读取由 7 次变为 1 次。SN 16-word 块只读取 user 一次。此项用少量请求内有效标记换取稳定块视图和减少重复复制，不宣称每个模块源码行数都下降。

从 `b76b8a76` 抽取原逐字函数，与新块函数在四产品分别比较全部 8036 个可读子块（正常、user/system/aux 三种失败条件），输出逐字节相同。新增回归检查 50-word 和 7-word 客户端分块、SN/校准/类型/两档休眠、tick 回绕、失效未知值、空指针、数量和地址回绕、非法尾地址无读取；四产品 Modbus、语义存储和源码契约入口通过。

## 验证边界

本轮只执行用户授权的相关 host 回归及四产品 sources/link/resources，不生成 BIN。host 与 ELF/MAP 不能替代实板 I2C/SPI 电气、物理保护恢复、MOS Gate/Vgs、Flash 掉电、低功耗/WDT、OTA 及现有 EXE/Android 与设备联调验收。
