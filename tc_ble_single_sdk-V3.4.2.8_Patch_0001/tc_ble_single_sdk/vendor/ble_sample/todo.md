
一个接口怎么实现一线通发送和串口modbus从机功能，以上是电路和一线通内容

owc是一线通接口，和一线通从设备通信，同时owc_tx、owc_rx直接通过端子可以作为串口，我是需要分时复用，或者说根据客户使用接口软件自动判断使用哪种通讯，软件该如何处理，能够区分使用哪种通讯


todo 
- 循环次数 soh
- 上位机增加 调试，显示状态，printf？？？

我会提供codex easyflash源码，需要让他改造成适合telink使用的，同时code review并精简不必要的功能，越精简、越稳定越好


在telink上设计一套简单、稳定的nvm框架，不需要考虑存储不同类型，仅仅只需要存储soc、循环次数、soh等等不会超过u16的，可以扩展增加需要存储变量，但不会无限制增加，还有一些bms事件，事件拼接相对上一个事件相对当前时间间隔，也需要保证不超过u16，这样保证框架的简洁和稳定，给出合适的方案和具体实现

通过u16表示存储key，拼接u16的val，来表示不同数据和事件


todo
- gc、gc逻辑
- 回读校验
- 冗余设计
- 必须page写？
- 几个sector，sector切换逻辑，gc逻辑

code review
task文件


soc(soc_ocv,soc_disp,Ah_soc)
- 1h ocv   soc_ocv
- kalman filter? 
- soc_disp = func(soc_ocv, Ah_soc)
- 