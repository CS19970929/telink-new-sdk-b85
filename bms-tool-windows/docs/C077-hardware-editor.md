# C077 双309完整硬保护编辑

内部版入口：连接C077→C077参数→编辑双309硬件保护（Flash / EEPROM）。每片独立编辑物理电压/恢复/延时、OCC/OCD1/OCD2/短路、温度与恢复、PF/低压禁充、MOS/检测/恢复/负载释放以及7个功能位。固定16串、CTL双路禁止、MCU均衡/预充和AFE1单CADC为板级合同，只读。

编辑器先确认MCU参数装载ready；未就绪时拒绝把RAM默认值显示为已保存Flash配置。读取后表中分别显示Flash保存、待保存、AFE实际读回；温度显示℃，原始协议和具名JSON为0.1℃。另一页逐项显示25字节期望/实际，失败时显示片号、寄存器、期望/实读字节及差异位图；IO/CRC失败的实读不可作有效证据。

先关闭输出/均衡，再保存所选片Flash；保存不写EEPROM。明确选择下发所选片或双片才编程。未保存草稿拒绝下发；失败/超时不自动重发，只读当前结果后明确重试。单片完成不代表另一片已配置，整包资格仍要求两片均有效。窗口保留MOS/均衡关闭许可，恢复许可走主界面。

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

hardware-get导出Product=C077、Schema=1、Chips两组32个具名字段；硬件保存一次提交一片，禁止不完整/陌生字段、非法档位、恢复窗口错误、陈旧Flash覆盖和输出未关。明确下发只使用Flash已保存值。verify只读核对EEPROM，不编程。

新增能力header为2B00 schema1，Flash字段2B10/2B30各32字，期望/差异/字节诊断2B80/2B90各16字；实际EEPROM和原schema2仍由2A40/64提供。C071/C072/C077分别下发AFE1/2/双片，C087只读核对。旧固件无能力时新编辑器拒绝写入。

固件格式6/752B兼容格式5/624B前缀；升级只补新增硬保护字段，保留身份/校准/软件参数/SOC/日志及原PENDING续办。硬保护Flash保存、EEPROM编程和现场.c077升级分别进行。默认具名JSON及Keil表达式见固件docs/c077-hardware-editor.md和docs/C077_HARDWARE_DEFAULTS.json。

MCU深睡已取消AFE SHIP/SLEEP请求，309保持测量与硬保护，按键/PA0唤醒保留。旧工程调用AFE_Sleep的事实不等于通过SHIP仓运；本轮按用户要求不主动仓运。

验证：共享C#分别连接真实C参数/Flash引擎和C驱动/工厂状态机，覆盖独立默认与具名JSON、Flash持久化、保存不编程、分别下发、失败定位及明确重试；C077/C060兼容回归、客户WPF/内部WPF/CLI编译。GPIO、VPRO/VGS、真实保护、功耗、物理掉电及实际串口升级仍需实板验收。
