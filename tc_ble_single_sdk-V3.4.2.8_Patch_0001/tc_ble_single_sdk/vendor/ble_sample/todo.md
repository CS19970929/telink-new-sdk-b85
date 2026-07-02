soc 融合测试？

能否合并目前校准逻辑，减少代码量，同时方便后续优化、调整
soc模块没用的宏、变量可以全部删除
soc
增加不同放电电流时，在安时积分前提下，同时ocv进行比较修正

d11 d004 32002278
c700 d004 32002279

一个接口怎么实现一线通发送和串口modbus从机功能，以上是电路和一线通内容

owc是一线通接口，和一线通从设备通信，同时owc_tx、owc_rx直接通过端子可以作为串口，我是需要分时复用，或者说根据客户使用接口软件自动判断使用哪种通讯，软件该如何处理，能够区分使用哪种通讯

todo 
- 循环次数 soh
- 上位机增加 调试，显示状态，printf？？？
-

//todo todo
测试、补偿低功耗下，休眠计时不准，具体多少，其他方案？？？
soc ocv校准？？？
flash存储是否会有异常，异常后会怎么样，比如断电是否会导致存储参数异常
ota分区是怎么样的？什么时候会影响
整个项目flash是怎样分区的
!!!分析各个flash模块磨损均衡怎么样，是否会有风险
！！！可以重置runtime模块时间为初始时间吗.是否可以合并到其他模块，例如kv，kv分区设置是否太小，分析flash寿命，是否需要加大,分析各模块在异常情况下风险，例如极端情况下异常断电是否会导致flash存储异常，出现严重问题，看门狗时间，ota flash区域有什么决定,如果各模块flash初始化异常是否会有问题，例如旧版本架构升级当前flash框架



mac qt上位机实现，跨平台，界面风格

todo 
仔细研究下
i2c_write_series
i2c_read_series
补充crc校验部分


flash方便使用的框架，log、param、sci comm


避免下错程序？13s下错10s，容易烧fuse
1、afe通讯异常
关ctlc，理论上充、放电mos关闭，如果mos坏了？adc检测总压 > 4.28 * snum

2、正常通讯，单节 > 4.25,关闭充电mos，如果检测到充电电流或者 adc检测总压 > 4.28 * snum

adc单独检测温度？防止误触发？？？
温度


1、mos温度保护？？？，结合软件保护
2、



7days enter fac mode,协议进入 fac mode接口，并测试，如何保证7 day准确，测试主循环cnt计时

设计在telink上实现一个功能：mcu累计运行时间小于7天时，bms进入工厂测试模式，大于7天后，进入正常模式，给出方案和具体实现逻辑和代码



todo 测试过放后，休眠时间，准确性


- D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\doc目录下有flash模块相关文档，review flash各模块代码，kv分区设置是否太小，分析flash寿命，是否需要加大,分析各模块在异常情况下风险，例如极端情况下异常断电是否会导致flash存储异常，出现严重问题，flash分区是否合理，是否会影响ota升级
- 如何重置runtime模块时间为初始时间吗吗，是否可以合并到其他模块，例如kv
- 梳理低功耗策略是否合理，低功耗下时间是否准确
- 梳理soc模块逻辑，增加、优化soc策略

梳理mac上位机和qt上位机的共性，逻辑、架构，主要是ble相关，我不熟悉mac、qt和ble相关，输出文档，还有为什么是python不是c++
soc_kv_store_update_and_log_if_changed中的soc_kv_store_write_all会检查缓存，缓存一样时是否还会写flash，还是说每5s必定写flash，我想soc、dsg、cycle任意发生改变时就写flash，理论上这三个变量变化不会很快，应该是大于5s的

家里的codex app和公司的codex app记录、记忆可以同步吗？


todo 分流器、过流通过宏控制

我后续用什么开发qt，如何优化功能

APP_BATT_CHECK_ENABLE宏的作用
测试soc逻辑

