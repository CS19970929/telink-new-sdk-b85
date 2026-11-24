# C077 双309完整硬保护编辑

内部版入口：连接C077→C077参数→编辑双309硬件保护（Flash / EEPROM）。每片独立编辑物理电压/恢复/延时、OCC/OCD1/OCD2/短路、温度与恢复、PF/低压禁充、MOS/检测/恢复/负载释放以及7个功能位。生产固件通过 `C077_PRODUCTION_SERIES_COUNT` 选择 5～16 串，默认 16。上位机只读显示串数，参数包的串数须与连接设备一致才能导入软件保护。CTL 双路禁止、MCU 均衡/预充和 AFE1 单 CADC 由板级定义。

编辑器先确认MCU参数装载ready；未就绪时拒绝把RAM默认值显示为已保存Flash配置。读取后表中分别显示 Flash 保存值、草稿和芯片诊断值。显式读取且两片核验成功才标本次确认。其它情况显示诊断缓存及错误、差异位图。温度显示 ℃，原始协议和具名 JSON 为 0.1 ℃。

先关闭输出/均衡，再保存所选片Flash；保存不写EEPROM。明确选择下发所选片或双片才编程。未保存草稿拒绝下发。编辑器发送命令前复核所选片的 Flash 保存档案，发现与窗口基准不同就要求重读。旧协议在复核与命令之间仍可能被另一端口更新。失败/超时后先读取当前结果，再明确决定是否重试。单片完成不代表另一片已配置，整包资格仍要求两片均有效。窗口保留MOS/均衡关闭许可，恢复许可走主界面。

软件65字保护在原页面编辑；软件过流Third不再自动修改硬保护。旧短路230A/230B作为兼容只读字段。客户版不显示硬保护编辑页；共享BmsClient/CLI使用同一校验和完成判据。

```powershell
bms-cli afe309 hardware-get --serial COMx --output C077_hardware.json --json
bms-cli afe309 hardware-save --serial COMx --chip 1 --input C077_hardware.json --yes --json
bms-cli afe309 hardware-save --serial COMx --chip 2 --input C077_hardware.json --yes --json
bms-cli afe309 program --serial COMx --chip 1 --yes --json
bms-cli afe309 program --serial COMx --chip 2 --yes --json
bms-cli afe309 program --serial COMx --chip both --yes --json
bms-cli afe309 verify --serial COMx --json
```

hardware-get导出Product=C077、Schema=1、Chips两组32个具名字段；硬件保存一次提交一片，禁止不完整/陌生字段、非法档位、恢复窗口错误、陈旧Flash覆盖和输出未关。明确下发按 Flash 已保存值操作。verify 通过 C087 核对 EEPROM，只触发读取。BLE 整组保存需要 MTU ≥ 76，直连串口沿用完整帧。

新增能力header为2B00 schema1，Flash字段2B10/2B30各32字，期望/差异/字节诊断2B80/2B90各16字；2A40/64 提供 EEPROM 诊断及服务结果，客户端支持 schema 2/3。C071/C072/C077分别下发AFE1/2/双片，C087只读核对。旧固件无能力时新编辑器拒绝写入。

当前模板使用 Flash format 7，C077 CONFIG 负载 540 字节。部署前按实际固件版本核对存储迁移与工厂复位修订。硬保护 Flash 保存、EEPROM 编程和现场升级分别执行。

当前模板周期 RTC 保留两片采样。保护性深睡按板级流程请求两片 CONF.SLEEP，SHIP 保持释放，唤醒后须重新核对采样及配置。

验证：共享C#分别连接真实C参数/Flash引擎和C驱动/工厂状态机，覆盖独立默认与具名JSON、Flash持久化、保存不编程、分别下发、失败定位及明确重试；C077/C060兼容回归、客户WPF/内部WPF/CLI编译。GPIO、VPRO/VGS、真实保护、功耗、物理掉电及实际串口升级仍需实板验收。
