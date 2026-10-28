# D015 / STM32F030 + SH3673520 配套支持

基线是feature/windows-afe-hw-protection-editor-v2的a6bcc552；本改动只更新既有客户/内部Windows共享协议、模型和必要界面，不修改D008/D011/D013/D014固件。

## 产品与协议

- 硬件字符串HS-D015必须与D130两字D015/0320相符；35字AFE profile报告真实3520，能力DF。旧3520的magic3520/24字接口仍独立识别，不误当成D015。
- 软件保护沿用2100/65字；D015允许单级阈值0禁用、温度范围-40..125℃，每次五字组写入并读回。
- AFE 2500/35字请求、2523 metadata、2540/35字有效值，42授权和MTU23暂存/提交沿用原语义。D015新增实际支持的OV/UV/OCD1+2/OCC/SC/TS1+2使能项，多个修改按mask合并。OCD共用硬件位，温度未开放TS3/4；其他型号保持仅SCD使能可编辑。
- D015按不晚保护方向向下选择电流档；既有3510向上量化规则保持。短路选项使用量化后的OCD2及精确分流比例，不把D015规则套给旧产品。
- OCD/OCC恢复电流共用，界面只保留一个编辑项，同步profile14/20；OV/UV/OCD/OCC恢复时间独立。Powerdown一直关闭，深睡由固件先确认双MOS关闭，再进入AFE Sleep。

## 电流校准与参数

2E24四字offset_mA/gain_ppm原子写，2E28七字原始/最终电流、valid及32000Hz采样tick；复用现有零点/两点向导及token连续性、新样本和结果未知处理。D015打开会话后冻结启动RAM修正，保存/恢复必须断开开关并确认MOS关闭；采集加载点时允许闭合开关，但仍受全部保护仲裁。

SN继续走2E40/2E50暂存，一次提交Flash；参数采集过滤D015未声明的工厂模式、加热、蓝牙名，不虚构这些功能。型号未开放的恢复默认组仍由固件拒绝，不继承Telink的其他业务参数。

## 升级边界

旧FFFD/FFFE/FFFF帧未改变。D015 APP起址08001C00，上限47KiB，身份D0150320；文件必须只含LR_APP并按1024字节补FF。加载时校验D015身份、实际长度/补页、向量及47KiB上限；已识别D015的内部升级流程拒绝其他身份文件。通用STM32栈顶允许等于20002000（合法空栈顶），仍要求D015栈8字节对齐。

未生成真实固件BIN/HEX、安装或发布上位机、连接板卡、执行OTA。使用旧BQ C031的IAP不能替代D015 IAP。

## 验证

- 客户WPF、内部WPF、CLI构建成功；客户版有基线CS0649（诊断录制字段），其余无警告。补齐两版RefreshIdentityAsync的取消令牌参数，修复基线SN刷新调用不能编译的问题。
- test-afe-write-compatibility：旧SH/DVC与新D015的量化、使能合并、短路候选及未编辑字段保留。
- test-afe-fragments、test-current-calibration、test-protection-batch、test-ota-protocol回归通过。
- test-d015-interop.ps1：分别加载实际客户/内部程序集，通过进程传输调用D015当前C模块的--pc-server测试入口。模拟AFE/Flash，拆分接收通知、MTU23请求，覆盖身份/实时/61001、软件保护写回、硬件使能和分片提交、校准新样本公式、SN及参数采集。它不是串口时序、电流精度或物理保护验收。

所有构建和测试产物写用户CodexTemp，工程内无新临时目录。要复现交叉测试，先在D015仓库运行tests/run_firmware.py，再传CustomerAssembly、InternalAssembly、FirmwareHarness及EvidenceDirectory给test-d015-interop.ps1。

配套固件已提交为382ba5f（codex/d015-f030-3520），APP 2.2.0；交叉测试使用该提交的真实C协议/配置/保护/校准模块。