3000mv下，进瞬间待机，记录sleep了？？？

 整理完整逻辑、寄存器、协议、等等输出文档，方便后续做成模板，ai接管mcu和上位机、app

 测试下soc存储逻辑


 测试各种休眠时间

//****************************//
d002    (32002276)    c11 13s20A保护参数
过充：4250   恢复：4150，延时1s
过放：2750  恢复：3000，延时1s
充电过温：55    恢复45
充电低温：-7    恢复0
放电过温：75   恢复60
放电低温：-20  恢复-10
放电过流1级：40A    延时：1s
放电过流2级：60A   延时：600ms
充电过流：20A   延时100ms
短路：200A，延时256us

//****************************//
d004    (32002278)    D11 10s15A保护参数
过充：4250   恢复：4150，延时1s
过放：2750  恢复：3000，延时1s
充电过温：55    恢复45
充电低温：-7    恢复0
放电过温：75   恢复60
放电低温：-20  恢复-10
放电过流1级：30A    延时：1s
放电过流2级：50A   延时：600ms
充电过流：20A   延时100ms
短路：200A，延时256us

//****************************//
d004    (32002279)    C700 10s15A保护参数
过充：4250   恢复：4150，延时1s
过放：2750  恢复：3000，延时1s
充电过温：55    恢复45
充电低温：-7    恢复0
放电过温：75   恢复60
放电低温：-20  恢复-10
放电过流1级：30A    延时：1s
放电过流2级：50A   延时：600ms
充电过流：20A   延时100ms
短路：200A，延时256us


输出这套充电器升级上位机的核心逻辑、软件架构，语言、架构、编译器等等，方便我后续研究、学习、完善

查看ble_master_kma_dongle工程，实现自己的app上位机ota和串口升级master
codex分析sdk，分析每个例子，输出文档


测试低功耗下soc校准
测试低功耗下，时间是否准确


C11_AND_C11pro这个板由于硬件问题，电流不准，目前观察到大概有1.2倍的误差，导致soc不准的原因之一，然后现在放电电流等比例除以了1.2,现在提供给你这份放电表格，根据这份表格继续优化soc准确度，这份表格是10A放电的记录，是之前的老代码电流显示12A多,先分析这份文档然后给出优化方案

电池容量是确定的，肯定不能改电池容量啊，增加策略，保证soc准确、收敛和用户体验

测试之前屏蔽了CorrectionTerminal_CV，会导致不会卡1和98的soc

静置不允许向上校准
修改soc中安时积分逻辑，只要有对应电流就计算soc，而不需要过滤、延时，计算频率为200ms，目前积分计算应该有问题，200ms调用，实际按1s来计算的，仔细梳理soc模块并优化，保证soc用户体验性，用于ebike


#define CapacityFactory (104) 容量代表10.4Ah，上次程序改完后，对外显示容量不对了，显示的104Ah

测试ota升级，5V充电改为上拉1M？

把afe电流校准逻辑移植到D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)项目中，sh367309_datadeal文件中有电流计算


阅读D:\telink\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)\tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\docs 目录下文档，项目主目录是ble_sample目录，是一个bms mcu项目，现在需要实现配套的win蓝牙上位机和安卓蓝牙app


看一下时间是否有问题，深度休眠补偿有问题？之前解决过，没记录？


怎么实现软件静态分析和输出静态分析报告，先给出方案

telink工具链目录是在C:\TelinkSDK\opt\tc32\bin 目录，你看下是不是，如果是，直接编译一遍


你现在在一个 Telink TLSR8251 / TLSR825x BLE 项目源码中工作。请先不要直接改代码，先完成源码分析，然后实现一个 PC 端蓝牙 OTA 升级上位机。

