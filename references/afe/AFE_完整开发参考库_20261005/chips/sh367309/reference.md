# SH367309 V1.1 中文实施参考

## 0. 使用范围、证据和文件

本参考只依据包内 **SH367309.pdf，V1.1（2019年8月）**。PDF共89页；正文物理PDF页1-86与印刷页码相同，PDF87-89为目录。文内“pN”均指**从1开始的PDF物理页**，引用同时给出原文件名。不得把其他SH36730x型号、其他修订或项目经验补成此芯片事实。此参考用于检索、原理图审查和驱动设计，不构成完成硬件安全验证的声明。

- [原始规格书](../../sources/SH367309.pdf)
- [机器可读寄存器](registers.json)：77个公开字节地址、逐位原始标签、解码字段、权限、复位值和逐项来源；未给出的值为null
- [来源保真寄存器/电气表文本](raw_register_extracts.md)：保留原表文字和已知排版错误，不代替规范化解释
- [全89页文字检索层](../../extracted/SH367309_V1_1/full_by_page.md)：图形、公式和接线仍以PDF为准
- [逐页覆盖清单](coverage.json)、[核验与不确定性](validation.md)

### 0.1 必须先记住的差异

1. **正常工作VBAT上限65V；70V是极限，不是连续工作额定值。** [SH367309.pdf p75](../../sources/SH367309.pdf#page=75)、[p76](../../sources/SH367309.pdf#page=76)
2. TWI为**7-bit地址0x1A**；总线写地址字节0x34、读地址字节0x35。读请求有**长度字节**，不等同普通“先写寄存器地址再读”的I²C驱动。 [SH367309.pdf p49](../../sources/SH367309.pdf#page=49)、[p50](../../sources/SH367309.pdf#page=50)、[p51](../../sources/SH367309.pdf#page=51)
3. **BFLAG1及BFLAG2非ADC标志是W0C；BFLAG2的CADC_FLG/VADC_FLG是读清。不是W1C。** [SH367309.pdf p73](../../sources/SH367309.pdf#page=73)、[p74](../../sources/SH367309.pdf#page=74)
4. MOS控制位为1只表示交给硬件逻辑，**不保证MOS已开启**；看BSTATUS3的实际状态。 [SH367309.pdf p35](../../sources/SH367309.pdf#page=35)、[p69](../../sources/SH367309.pdf#page=69)、[p72](../../sources/SH367309.pdf#page=72)
5. VADC与CADC电流比例不同；CUR用26837，CADCD用21470；寄存器符号位1表示放电。 [SH367309.pdf p45](../../sources/SH367309.pdf#page=45)、[p46](../../sources/SH367309.pdf#page=46)、[p47](../../sources/SH367309.pdf#page=47)
6. 二次过充/断线故障会停止两个ADC但保留旧测量值；WarmUp结束后的**8分钟不检测这两种故障**。 [SH367309.pdf p12](../../sources/SH367309.pdf#page=12)、[p28](../../sources/SH367309.pdf#page=28)、[p30](../../sources/SH367309.pdf#page=30)
7. EEPROM只在VPRO烧写模式可写，字节间隔35ms，写后需软件复位生效；不能把EEPROM当高频状态存储。原文编程/擦除次数写“≤100次”，不是可无限使用。 [SH367309.pdf p1](../../sources/SH367309.pdf#page=1)、[p12](../../sources/SH367309.pdf#page=12)、[p50](../../sources/SH367309.pdf#page=50)、[p51](../../sources/SH367309.pdf#page=51)

## 1. 器件定位、功能与版本

SH367309为5-16串锂电池BMS前端。保护模式可独立工作；采集模式与MCU配合且硬件保护仍生效。内置电压、温度、电流保护，二次过充与断线保护，内部平衡开关，低压禁止充电，小电流检测，看门狗，EEPROM与支持CRC8的TWI。VADC为13-bit Σ-Δ，名义10Hz，含16路电压、3路温度、1路电流；独立CADC为16-bit Σ-Δ、名义4Hz电流通道。支持乱序上下电是功能声明，不能推导为所有引脚任意过压均安全。 [SH367309.pdf p1](../../sources/SH367309.pdf#page=1)、[p2](../../sources/SH367309.pdf#page=2)、[p40](../../sources/SH367309.pdf#page=40)、[p47](../../sources/SH367309.pdf#page=47)

内部方框图按输入采集、保护判决、状态/标志、MOS驱动、电源、EEPROM和TWI组织；保护模式不开放正常TWI，烧写模式另有入口。文档未列该型号的其他功能变体。订购项为SH367309U/048UR、TQFP48L、Tray、最小起订量2.5K。V1.1相对V1.0只在更改记录中声明“更新参考原理图CHGD端口器件”；不把其解释为硅片功能更改。 [SH367309.pdf p2](../../sources/SH367309.pdf#page=2)、[p10](../../sources/SH367309.pdf#page=10)、[p85](../../sources/SH367309.pdf#page=85)、[p86](../../sources/SH367309.pdf#page=86)

## 2. 引脚、电源与电气边界

### 2.1 全部引脚

下表所有编号及功能来自 [SH367309.pdf p3管脚图](../../sources/SH367309.pdf#page=3)、[p4表1](../../sources/SH367309.pdf#page=4)。pin1为VC17，pin17为VC1，编号方向不能按CELL编号正序推定。

|引脚|名字|角色/注意点|
|---|---|---|
|1-17|VC17…VC1|相邻电芯抽头；VC17最高电芯正端，VC1最低电芯负端|
|18|RS1|电流采样负输入|
|19|RS2|电流采样正输入；放电保护比较RS2-RS1|
|20/21/22|T1/T2/T3|外接热敏电阻|
|23/24/39|NC|不连接|
|25|ALARM|开漏事件输出|
|26|SCL|TWI时钟；支持从机拉低时钟延长|
|27|SDA|TWI开漏数据|
|28|VCC|LDO1、3.3V输出|
|29|VSS|芯片供电负端|
|30/31/32|CAPS/CAPN/CAPP|DCDC转换控制节点|
|33|V11|LDO2、11V输出|
|34|DSG|放电NMOS驱动|
|35|PCHG|预充NMOS驱动，关闭时高阻|
|36|CHG|充电NMOS驱动，关闭时高阻|
|37/38|LDO_P/LDO_O|LDO3供电正端/稳压输出|
|40|VPRO|EEPROM烧写电源|
|41|DSGD|负载检测|
|42|CHGD|充电器检测|
|43|SHIP|低电平进入仓运|
|44|LDO_EN|LDO3使能，也影响上电待激活流程|
|45|MODE|高电平采集、低电平保护|
|46|CTL|按CTLC配置优先关闭相应MOS|
|47|PF|二次过充/断线保护输出|
|48|VBAT|芯片供电正端|

### 2.2 正常工作、功耗与逻辑电平

- 常温VBAT工作8.5-65V，测试条件“无保护、LDO1/LDO2无负载”。工作环境-40至85°C。**不把65-70V归为正常工作区。** [SH367309.pdf p75](../../sources/SH367309.pdf#page=75)、[p76](../../sources/SH367309.pdf#page=76)
- 常温模式电流典型/最大：采集70/105µA、保护40/55µA、仓运1.5/2µA、IDLE40/50µA、Powerdown3/5µA、SLEEP35/45µA。测试VBAT=60V，无保护/平衡，LDO无负载、CHG/DSG悬空，在VSS测量；这些数不含系统MCU和外部电阻电流。全温Powerdown最大改为6µA，其他模式同表。 [SH367309.pdf p76](../../sources/SH367309.pdf#page=76)、[p82](../../sources/SH367309.pdf#page=82)
- VC1-VC16漏电最大1µA，VC17常温最大1.5µA、全温最大2µA，条件相邻电芯电压3.8V。 [SH367309.pdf p76](../../sources/SH367309.pdf#page=76)、[p82](../../sources/SH367309.pdf#page=82)
- CTL高电平要求VCC-0.3V至VBAT；MODE、SHIP、LDO_EN高电平要求VBAT-0.3V至VBAT；四者低电平最大0.3V。**MODE/SHIP/LDO_EN不能直接当普通3.3V输入。** [SH367309.pdf p76](../../sources/SH367309.pdf#page=76)
- SDA/SCL VIH为2-5V，VIL最大0.6V；VOL最大0.4V，分别在SDA/SCL灌3mA、PF灌100µA、ALARM灌1mA条件。 [SH367309.pdf p77](../../sources/SH367309.pdf#page=77)

### 2.3 LDO与MOS驱动电气

|模块/参数|min / typ / max 与条件|来源|
|---|---|---|
|VCC/LDO1|3.1/3.3/3.5V；13≤VBAT≤60V时Iload≤10mA；8.5≤VBAT<13V时≤2mA|[SH367309.pdf p77](../../sources/SH367309.pdf#page=77)|
|V11/LDO2|8.5/11/13V，VBAT≥13V、Iload=5mA；VBAT=10V、5mA时最小8V|[SH367309.pdf p77](../../sources/SH367309.pdf#page=77)|
|LDO3输出|3.1/3.3/3.5V；线性调整典型10/最大50mV（VBAT12-60V、25mA）；负载调整典型30/最大100mV（VBAT50V、0.1-25mA）|[SH367309.pdf p77](../../sources/SH367309.pdf#page=77)|
|LDO3限流|50/75/100mA，典型检测延时2ms；限流时不保证输出电压，触发LOAD_FLG及ALARM|[SH367309.pdf p39](../../sources/SH367309.pdf#page=39)、[p73](../../sources/SH367309.pdf#page=73)、[p77](../../sources/SH367309.pdf#page=77)|
|CHG/DSG/PCHG高电平|8/11/13V（VBAT≥13V，外接1MΩ到地）；VBAT=8.5V且LDO1/2空载时最小6.5V|[SH367309.pdf p78](../../sources/SH367309.pdf#page=78)|
|DSG低电平|最大1V，IOL=0.5mA；CHG/PCHG关闭为高阻|[SH367309.pdf p35](../../sources/SH367309.pdf#page=35)、[p78](../../sources/SH367309.pdf#page=78)|
|CHG上升|typ100/max200µs，50nF，10%-90%|[SH367309.pdf p78](../../sources/SH367309.pdf#page=78)|
|PCHG上升|typ500/max1000µs，4700pF，10%-90%|[SH367309.pdf p78](../../sources/SH367309.pdf#page=78)|
|DSG下降/上升|typ150/max400µs；typ200/max600µs；50nF、串1kΩ，90%-10%/10%-90%|[SH367309.pdf p78](../../sources/SH367309.pdf#page=78)|

第一页“3.3V（25mA@MAX）”不能套到所有LDO；必须按上述具体输出与条件选电源。 [SH367309.pdf p1](../../sources/SH367309.pdf#page=1)、[p77](../../sources/SH367309.pdf#page=77)

### 2.4 绝对极限（非正常工作承诺）

全部来自 [SH367309.pdf p75表12](../../sources/SH367309.pdf#page=75)，超过可永久损坏。未列出的瞬态、注入电流和ESD等级不能自填。

|节点|极限电压范围|
|---|---|
|VBAT|VSS-0.3至70V|
|LDO_P|VSS-0.3至13V|
|RS1|VSS-0.3至VSS+0.3V|
|VC1|-0.3至5V|
|VC2-VC17|VSS-0.3至70V|
|RS2|VSS-1V至VCC+0.3V|
|CHGD/DSGD|VBAT-70至VBAT+0.3V|
|CTL/SHIP/MODE/LDO_EN|VSS-0.3至VBAT+0.3V|
|T1-T3|VSS-0.3至VCC+0.3V|
|SCL/SDA|VSS-0.3至5.5V|
|VPRO|源表原样：VSS-0.3至VPRO+0.3V；自引用上限含糊，不能推定绝对额定值|
|CHG/PCHG|VBAT-70至V11+0.3V|
|DSG|VSS-0.3至V11+0.3V|
|PF|VSS-0.3至VBAT+0.3V|
|V11/CAPP|VSS-0.3至V11+0.3V|
|CAPS/CAPN、VCC/ALARM、LDO_O|VSS-0.3至5.5V|

工作温度-40至85°C；存储-40至125°C。VPRO烧写工作电压另见7.7/8/8.3V，不能把它自动补成极限表的缺失绝对值。 [SH367309.pdf p75](../../sources/SH367309.pdf#page=75)、[p77](../../sources/SH367309.pdf#page=77)

## 3. 模式、复位、启动与低功耗

### 3.1 模式/功能矩阵

原矩阵见 [SH367309.pdf p10表2](../../sources/SH367309.pdf#page=10)。这里“开”表示该模式保留；“可开”表示采集模式默认关闭、由寄存器启用。

|状态|电压/温度/PF保护|电流保护|平衡|VADC/CADC|TWI|充电器检测|唤醒监测|WDT|电源|
|---|---|---|---|---|---|---|---|---|---|
|保护正常|开|开|开|VADC开/CADC关|关|开|无STA|关|LDO1/2开，LDO3关|
|保护Powerdown|关|关|关|关|关|开|充电器|关|全关|
|采集正常|开|开|开|VADC开/CADC可开|开|开|正常运行|可开|LDO1/2开，LDO3由LDO_EN|
|采集IDLE|关|开|关|关|关|开|STA及充/放电电流|保持进入前状态|LDO1/2开，LDO3由LDO_EN|
|采集SLEEP|关|关|关|关|关|开|STA及充电器|关|LDO1/2开，LDO3由LDO_EN|
|仓运|关|关|关|关|关|关|仅SHIP退出|关|全关|

模式引脚：MODE低选保护，高选采集；SHIP低进入仓运，只有SHIP高退出且硬件复位；仓运接充电器不会醒。VPRO施加烧写电压并等待10ms进入烧写，关闭充放电MOS和保护。 [SH367309.pdf p10](../../sources/SH367309.pdf#page=10)、[p11](../../sources/SH367309.pdf#page=11)、[p12](../../sources/SH367309.pdf#page=12)

### 3.2 启动与复位

硬件复位包含上电、LVR、退出仓运、退出Powerdown。上电/退仓运/退Powerdown后，若LDO_EN高先待激活：MOS和TWI关闭，需CHGD低于VCHGD1并保持tD3后进入WarmUp；若LDO_EN低直接WarmUp。LVR直接WarmUp。WarmUp最大250ms，期间MOS关闭、TWI禁止；软件复位也进入WarmUp。WarmUp结束后8分钟屏蔽断线与二次过充检测。 [SH367309.pdf p12](../../sources/SH367309.pdf#page=12)、[p76](../../sources/SH367309.pdf#page=76)、[p80](../../sources/SH367309.pdf#page=80)

RST_FLG上电/复位为1，需MCU写0清；不能把最早读到的全零测量当有效电池值。CONF复位0x70意味着PCHMOS/DSGMOS/CHGMOS交硬件控制，而CADC、WDT关闭。 [SH367309.pdf p40](../../sources/SH367309.pdf#page=40)、[p47](../../sources/SH367309.pdf#page=47)、[p69](../../sources/SH367309.pdf#page=69)、[p74](../../sources/SH367309.pdf#page=74)

### 3.3 IDLE

进入同时要求：无保护且无保护延时、VCD2<RS2-RS1<VCD1、CONF.IDLE=1。阻止进入的保护包括OV/UV/PF/SC/OCC/OCD1/OCD2、四种温度、预充与低压禁止充电；**不包括WDT或CTL关MOS**。进入关闭VADC/CADC/TWI、电压/温度保护并清BALANCEH/L，开启STA与小电流检测。STA或电流超过VCD范围持续tCD可醒，电流唤醒发ALARM。 [SH367309.pdf p11](../../sources/SH367309.pdf#page=11)

VCD1 min/typ/max=0.5/1.4/2.6mV；VCD2=-2.6/-1.4/-0.5mV；tCD=10/15/20ms。**这是低功耗唤醒阈值，不是CHS设置的充放电状态阈值。** [SH367309.pdf p80](../../sources/SH367309.pdf#page=80)、[p83](../../sources/SH367309.pdf#page=83)

### 3.4 SLEEP与STA

CONF.SLEEP=1请求SLEEP。进入关所有MOS、VADC/CADC/TWI/WDT及所有保护，清平衡，保留STA与充电器检测。STA或CHGD<VCHGD1持续tD3退出；充电器唤醒发ALARM。原文p11说“已连接充电器时先进入再退出”，p69说“不进入且自动清位”：**能确定不会持续SLEEP，瞬态行为有描述冲突，应板测**。 [SH367309.pdf p11](../../sources/SH367309.pdf#page=11)、[p69](../../sources/SH367309.pdf#page=69)

STA检测时芯片拉低SCL、退出低功耗、再释放SCL，并置WAKE_FLG。主机必须支持clock stretching，不能以发送START即假设立刻能在固定时刻完成。WAKE_FLG详表仅举电流/充电器唤醒，p39明确STA也置位；两处一起保留。 [SH367309.pdf p39](../../sources/SH367309.pdf#page=39)、[p74](../../sources/SH367309.pdf#page=74)

### 3.5 Powerdown

仅保护模式：任一电芯低于VPD持续TPD，关闭充放电MOS并进入；已接充电器不能进入；CHGD<VCHGD3退出且硬件复位。TPD典型10s，先发生UV才检测Powerdown。VPD表的min/typ/max原样为VUV-150/VUV-200/VUV-250mV，数值排序反常；不能擅自交换“min/max”。典型可描述为VUV低200mV，保证边界需厂家澄清。 [SH367309.pdf p11](../../sources/SH367309.pdf#page=11)、[p76](../../sources/SH367309.pdf#page=76)

## 4. TWI传输、CRC与持久化

### 4.1 基础总线和时序

只支持从机；地址7-bit 0x1A，总线字节0x34/0x35。MSB先传，每个字节加ACK/NACK，主机提供时钟及START/STOP；SCL高时SDA下降为START，上升为STOP。从机可延长SCL低电平。主机NACK后，若继续读数据芯片返回全1，等待STOP或重复START。 [SH367309.pdf p49](../../sources/SH367309.pdf#page=49)、[p50](../../sources/SH367309.pdf#page=50)

|参数|要求|来源|
|---|---|---|
|fTWI|10-100kHz|[SH367309.pdf p81](../../sources/SH367309.pdf#page=81)|
|tBUF / tLOW|min4.7µs / min4.7µs|同上|
|tHIGH|min4.0µs、max50µs|同上|
|数据保持tHD:DAT / 建立tSU:DAT|min300ns / min250ns|同上|
|START保持tHD:STA / 建立tSU:STA|min4.0µs / min4.7µs|同上|
|STOP建立tSU:STO|min4.0µs|同上|
|上升tR / 下降tF|max1000ns / max300ns|同上|
|低电平超时tTIMEOUT|typ25ms；未给min/max|同上|
|RC精度|全温最大±10%|同上|

### 4.2 精确帧

以下ACK由接收方产生；S为START、Sr为重复START、P为STOP。地址、寄存器、长度、DATA、CRC均为字节。长度N只包括有效数据，不包含CRC。来源：[SH367309.pdf p50图14](../../sources/SH367309.pdf#page=50)、[p51图15-18](../../sources/SH367309.pdf#page=51)。

- 单字节写：S → 0x34 → ACK → reg → ACK → DATA → ACK → CRC → ACK → P
- 多字节读：S → 0x34 → ACK → reg → ACK → N → ACK → Sr → 0x35 → ACK → DATA[0] → ACK → … → DATA[N-1] → ACK → CRC → NACK → P
- 软件复位：S → 0x34 → ACK → 0xEA → ACK → 0xC0 → ACK → CRC → ACK → P

读最后一个数据字节仍ACK，因为后面还有CRC；CRC才NACK。未定义地址返回全1，不可当有意义的默认值。不得改为多字节burst write；写DATA长度固定1Byte。 [SH367309.pdf p50](../../sources/SH367309.pdf#page=50)、[p51](../../sources/SH367309.pdf#page=51)

### 4.3 CRC契约

- 多项式x⁸+x²+x+1；省略最高项的常用表示为0x07（由多项式换算）
- 初始值固定0x00
- 写校验字节流：[0x34, reg, DATA]
- 读校验字节流：[0x34, reg, N, 0x35, DATA[0]…DATA[N-1]]；**重复START不重置CRC累计**
- 软件复位按写式覆盖[0x34,0xEA,0xC0]
- 写CRC正确才更新寄存器并ACK；错误不更新并NACK

上述均据 [SH367309.pdf p51](../../sources/SH367309.pdf#page=51)。原文没有单列RefIn/RefOut/XorOut或给标准校验向量；不能宣称完整CRC实现已在硬件验证。总线MSB-first不自动等于所有CRC参数都已明文指定。建议用已知寄存器读取帧确认完整实现，再允许配置写入；这是实施建议。

### 4.4 读写空间与间隔

|空间|公开读范围|公开写范围|写入约束|来源|
|---|---|---|---|---|
|EEPROM|0x00-0x19|0x00-0x18|VPRO烧写，单字节；下一次写前≥35ms|[SH367309.pdf p50](../../sources/SH367309.pdf#page=50)、[p51](../../sources/SH367309.pdf#page=51)|
|RAM|0x40-0x72|0x40-0x42、0x70-0x72|单字节；下一次写前≥1ms；仍服从逐位权限|[SH367309.pdf p51](../../sources/SH367309.pdf#page=51)|

0x00-0x3F为EEPROM映像，图中0x1A-0x3F称DE Option，表中列为Reserved；这不授权访问工厂区。p53仅写Reserved 1A-1F、p66写1A-3F，协议有效公开范围是00-19。 [SH367309.pdf p52](../../sources/SH367309.pdf#page=52)、[p53](../../sources/SH367309.pdf#page=53)、[p66](../../sources/SH367309.pdf#page=66)、[p74](../../sources/SH367309.pdf#page=74)

### 4.5 EEPROM安全流程

供VPRO=7.7-8.3V（typ8V），等待10ms进入烧写；此时关闭MOS与保护。只写需改变的00-18字节，遵守35ms间隔；读回校验并检查BSTATUS3.EEPR_WR（1=写错，0=正确）；执行软件复位使参数生效，经过WarmUp再核验。TR0x19只读。写入复位之外，VPRO撤去的精确等待时间、掉电中断写的原子性和保持寿命未给出，不能添加保证。 [SH367309.pdf p12](../../sources/SH367309.pdf#page=12)、[p50](../../sources/SH367309.pdf#page=50)、[p51](../../sources/SH367309.pdf#page=51)、[p65](../../sources/SH367309.pdf#page=65)、[p72](../../sources/SH367309.pdf#page=72)、[p77](../../sources/SH367309.pdf#page=77)

EEPROM默认值未公布；启动先读取，不填一套臆测“标准值”。原文“编程/擦除次数≤100次”必须原样理解为受限次数信息，不能改写成保证至少100次。 [SH367309.pdf p1](../../sources/SH367309.pdf#page=1)、[p53-65](../../sources/SH367309.pdf#page=53)

## 5. 全部配置与状态寄存器

### 5.1 EEPROM逐地址

下表为全部公开EEPROM字段；均没有给出可靠出厂/复位值。00-18为烧写模式R/W，19为R。Reserved保持未指定，不随意改写。完整逐位对象及枚举在[registers.json](registers.json)。

|地址|名字|bits7→0或组合字段|来源|
|---|---|---|---|
|00|SCONF1|ENPCH, ENMOS, OCPM, BAL, CN[3:0]|[SH367309.pdf p54](../../sources/SH367309.pdf#page=54)|
|01|SCONF2|E0VB, Reserved, UV_OP, DIS_PF, CTLC[1:0], OCRA, EUVR|[SH367309.pdf p55](../../sources/SH367309.pdf#page=55)|
|02|OVT/LDRT/OVH|OVT[3:0]占7:4，LDRT[1:0]占3:2，OV[9:8]占1:0|[SH367309.pdf p56](../../sources/SH367309.pdf#page=56)|
|03|OVL|OV[7:0]|同上|
|04|UVT/OVRH|UVT[3:0]占7:4，3:2 Reserved，OVR[9:8]占1:0|[SH367309.pdf p57](../../sources/SH367309.pdf#page=57)|
|05|OVRL|OVR[7:0]|同上|
|06|UV|UV[7:0]|同上|
|07|UVR|UVR[7:0]|同上|
|08|BALV|BALV[7:0]|[SH367309.pdf p58](../../sources/SH367309.pdf#page=58)|
|09|PREV|PREV[7:0]|同上|
|0A|L0V|bit7 Reserved，L0V[6:0]；源说明位号写7:0但标签只有6:0|同上|
|0B|PFV|PFV[7:0]|同上|
|0C|OCD1V/OCD1T|电压档7:4，延时档3:0|[SH367309.pdf p59](../../sources/SH367309.pdf#page=59)|
|0D|OCD2V/OCD2T|电压档7:4，延时档3:0|[SH367309.pdf p60](../../sources/SH367309.pdf#page=60)|
|0E|SCV/SCT|电压档7:4，延时档3:0|[SH367309.pdf p61](../../sources/SH367309.pdf#page=61)|
|0F|OCCV/OCCT|电压档7:4，延时档3:0|[SH367309.pdf p62](../../sources/SH367309.pdf#page=62)|
|10|MOST/OCRT/PFT|CHS占7:6，MOST占5:4，OCRT占3:2，PFT占1:0|[SH367309.pdf p63](../../sources/SH367309.pdf#page=63)|
|11/12|OTC/OTCR|充电高温保护/恢复阈值，各8bit|同上|
|13/14|UTC/UTCR|充电低温保护/恢复阈值，各8bit|[SH367309.pdf p64](../../sources/SH367309.pdf#page=64)|
|15/16|OTD/OTDR|放电高温保护/恢复阈值，各8bit|同上|
|17/18|UTD/UTDR|放电低温保护/恢复阈值，各8bit|[SH367309.pdf p64](../../sources/SH367309.pdf#page=64)、[p65](../../sources/SH367309.pdf#page=65)|
|19|TR|bit7 Reserved，TR[6:0]只读参考电阻系数|[SH367309.pdf p65](../../sources/SH367309.pdf#page=65)|
|1A-3F|Reserved/DE Option|不作为公开配置|[SH367309.pdf p52](../../sources/SH367309.pdf#page=52)、[p66](../../sources/SH367309.pdf#page=66)|

SCONF1：ENPCH0禁预充/1启用；ENMOS1允许保护关充电MOS后按放电状态重开；OCPM0单向保护只关相应MOS、1电流保护同时关充放电；BAL0由内部开启平衡、1由MCU开启但内部调度。CN=5…15分别对应5…15串，其他码都表示16串；少于16串时靠VBAT的未使用输入接最高串正端。 [SH367309.pdf p54](../../sources/SH367309.pdf#page=54)

SCONF2：E0VB1开启低压禁止充电；UV_OP1欠压关充放电，0仅放电；DIS_PF1禁二次过充和断线，0使能；CTLC=00不使用CTL，01控制充电/预充，10控制放电，11控制三者；CTL低强关，CTL高交内部。OCRA1允许电流保护定时自恢复；EUVR1要求欠压恢复时额外满足负载释放。 [SH367309.pdf p30](../../sources/SH367309.pdf#page=30)、[p55](../../sources/SH367309.pdf#page=55)

### 5.2 RAM配置、实际状态、事件标志

所有地址为十六进制；表中复位值由逐位复位行合成。 [SH367309.pdf p32](../../sources/SH367309.pdf#page=32)、[p34](../../sources/SH367309.pdf#page=34)、[p67](../../sources/SH367309.pdf#page=67)、[p68](../../sources/SH367309.pdf#page=68)、[p69-74](../../sources/SH367309.pdf#page=69)

|地址/名字|复位|权限|bit7→bit0|
|---|---|---|---|
|40 CONF|70|R/W|OCRC, PCHMOS, DSGMOS, CHGMOS, CADCON, ENWDT, SLEEP, IDLE|
|41 BALANCEH|00|R/W|CB16…CB9|
|42 BALANCEL|00|R/W|CB8…CB1|
|43 BSTATUS1|00|R|WDT, PF, SC, OCC, OCD2, OCD1, UV, OV|
|44 BSTATUS2|00|R|Reserved[7:4], OTD, UTD, OTC, UTC|
|45 BSTATUS3|00|R|CHGING, DSGING, Reserved, EEPR_WR, L0V, PCHG_FET, CHG_FET, DSG_FET|
|70 BFLAG1|00|R/W0C|WDT_FLG, PF_FLG, SC_FLG, OCC_FLG, LOAD_FLG, OCD_FLG, UV_FLG, OV_FLG|
|71 BFLAG2|80|混合|RST_FLG, WAKE_FLG, CADC_FLG, VADC_FLG, OTD_FLG, UTD_FLG, OTC_FLG, UTC_FLG|
|72 RSTSTAT|00|bits1:0 R/W；其余R/Reserved|Reserved[7:2], WDT[1:0]|

- CONF.OCRC按0→1→0连续写释放电流保护，不能只写1；PCHMOS/DSGMOS/CHGMOS写0强关、写1交硬件；CADCON和ENWDT使能独立模块；SLEEP/IDLE请求醒来自动清。 [SH367309.pdf p23](../../sources/SH367309.pdf#page=23)、[p69](../../sources/SH367309.pdf#page=69)
- BSTATUS是当前状态，BFLAG是发生过的事件。清事件不等于清物理保护；但WDT_FLG清零明确解除WDT溢出。PF_FLG同时承载二次过充/断线，无法仅凭该位区分根因。OCD_FLG将OCD1/OCD2合并，当前状态才区分。 [SH367309.pdf p18](../../sources/SH367309.pdf#page=18)、[p28](../../sources/SH367309.pdf#page=28)、[p30](../../sources/SH367309.pdf#page=30)、[p34](../../sources/SH367309.pdf#page=34)、[p70](../../sources/SH367309.pdf#page=70)、[p73](../../sources/SH367309.pdf#page=73)
- BFLAG1全部W0C；BFLAG2的bits7,6,3:0 W0C，bits5:4只读且读取即清。通用W1C清位函数会错。读取BFLAG2即有副作用，即使后续CRC不匹配也不能假定旧ADC标志仍在；这最后一条是由读清机制推导的驱动风险，芯片未规定CRC失败时回滚标志。 [SH367309.pdf p46](../../sources/SH367309.pdf#page=46)、[p48](../../sources/SH367309.pdf#page=48)、[p73](../../sources/SH367309.pdf#page=73)、[p74](../../sources/SH367309.pdf#page=74)
- WDT[1:0]码0/1/2/3对应32/16/8/4s。有效TWI通信喂狗；溢出关所有MOS并清平衡。清WDT_FLG或芯片复位释放；进退WDT保护不清其他保护（复位除外）。 [SH367309.pdf p34](../../sources/SH367309.pdf#page=34)

### 5.3 全部测量寄存器

所有测量字节只读、复位0；每个结果高字节在低地址。下表穷举全部测量地址，源完整位图见 [SH367309.pdf p40-45](../../sources/SH367309.pdf#page=40)、[p47](../../sources/SH367309.pdf#page=47)、[p67-68](../../sources/SH367309.pdf#page=67)。

|测量|高/低地址|测量|高/低地址|
|---|---|---|---|
|TEMP1|46/47|TEMP2|48/49|
|TEMP3|4A/4B|CUR（VADC）|4C/4D|
|CELL1|4E/4F|CELL2|50/51|
|CELL3|52/53|CELL4|54/55|
|CELL5|56/57|CELL6|58/59|
|CELL7|5A/5B|CELL8|5C/5D|
|CELL9|5E/5F|CELL10|60/61|
|CELL11|62/63|CELL12|64/65|
|CELL13|66/67|CELL14|68/69|
|CELL15|6A/6B|CELL16|6C/6D|
|CADCD（CADC）|6E/6F|||

CELL1靠VSS，CELL16靠VBAT；高字节bits7:0承载结果bits15:8，低字节承载7:0。总表p68把CADCDL bit1印成CDATA.0，详表p47正确为CDATA.1，本参考按详表并记录差异。 [SH367309.pdf p40](../../sources/SH367309.pdf#page=40)、[p47](../../sources/SH367309.pdf#page=47)、[p68](../../sources/SH367309.pdf#page=68)

## 6. 转换公式、采样新鲜度与校准

### 6.1 数值表示与公式

VADC结果在原文明确为有符号16bit，尽管ADC核心13bit；不可拿未经定义的13位掩码截断寄存器。CUR和CADCD最高bit1表示放电、0表示充电。**原文没有明确写“二进制补码”，因此负数编码不能标成已确认**；驱动实现需用已知方向/幅值台架测量确认。不应把电流结果的符号定义直接套成RS2-RS1保护比较值的极性。 [SH367309.pdf p40](../../sources/SH367309.pdf#page=40)、[p45](../../sources/SH367309.pdf#page=45)、[p47](../../sources/SH367309.pdf#page=47)

以下公式经原PDF图像人工核验，解决提取文本缺公式的问题：

|物理量|公式与单位|来源|
|---|---|---|
|电芯电压|Vcell_mV = CELLn × 5 / 32；即0.15625mV/计数（数学换算）|[SH367309.pdf p46](../../sources/SH367309.pdf#page=46)|
|内部参考电阻|RREF_kΩ = 6.8 + 0.05 × (TR & 0x7F)|同上、[p28](../../sources/SH367309.pdf#page=28)|
|热敏电阻|RT_kΩ = TEMPn / (32768 - TEMPn) × RREF_kΩ；再用所装NTC的R-T关系换温度|[SH367309.pdf p46](../../sources/SH367309.pdf#page=46)|
|VADC电流|I_mA = 200 × CUR / (26837 × RSENSE_Ω)|同上|
|CADC电流|I_mA = 200 × CADCD / (21470 × RSENSE_Ω)|[SH367309.pdf p47](../../sources/SH367309.pdf#page=47)|

RSENSE单位必须为Ω；不能把mΩ数值直接代入。热敏公式分母为0或非物理结果要判无效，不作为极端真实温度；这是数值实现建议。规格未给通用NTC型号、B值替代所有外接NTC，也未给用户电压/电流校准寄存器、offset/gain流程。TR是明确的温度参考电阻校正信息；任何额外软件校准属于系统层，须单独标定而非称为芯片功能。 [SH367309.pdf p46](../../sources/SH367309.pdf#page=46)、[p53](../../sources/SH367309.pdf#page=53)、[p65](../../sources/SH367309.pdf#page=65)

### 6.2 采集范围与精度

|通道|范围|速度/误差|来源|
|---|---|---|---|
|电芯VADC|0-5V|单通道典型5ms；常温绝对精度±5mV|[SH367309.pdf p40](../../sources/SH367309.pdf#page=40)、[p77](../../sources/SH367309.pdf#page=77)|
|温度VADC|输入0-3V|单通道典型5ms；常温采集精度±2°C|同上|
|电流VADC|差分-200至200mV|典型5ms；常温绝对精度±150µV|[SH367309.pdf p78](../../sources/SH367309.pdf#page=78)|
|电流CADC|差分-200至200mV|转换237.5/250/262.5ms；INL典型±1、最大±3LSB|[SH367309.pdf p47](../../sources/SH367309.pdf#page=47)、[p78](../../sources/SH367309.pdf#page=78)|

不得把VADC采集±5mV、保护阈值±25mV或全温±50mV混为同一个指标；各自条件不同。 [SH367309.pdf p77-79](../../sources/SH367309.pdf#page=77)、[p82](../../sources/SH367309.pdf#page=82)

### 6.3 更新节奏、缓存与平衡影响

- 无平衡时，每tcycle采16电芯+1电流；tcycle min/typ/max95/100/105ms。三路温度每1s采一次；串数/温度点数变化不改变时序。 [SH367309.pdf p46](../../sources/SH367309.pdf#page=46)、[p76](../../sources/SH367309.pdf#page=76)
- 每周期完成置VADC_FLG并输出ALARM；读BFLAG2清标志。CADC每250ms更新一次并置CADC_FLG/ALARM，不受该标志未清影响。 [SH367309.pdf p46](../../sources/SH367309.pdf#page=46)、[p47](../../sources/SH367309.pdf#page=47)、[p48](../../sources/SH367309.pdf#page=48)
- 平衡时是“电压检测tcycle → 奇数平衡tbalanceT → 电压检测 → 偶数平衡tbalanceT”；tbalanceT典型400ms。电压采集不再可简单宣称持续10Hz；OV/OVR/UV/UVR/Powerdown/PF/预充/L0V进退延时可多出最大tbalanceT偏差。温度、电流检测/保护不受影响。 [SH367309.pdf p31](../../sources/SH367309.pdf#page=31)、[p46](../../sources/SH367309.pdf#page=46)、[p79](../../sources/SH367309.pdf#page=79)
- IDLE/SLEEP、PF/断线后不能把旧寄存器当新数据；PF/断线明确保留旧ADC数据。采样有效性应携带“最后有效更新时刻、工作模式、对应完成标志和通信CRC状态”；属于推荐的软件有效性模型。 [SH367309.pdf p11](../../sources/SH367309.pdf#page=11)、[p28](../../sources/SH367309.pdf#page=28)、[p30](../../sources/SH367309.pdf#page=30)
- 原文未承诺多字节读取期间冻结值、整组16电芯同步快照或读H锁存L。连续读H/L能减少风险，但不是证明原子一致性的依据；需要厂家澄清或台架边界验证。

## 7. 保护阈值编码、延时、动作与恢复

### 7.1 电压编码及合法工作设置范围

|功能|编码|电气表设置范围|恢复/关系|来源|
|---|---|---|---|---|
|OV|((02&03)<<8\|03)×5mV|3.6-4.5V|任一高于阈值触发|[SH367309.pdf p56](../../sources/SH367309.pdf#page=56)、[p78](../../sources/SH367309.pdf#page=78)|
|OVR|((04&03)<<8\|05)×5mV|3.3-4.5V|OVR<OV，所有电芯低于才恢复|[SH367309.pdf p57](../../sources/SH367309.pdf#page=57)、[p78](../../sources/SH367309.pdf#page=78)|
|UV|06×20mV|2.0-3.1V|任一低于阈值触发|[SH367309.pdf p57](../../sources/SH367309.pdf#page=57)、[p79](../../sources/SH367309.pdf#page=79)|
|UVR|07×20mV|2.0-3.6V|UV<UVR；所有高于恢复|同上|
|BALV|08×20mV|3.3-4.5V|硬件平衡门槛|[SH367309.pdf p58](../../sources/SH367309.pdf#page=58)、[p79](../../sources/SH367309.pdf#page=79)|
|PREV|09×20mV|1.0-3.0V|预充开启|同上|
|L0V|(0A&7F)×20mV|0.5-2.0V|低压禁止充电|同上|
|PFV|0B×20mV|3.8-5.0V|二次过充门槛|同上|

表内02/03等是寄存器**地址所存字节**，不是常数。更无歧义的实现表达式见JSON。硬件编码可表示的数学范围不等于电气表保证的可用范围。p58给出的电压高至低顺序为PFV→OV→OVR→UVR→UV→VPD→PREV→L0V，选值须检查这组关系。以上电压保护/恢复/平衡/预充/PF/L0V常温误差±25mV，全温±50mV。 [SH367309.pdf p58](../../sources/SH367309.pdf#page=58)、[p78](../../sources/SH367309.pdf#page=78)、[p79](../../sources/SH367309.pdf#page=79)、[p82](../../sources/SH367309.pdf#page=82)

OVT、UVT的码0…15依次为：100、200、300、400、600、800、1000、2000、3000、4000、6000、8000、10000、20000、30000、40000ms。恢复tOVR/tUVR典型2×tcycle。保护延时精度栏原样：OV/UV/PF的min为100ms−设定延时×5%，max为200ms＋设定延时×5%；单位栏为“−”。它不是单纯±5%，原文未用完整不等式说明该值与总实际延时的关系，保留原表达而不擅自改成总延时公式。 [SH367309.pdf p56](../../sources/SH367309.pdf#page=56)、[p57](../../sources/SH367309.pdf#page=57)、[p78](../../sources/SH367309.pdf#page=78)、[p79](../../sources/SH367309.pdf#page=79)

### 7.2 电流/短路完整枚举

每个字段均4bit，下列数组顺序就是编码0…15；不可线性插值，很多末尾档位不均匀。

|字段|码0…15的物理值|来源|
|---|---|---|
|OCD1V / OCCV，mV|20,30,40,50,60,70,80,90,100,110,120,130,140,160,180,200|[SH367309.pdf p59](../../sources/SH367309.pdf#page=59)、[p62](../../sources/SH367309.pdf#page=62)|
|OCD2V，mV|30,40,50,60,70,80,90,100,120,140,160,180,200,300,400,500|[SH367309.pdf p60](../../sources/SH367309.pdf#page=60)|
|SCV，mV|50,80,110,140,170,200,230,260,290,320,350,400,500,600,800,1000|[SH367309.pdf p61](../../sources/SH367309.pdf#page=61)|
|OCD1T，ms|50,100,200,400,600,800,1000,2000,4000,6000,8000,10000,15000,20000,30000,40000|[SH367309.pdf p59](../../sources/SH367309.pdf#page=59)|
|OCD2T / OCCT，ms|10,20,40,60,80,100,200,400,600,800,1000,2000,4000,8000,10000,20000|[SH367309.pdf p60](../../sources/SH367309.pdf#page=60)、[p62](../../sources/SH367309.pdf#page=62)|
|SCT，µs|0,64,128,192,256,320,384,448,512,576,640,704,768,832,896,960|[SH367309.pdf p61](../../sources/SH367309.pdf#page=61)|

OCD/SC阈值是RS2-RS1；OCC枚举值是RS1-RS2的正幅值，电气表VCOC写-200至-20mV。用电流估算阈值时I_A=V_shunt_V/Rsense_Ω只是欧姆定律推导，不是芯片直接以安培编程。短路SCT只指内部检测延时；Sense端RC还引入<50µs的额外延时（按原文说明），MOS关断时间另外计算。SCT=0不等于真实0延迟关断。 [SH367309.pdf p59-62](../../sources/SH367309.pdf#page=59)、[p78](../../sources/SH367309.pdf#page=78)、[p80](../../sources/SH367309.pdf#page=80)

电流阈值精度在幅值<100mV时±10mV，≥100mV时±10%；OCD1/OCD2/OCC延时误差±5%；SC延时精度0至+64µs，测试条件输入≥500mV+100mV。OCC精度栏比较VCOC<100mV/≥100mV却VCOC为负，符号表述不严谨，工程解释应按幅值并求厂家确认，不能掩盖原文问题。 [SH367309.pdf p80](../../sources/SH367309.pdf#page=80)、[p83](../../sources/SH367309.pdf#page=83)

### 7.3 共用两位字段与检测电平

|字段|代码00 / 01 / 10 / 11|来源|
|---|---|---|
|LDRT（02[3:2]）|100 / 500 / 1000 / 2000ms负载释放|[SH367309.pdf p56](../../sources/SH367309.pdf#page=56)|
|CHS（10[7:6]）|200 / 500 / 1000 / 2000µV状态检测|[SH367309.pdf p63](../../sources/SH367309.pdf#page=63)|
|MOST（10[5:4]）|64 / 128 / 256 / 512µs MOS开启|同上|
|OCRT（10[3:2]）|8 / 16 / 32 / 64s电流自恢复|同上|
|PFT（10[1:0]）|8 / 16 / 32 / 64s PF及断线延时|同上、[p30](../../sources/SH367309.pdf#page=30)|

CHS阈值实际min/typ/max分别50/200/350、350/500/650、850/1000/1150、1850/2000/2150µV；需RS2-RS1≤-VCH或≥VCH持续2×tcycle判断充/放电。两状态位都0为静置。 [SH367309.pdf p33](../../sources/SH367309.pdf#page=33)、[p78](../../sources/SH367309.pdf#page=78)、[p82](../../sources/SH367309.pdf#page=82)

CHGD睡眠/待激活连接阈VCHGD1列最小-0.25V，OCC断开VCHGD2列最大-0.05V；Powerdown连接VCHGD3列最小1.0V；DSGD释放VDSGD列最小1.0V。不要把这些单侧极限写成精确比较器典型点。DSGD下拉500/900/1400kΩ（VBAT50V、外接3V）。tD2拔充电器释放、tD3接充电器唤醒均400/500/600ms。 [SH367309.pdf p80](../../sources/SH367309.pdf#page=80)、[p82](../../sources/SH367309.pdf#page=82)、[p83](../../sources/SH367309.pdf#page=83)

### 7.4 保护状态机

|保护|触发及动作|恢复条件|来源|
|---|---|---|---|
|OV|任一电芯>VOV持续tOV；关充电MOS，置OV及OV_FLG|全部电芯<VOVR持续tOVR；清当前OV，重新由逻辑允许充电|[SH367309.pdf p13](../../sources/SH367309.pdf#page=13)|
|UV|任一<VUV持续tUV；关放电，置UV/UV_FLG；UV_OP=1连充电也关|全部>VUVR持续tUVR；EUVR=1还需DSGD<VDSGD。UV_OP=1时接充电器CHGD<VCHGD1后延时100ms开启充电|[SH367309.pdf p15](../../sources/SH367309.pdf#page=15)|
|OCD1/OCD2|RS2-RS1>设定门槛持续相应延时；关放电，置当前位和共用OCD_FLG|负载断开DSGD<VDSGD持续tD1；或已使能OCRA定时恢复；采集模式也可OCRC序列|[SH367309.pdf p17](../../sources/SH367309.pdf#page=17)、[p23](../../sources/SH367309.pdf#page=23)|
|SC|RS2-RS1>VDOC3持续tDOC3，关放电置SC/SC_FLG|同电流保护释放路径；不可把重启当唯一解除方法|同上|
|OCC|RS2-RS1<VCOC持续tCOC；关充电置OCC/OCC_FLG|CHGD>VCHGD2持续tD2；或OCRA/OCRC|同上|
|四种温度|任一测点越限持续tT；充电高/低温关充电，放电高/低温关放电；置对应状态/标志|全部测点回到各自恢复界限持续tT|[SH367309.pdf p24](../../sources/SH367309.pdf#page=24)、[p25](../../sources/SH367309.pdf#page=25)|
|PF二次过充|DIS_PF=0且任一>VP2N持续tP2N；关充放电及ADC，保留旧测量，PF拉VSS、PF状态/标志及ALARM|重新上电、软件复位、SHIP进退之一；禁止Powerdown/SLEEP|[SH367309.pdf p28](../../sources/SH367309.pdf#page=28)|
|断线|DIS_PF=0启用；检测算法/具体端口覆盖未详述；动作同PF，延时共用PFT|同PF；没有独立可区分状态位|[SH367309.pdf p30](../../sources/SH367309.pdf#page=30)|
|低压禁止充电|E0VB=1且任一<VL0V持续10×tcycle；关充电，L0V状态|原文称“一旦低于则永久无法充电”；未给独立恢复序列，不擅自许诺复位恢复|[SH367309.pdf p30](../../sources/SH367309.pdf#page=30)、[p72](../../sources/SH367309.pdf#page=72)|

OCPM=1使电流保护关充放电，覆盖表中默认单向关断；恢复时其他保护、CTL、WDT、CONF仍可阻止MOS真正开启。OCRA=1时除拔负载/充电器外，经过tAUTO也可恢复；MCU可按OCRC0-1-0释放电流状态，但应先检查危险条件是否消失，不能把自动清保护当安全重试策略。 [SH367309.pdf p23](../../sources/SH367309.pdf#page=23)、[p35](../../sources/SH367309.pdf#page=35)、[p38](../../sources/SH367309.pdf#page=38)

### 7.5 温度阈值编码与允许范围

RREF=6.8+0.05×TR[6:0]，RT是目标温度对应的外部热敏电阻阻值，单位均kΩ。**阈值不是摄氏温度的直接整数。**

- OTC/OTCR/OTD/OTDR：code = 512 × RT / (RREF + RT)
- UTC/UTCR/UTD/UTDR：code = 512 × (RT / (RREF + RT) - 0.5)

来源及视觉核验：[SH367309.pdf p28](../../sources/SH367309.pdf#page=28)。原文未规定浮点换整的舍入方式、越界饱和或非法值行为，配置工具应做范围/回算检查，不能默默wrap。

|门限|规定可设置温度范围|来源|
|---|---|---|
|OTC / OTCR|45-70°C / 40-70°C|[SH367309.pdf p81](../../sources/SH367309.pdf#page=81)|
|UTC / UTCR|-20至10°C / -20至15°C|同上|
|OTD / OTDR|45-80°C / 40-80°C|同上|
|UTD / UTDR|-40至10°C / -40至15°C|同上|

电气表写1°C一档，tT典型2s；温度保护精度典型±2°C、最大±4°C。温度采集精度±2°C是另一指标。保护与恢复编码要依实际NTC曲线回算并维持合理迟滞，不能仅以code数值大小猜温度升降方向。 [SH367309.pdf p77](../../sources/SH367309.pdf#page=77)、[p81](../../sources/SH367309.pdf#page=81)、[p83](../../sources/SH367309.pdf#page=83)

## 8. MOS控制、预充、CTL与事件

### 8.1 控制优先关系

DSG为驱动高/低；CHG/PCHG关闭为高阻，外围栅极网络决定关断效果。CONF三个MOS位为0可强关，为1只允许硬件判断；CTL低按CTLC优先关相应MOS，CTL高恢复交内部；WDT溢出关充/放/预充，PF/断线也关主MOS。判定实际输出要读BSTATUS3而非只读控制寄存器。 [SH367309.pdf p34](../../sources/SH367309.pdf#page=34)、[p35](../../sources/SH367309.pdf#page=35)、[p38](../../sources/SH367309.pdf#page=38)、[p72](../../sources/SH367309.pdf#page=72)

规格书不是完整的“所有并发保护优先级真值表”；未明确的多保护交互不可单靠某一句“开启MOS”推导为覆盖其他关断原因。

### 8.2 预充序列

ENPCH=1时，必须同时满足：处于UV，任一电芯<VPCH持续tPCHG；无OV、充电高/低温、OCC、PF，以及OCPM=1时的OCD；CTL未关预充、WDT未溢出；采集模式PCHMOS=1；低压禁充关闭或所有电芯都高于VL0V。预充开启时关闭普通充电MOS，平衡仍有效。 [SH367309.pdf p36](../../sources/SH367309.pdf#page=36)

停止预充任一条件：全部电芯>VPCH持续2×tcycle；上述阻止条件出现；CTL关预充；WDT溢出；采集模式PCHMOS=0；低压禁充已启用且任一低于VL0V。原文p36停止条件旁“设置为0（预充MOS开启）”是括号文字矛盾，以控制表“0关闭”为准并记录。tPCHG=950/1000/1250ms；PREV编码×20mV。 [SH367309.pdf p35](../../sources/SH367309.pdf#page=35)、[p36](../../sources/SH367309.pdf#page=36)、[p79](../../sources/SH367309.pdf#page=79)

### 8.3 ENMOS强制恢复充电MOS

ENMOS=1时，过充/温度保护关闭充电MOS之后，检测放电电流超过OCD1门槛且超过tMOSFET，或已识别放电状态，可强制开充电MOS；强制条件都不满足且保护未解除时，已打开的充电MOS延迟10ms关。tMOSFET由MOST选择64/128/256/512µs。不能把这当充电许可或绕过全部保护的通用强开命令。 [SH367309.pdf p37](../../sources/SH367309.pdf#page=37)、[p54](../../sources/SH367309.pdf#page=54)、[p63](../../sources/SH367309.pdf#page=63)

### 8.4 ALARM中断处理

采集模式正常高，事件上升产生低脉冲；保护模式高阻。事件包括VADC、CADC完成、OV/UV、电流/短路、温度、PF、WDT、LDO3过流、电流醒IDLE、充电器醒SLEEP。低脉冲与间隔min/typ/max均0.8/1/1.2ms。单个ALARM边沿不说明具体故障，也不保证一次边沿只含一个事件。 [SH367309.pdf p39](../../sources/SH367309.pdf#page=39)、[p76](../../sources/SH367309.pdf#page=76)

建议ISR只记时间/待处理，统一任务先缓存状态和一次BFLAG2读取的结果，再按ADC标志读取数据并做CRC、解释故障；清W0C标志前保存事件快照。通用读-改-写可能误清读取后新产生的W0C事件，必须按W0C专用清除意图管理；置1保留的具体并发语义在原文未详述，驱动需验证。该段为软件建议，非额外芯片保证。

## 9. 平衡

硬件控制BAL=0：无温度/PF保护、该CellN>VBAL并持续tbalanceT后开启；CellN<VBAL或温度/PF出现则停。采用奇偶交替与电压检测间隔，并非全部置位电芯一直同时导通。 [SH367309.pdf p31](../../sources/SH367309.pdf#page=31)

采集模式BAL=1：在无温度/PF前提下CBn=1请求平衡；CBn=0、持续1分钟、温度/PF出现之一停止。1分钟后所有Balance位清0；过程中任意位写1会重新开始1分钟计时。BALANCEH控制16…9，BALANCEL控制8…1。进入IDLE/SLEEP或WDT溢出也会清平衡。 [SH367309.pdf p11](../../sources/SH367309.pdf#page=11)、[p32](../../sources/SH367309.pdf#page=32)、[p34](../../sources/SH367309.pdf#page=34)

内部平衡电阻常温min/typ/max120/260/400Ω，全温75/300/500Ω，条件电芯=VBAL+100mV；并不是恒流源。估算平衡电流需包括板上串联电阻和调度占空比，不能直接按标称内阻宣称持续平衡电流。tbalanceT典型400ms。外部平衡电路例见图4；元件热容量、温升和电阻额定功率须按实际方案验证。 [SH367309.pdf p6](../../sources/SH367309.pdf#page=6)、[p79](../../sources/SH367309.pdf#page=79)、[p82](../../sources/SH367309.pdf#page=82)

## 10. 参考电路、连接与封装

### 10.1 全部参考电路的选择

|原图|用途|审查重点|来源|
|---|---|---|---|
|图3|保护模式16串同口|充放电共口、预充支路、负端分流与MOS、独立烧写接口|[SH367309.pdf p5](../../sources/SH367309.pdf#page=5)|
|图4|保护模式16串半分口，支持外部平衡|外部平衡晶体管/耗能电阻、充放路径及PF熔断驱动|[SH367309.pdf p6](../../sources/SH367309.pdf#page=6)|
|图5|保护模式16串全分口|独立P-/C-拓扑、额外放电支路器件，不能直接照同口改端口名称|[SH367309.pdf p7](../../sources/SH367309.pdf#page=7)|
|图6|保护模式10串全分口|顶部不用VC短接接最高串，与CN设置一致|[SH367309.pdf p8](../../sources/SH367309.pdf#page=8)|
|图7|采集模式16串半分口|MCU、电源、SCL/SDA/ALARM上拉及接口，硬件保护仍存在|[SH367309.pdf p9](../../sources/SH367309.pdf#page=9)|

图中包含电芯输入RC、供电滤波/钳位、Sense差分滤波、三路NTC、栅极钳位/电阻、PF相关外围和烧写接口。图示常见电芯输入为1kΩ及0.1µF/25V；这些是参考图器件值，**不是对所有电池包的通用推荐BOM**。图7 MCU侧SCL/SDA/ALARM有上拉/串阻；驱动端和高压检测端不得直接等同数字口。实际搭建须放大原图逐网核对，不能只由本摘要复制接线。 [SH367309.pdf p5-9](../../sources/SH367309.pdf#page=5)

V1.1特别更新CHGD器件；必须以本修订各拓扑的CHGD通路为准，不复用更早图。16串4.2V满充的总电压数学上为67.2V，超过本书VBAT65V正常范围；因此“16串支持”不能脱离化学体系与供电条件作合规结论。这是由额定范围推导的选型风险，非新增芯片规定。 [SH367309.pdf p1](../../sources/SH367309.pdf#page=1)、[p76](../../sources/SH367309.pdf#page=76)、[p86](../../sources/SH367309.pdf#page=86)

### 10.2 布板边界

本资料没有独立PCB布局章、推荐铜皮、爬电间距、Kelvin走线尺寸、热设计或ESD/EMC认证要求。不可声称给出了这些参数。实施审查可检查：Sense回路避开大电流共享压降；输入/电源滤波靠近芯片；开漏拉电阻与总线电容满足时序；高压与低压域按实际电压/适用规范隔离；所有参考图电容耐压、外部FET栅极钳位及电阻功率符合项目应力。此为一般工程建议，不替代厂家未提供的布局规范或真实板测试。

### 10.3 封装完整尺寸

TQFP48L，尺寸以mm为控制值；原表另给英寸。所有数值据 [SH367309.pdf p84](../../sources/SH367309.pdf#page=84)。

|符号|mm min…max/typ|
|---|---|
|A|最大1.2|
|A1|0.05…0.15|
|A2|0.9…1.05|
|D/E|各6.85…7.15|
|HD/HE|各8.8…9.2|
|b|0.15…0.27|
|e|0.500典型|
|c|0.090…0.200|
|L|0.45…0.75|
|L1|0.85…1.15|
|θ2|0…10°|

尺寸不含模具毛边/门毛刺；未特指容差±0.1mm；共面性0.1mm；转换英寸不作控制要求。此页是封装外形，不是推荐焊盘图。 [SH367309.pdf p84](../../sources/SH367309.pdf#page=84)

## 11. 可执行的安全驱动检查清单

以下为基于上述事实的实施建议，不是已经验证的生产驱动：

1. 先确认芯片型号/修订、真实串数、板上MODE/SHIP/LDO_EN/CTL电平、RSENSE单位和电流方向；不要默认全系列兼容
2. 检查VBAT工作额定与各脚绝对极限；分开评估电芯输入、负端瞬态、CHGD/DSGD负压、LDO负载
3. 启动等待实际WarmUp并识别待激活状态；首次读取确认RST_FLG、CONF及EEPROM内容，不以测量复位0作真实电芯电压
4. TWI明确7-bit地址；读请求必须带长度及重复START，CRC覆盖全部规定字节；成功CRC前不使用值；支持SCL拉伸、超时与合法STOP重建
5. 使用只读地址/可写地址白名单；00-18烧写、19只读，1A-3F禁改；RAM按位权限处理；保留位不从想象默认值填充
6. 为BFLAG单独实现W0C/读清逻辑；不复用W1C函数；读BFLAG2一次后缓存完整事件，不通过重复读取“确认”同一个ADC完成标志
7. 将控制寄存器、当前保护状态、历史事件和实际FET状态分开；OCRC序列、ENMOS和自动恢复前确认风险，避免无限重试短路
8. 电压、TEMP、CUR、CADCD分开转换；保存原始码和单位；边界检查NTC公式；台架验证负数编码、电流方向、CRC细节及H/L一致性
9. 样本附有效标志与时间；低功耗/PF/断线或长时间无ADC完成后标过期；别把每100ms VADC标志当温度每100ms更新
10. 改配置工具应回算实际门槛、枚举取整及迟滞，检查PFV>OV>OVR>UVR>UV>VPD>PREV>L0V关系；不接受范围外或整数溢出
11. 平衡按奇偶和1分钟清零设计，WDT/低功耗进入后重读Balance；不要定时盲目刷新导致无期限平衡
12. EEPROM烧写在安全隔离测试条件下执行：保护关闭期间由外部保证安全；单字节、间隔、读回、EEPR_WR、软件复位、再读确认，限制写次数
13. 中断测试覆盖并发故障与ADC、事件读清、通信损坏、看门狗、充电器反复插拔、MOS迟滞、低功耗唤醒和复位8分钟PF盲区
14. 对本参考列出的原文冲突取得厂家澄清；“没有写明”不能升级为“支持/保证”

关键依据：[SH367309.pdf p11-12](../../sources/SH367309.pdf#page=11)、[p23](../../sources/SH367309.pdf#page=23)、[p28-39](../../sources/SH367309.pdf#page=28)、[p46-51](../../sources/SH367309.pdf#page=46)、[p58](../../sources/SH367309.pdf#page=58)、[p69-81](../../sources/SH367309.pdf#page=69)。

## 12. 原文不确定性和禁止补全项

- EEPROM复位/出厂值未给；不能把null写成0
- 当前结果有符号，但补码/原码等负数表示未直接说明；CRC无RefIn/RefOut/XorOut独立声明/测试向量
- 全组测量与H/L读取锁存机制、内部更新和主机读取的竞态未定义
- W0C并发置位优先级、读清标志遇CRC错误时的精确状态未定义
- VPRO极限自引用；VPD min/max排序异常；OCC精度条件的符号表述含糊
- p11与p69对已接充电器后SLEEP瞬态的说法不同；p39与p74对WAKE来源描述粒度不同
- p53 UV bit3误作OV.3，p57/66为UV.3；p68 CADCDL bit1误作CDATA.0，p47为CDATA.1；p57 UVR注释误写UV，字段及寄存器名为UVR；p30/58 L0V位宽文字与位表不同
- L0V永久禁充后的恢复范围未交代；不能擅自提供可靠复位解除流程
- 断线检测算法、测试覆盖、断线根因独立诊断、EEPROM保持寿命与掉电写安全未公布
- 内置保护不代表系统安全认证；本文没有电池化学参数、项目电流额定、NTC曲线、充电策略、MOS/BOM选型授权

详见[validation.md](validation.md)，所有来源页与覆盖状态在[coverage.json](coverage.json)。
