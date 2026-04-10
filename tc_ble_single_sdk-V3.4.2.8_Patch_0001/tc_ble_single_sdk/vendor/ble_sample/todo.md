
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