目标：
1. 基于当前项目源码确认是否已经启用 Telink BLE OTA Server。
2. 找出 OTA GATT Service / Characteristic / Handle / UUID / Attribute Table 定义。
3. 找出设备端 OTA 相关源码、宏和回调，例如：
   - otaWrite
   - my_OtaServiceUUID
   - my_OtaUUID
   - TELINK_SPP_DATA_OTA
   - OTA_CMD_OUT_DP_H
   - blc_ota_registerOtaStartCmdCb
   - blc_ota_registerOtaResultIndicationCb
   - blc_ota_setOtaProcessTimeout
   - blc_ota_setOtaDataPacketTimeout
   - blt_ota_procTimeout
4. 判断项目使用 Legacy OTA 还是 Extend OTA / Big PDU / Secure Boot OTA。
5. 在 tools/telink_ota_pc/ 下实现一个 Python + Bleak 的 PC 上位机，第一版优先支持 Legacy OTA。

Telink Legacy OTA 协议要求：
1. OTA 命令格式：
   - Opcode 2 bytes，小端
   - CMD_OTA_START = 0xFF01
   - CMD_OTA_END = 0xFF02
   - CMD_OTA_RESULT = 0xFF06
2. OTA 数据包格式：
   - adr_index: 2 bytes，小端
   - data: 16 bytes
   - crc16: 2 bytes，小端
   - 总长度 20 bytes
3. bin 文件处理：
   - 读取 .bin 文件
   - 如果长度不是 16 字节对齐，末尾补 0xFF
   - 每 16 字节生成一个 OTA data packet
   - adr_index 从 0 开始递增，必须连续
4. CRC：
   - 根据项目 SDK 或 Telink 文档中的 CRC16 算法实现
   - 优先从当前项目源码中复用/移植 CRC16 算法，不能随便猜
   - 为 CRC16 和 OTA packet 生成写单元测试
5. OTA_END：
   - 格式为：0xFF02 + adr_index_max + bitwise_not(adr_index_max)
   - 小端
   - OTA_END 不带 CRC16
6. 发送方式：
   - 优先使用 BLE Write Without Response
   - 支持通过参数切换 Write With Response，便于调试
   - 每发送若干包允许插入小延时，参数可配置
7. 结果处理：
   - 订阅 OTA result notify
   - 解析 CMD_OTA_RESULT
   - 打印成功或失败原因
   - 失败时停止发送并输出错误码说明
8. 命令行：
   - python telink_ota.py scan
   - python telink_ota.py list-services --name <device_name>
   - python telink_ota.py upgrade --name <device_name> --bin <firmware.bin>
   - python telink_ota.py upgrade --address <ble_address> --bin <firmware.bin>
   - 参数支持：--ota-service-uuid、--ota-char-uuid、--pdu-size、--delay-ms、--timeout、--write-response
9. 工程要求：
   - 代码清晰、模块化
   - 不要改动设备端 OTA boot/flash 逻辑，除非源码分析确认项目缺少必要 OTA 配置
   - 如果设备端缺少 OTA Attribute Table 或回调，只给出最小补丁，并说明风险
   - 输出 README.md，写清楚安装、扫描、升级、失败码、注意事项
   - 输出一份 docs/telink_ota_analysis.md，记录从当前项目源码中找到的 OTA UUID、handle、宏、协议模式、固件分区、启动地址等信息

实现步骤：
第一步：只做源码分析，输出 plan。
第二步：实现 Python CLI 上位机。
第三步：补充单元测试。
第四步：补充 README 和分析文档。
第五步：给出实际使用命令和注意事项。

注意：
- 不要凭空假设 OTA UUID，必须优先从项目源码 Attribute Table 中提取。
- 如果找不到 UUID，则实现 list-services 功能，让用户先连接设备枚举 GATT 服务。
- Telink OTA 对包序号连续性很敏感，不能并发乱序发送。
- OTA 过程中建议关闭设备低功耗，若设备端已有 OTA start callback，应确认 callback 中是否关闭 PM。
- 上位机必须显示进度，并能在失败时输出 OTA result 错误码。