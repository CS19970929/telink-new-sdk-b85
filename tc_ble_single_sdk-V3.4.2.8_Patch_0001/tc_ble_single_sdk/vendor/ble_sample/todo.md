
一个接口怎么实现一线通发送和串口modbus从机功能，以上是电路和一线通内容

owc是一线通接口，和一线通从设备通信，同时owc_tx、owc_rx直接通过端子可以作为串口，我是需要分时复用，或者说根据客户使用接口软件自动判断使用哪种通讯，软件该如何处理，能够区分使用哪种通讯


todo 
- 循环次数 soh
- 上位机增加 调试，显示状态，printf？？？
-


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