# SH36735XX 系列 AI 可读工程参考

<a id="overview"></a>
## 0. 范围、版本与使用边界

- 文件名中的CV是本包来源标签，页脚实际版本为V1.0A/V0.2C。主基线：SH36735XX CV1.0A.pdf，正文版本 V1.0A，修订记录日期 2025年10月，57页。对比来源：SH36735XX CV0.2C.pdf，V0.2C、Preliminary、2024年12月，57页。两份的 PDF 页码与印刷页码一致。本文除明确标出旧版外均为 CV1.0A；旧版数值不得套用新版，也不可反向把新条目补进旧版。详细差异见 [version_differences.md](version_differences.md)。[SH36735XX CV1.0A.pdf，PDF/印刷页54](../../sources/SH36735XX%20CV1.0A.pdf#page=54) [SH36735XX CV0.2C.pdf，PDF/印刷页54](../../sources/SH36735XX%20CV0.2C.pdf#page=54)
- 用户所称“3520”在这里指 SH3673520；系列还含 SH3673510、SH3673514、SH3673517。厂商说明四型仅串数不同，其他功能相同：分别支持 4–10、4–14、4–17、4–20 串，总电压不超过 88V。封装均为 TQFP48。[SH36735XX CV1.0A.pdf，PDF/印刷页1](../../sources/SH36735XX%20CV1.0A.pdf#page=1)
- 本参考按“硬件事实 / 手册内部疑点 / 工程建议”分开。所有复位值是芯片手册值，不是面向任意电芯、分流器或功率级的安全配置建议。没有依据用户项目虚构参数。
- 伴随 [registers.json](registers.json) 包含每版独立的 90 个地址行、全部 bit、访问/复位/副作用/编码及页码；[coverage.json](coverage.json) 覆盖两份共114页；[validation.md](validation.md) 记录原图检查和未解决问题。

### 0.1 模块全景

SH36735XX 是数字 BMS 模拟前端：13-bit Σ-Δ VADC 采样电芯、C+、B+、电流、四路外部温度及内部温度；16-bit Σ-Δ CADC 采样电流，供 MCU 统计容量；片上带过充/过放、两级放电过流、短路、充电过流、充放电高低温保护、内部高温保护、断线检测、均衡、看门狗、SPI、高/低侧充放电 N-MOS 驱动和高侧预放电 P-MOS 驱动。它不是完整 SOC 算法，也未公开容量累加寄存器。[SH36735XX CV1.0A.pdf，PDF/印刷页1](../../sources/SH36735XX%20CV1.0A.pdf#page=1) [SH36735XX CV1.0A.pdf，PDF/印刷页2](../../sources/SH36735XX%20CV1.0A.pdf#page=2) [SH36735XX CV1.0A.pdf，PDF/印刷页21](../../sources/SH36735XX%20CV1.0A.pdf#page=21)

系统方框关系：VBAT供电 → 内部LDO1/VCC和外部LDO2/LDO_O；VC电芯输入经电平移位/均衡模块到MUX/VADC；RS差分送VADC/CADC及过流模块；逻辑/寄存器将保护和MCU请求共同送FET驱动；SPI读写和ALARM/RESET为MCU接口。[SH36735XX CV1.0A.pdf，PDF/印刷页2](../../sources/SH36735XX%20CV1.0A.pdf#page=2)

<a id="pins"></a>
## 1. 管脚、电芯串数与供电

### 1.1 全部48脚映射

| 脚号 | SH3673520信号 | 其他型号差异 | I/O与用途 |
|---|---|---|---|
| 1 | VBAT | 无 | P，芯片供电正端 |
| 2–4 | VC20、VC19、VC18 | 3517/3514/3510均NC | I，对应电芯正端 |
| 5–7 | VC17、VC16、VC15 | 3514/3510为NC；3517有效 | I，对应电芯正端 |
| 8–11 | VC14、VC13、VC12、VC11 | 3510为NC；3514/3517有效 | I，对应电芯正端 |
| 12–21 | VC10、VC9、VC8、VC7、VC6、VC5、VC4、VC3、VC2、VC1 | 全系列相同 | I，电芯正端；脚号=22−电芯编号 |
| 22 | VC0 | 无 | I，第1节电芯负端 |
| 23 / 24 | RS1 / RS2 | 无 | I，电流采样负端/正端 |
| 25–28 | TS1、TS2、TS3、TS4 | 无 | I，四路外部NTC |
| 29 | ALARM | 无 | O，开漏报警，外部上拉 |
| 30 / 31 / 32 / 33 | CS / SCK / SDI / SDO | 无 | I / I / I / O，SPI；CS低有效 |
| 34 | RESET | 无 | O，开漏复位输出；不是芯片复位输入 |
| 35 / 36 | VSS / VCC | 无 | P供电负端 / O内部LDO1输出 |
| 37 / 38 | DSG / CHG | 无 | O，低侧放/充电MOS控制 |
| 39 / 40 | LDO_O / LDO_P | 无 | O外部LDO2输出 / P外部LDO2供电 |
| 41 / 42 | DSGD / CHGD | 无 | I，负载检测 / C+采样与充电器唤醒 |
| 43 / 44 / 45 | HDSG / PDSG / HCHG | 无 | O，高侧放电/预放电/充电驱动 |
| 46 / 47 / 48 | VCP / VCPR / SHIP | 无 | O电荷泵输出 / P电荷泵输入 / I运输模式 |

整个管脚表来自 [SH36735XX CV1.0A.pdf，PDF/印刷页4](../../sources/SH36735XX%20CV1.0A.pdf#page=4) [SH36735XX CV1.0A.pdf，PDF/印刷页5](../../sources/SH36735XX%20CV1.0A.pdf#page=5)，封装顶视图见 [SH36735XX CV1.0A.pdf，PDF/印刷页3](../../sources/SH36735XX%20CV1.0A.pdf#page=3)。上表的 NC 是该型号不存在的端口，与“芯片存在但当前串数未用”的 VC 端口不同。

### 1.2 串数连接和CN编码

SCONF4.CN[4:0] 直接编码串数。合法区间为本型号 4..最大串数；其他编码按本型号最大串数处理，因此复位 CN=31 对应各型号最大串数，不能把复位值31理解为31串。SCONF4复位0x7F还包含PDSGT=3。[SH36735XX CV1.0A.pdf，PDF/印刷页32](../../sources/SH36735XX%20CV1.0A.pdf#page=32) [SH36735XX CV1.0A.pdf，PDF/印刷页33](../../sources/SH36735XX%20CV1.0A.pdf#page=33)

电芯从底部连续连接：Cell1在VC1−VC0，CellN在VCN−VC(N−1)。当配置少于器件最大串数，未用的上部相邻VC输入短接，手册串数表以S表示Short；存在的VCN(N≥5)端口不允许悬空。例如配置4串时，VC4以上未用输入都按表短接。寄存器中的未启用CELLnH/L在串数配置生效后清0。不能把这些0值当真实欠压电芯。[SH36735XX CV1.0A.pdf，PDF/印刷页8](../../sources/SH36735XX%20CV1.0A.pdf#page=8) [SH36735XX CV1.0A.pdf，PDF/印刷页43](../../sources/SH36735XX%20CV1.0A.pdf#page=43)

B+测量在最高物理通道：3510取VC10、3514取VC14、3517取VC17、3520取VC20；减少串数后，短接链仍使其代表总压。[SH36735XX CV1.0A.pdf，PDF/印刷页24](../../sources/SH36735XX%20CV1.0A.pdf#page=24)

### 1.3 电源和逻辑域

正常工作VBAT为8–88V；内部LDO1输出VCC为5.0/5.2/5.4V(min/typ/max，VBAT 8–88V，负载2mA)，其LVR阈值典型4.0V。LDO2输出LDO_O为3.1/3.3/3.5V（无负载），特性首页标称3.3V、25mA最大应用输出；短路限流45/75/130mA不是允许持续带载能力。LDO2 LVR为2.1/2.3/2.5V，只复位SPI并置RST2_FLG；LDO1 LVR导致系统复位/WarmUp并关FET。[SH36735XX CV1.0A.pdf，PDF/印刷页1](../../sources/SH36735XX%20CV1.0A.pdf#page=1) [SH36735XX CV1.0A.pdf，PDF/印刷页11](../../sources/SH36735XX%20CV1.0A.pdf#page=11) [SH36735XX CV1.0A.pdf，PDF/印刷页47](../../sources/SH36735XX%20CV1.0A.pdf#page=47)

LDO_O电压线性度typ10/max50mV（VBAT 8–88V，25mA）；负载调整率typ30/max100mV（VBAT70V，0.1–25mA）。数字输入高至少0.8×LDO_O、低最多0.2×LDO_O；CS/SCK/SDI内部上拉1/2/3MΩ。SDO高电平至少LDO_O−0.6V@−10mA，低最多0.6V（测试电流15mA）；RESET/ALARM低最多0.4V（测试电流1mA）。[SH36735XX CV1.0A.pdf，PDF/印刷页46](../../sources/SH36735XX%20CV1.0A.pdf#page=46) [SH36735XX CV1.0A.pdf，PDF/印刷页47](../../sources/SH36735XX%20CV1.0A.pdf#page=47)

<a id="hardware"></a>
## 2. 应用原理图、外部器件与布局

### 2.1 两个参考拓扑

正文实际顺序为：5.1 SH3673520的20串低侧N-MOS同口应用（图3，PDF页6）；5.2 SH3673517的16串高侧N-MOS同口应用（图4，PDF页7）。目录页把两小节的标题/位置写反，不应据目录选择电路。[SH36735XX CV1.0A.pdf，PDF/印刷页6](../../sources/SH36735XX%20CV1.0A.pdf#page=6) [SH36735XX CV1.0A.pdf，PDF/印刷页7](../../sources/SH36735XX%20CV1.0A.pdf#page=7) [SH36735XX CV1.0A.pdf，PDF/印刷页56](../../sources/SH36735XX%20CV1.0A.pdf#page=56)

低侧参考图：B+/P+/C+共正端，低侧背靠背N-MOS切换P−/C−；VC链带输入滤波，RS端接低侧分流器及滤波/保护；图中有外部均衡单元示例；MCU SPI串阻和RESET上拉；新版明确注释“MCU电源由其它电路部分提供，本图省略”。不要误认低侧图已经完整提供MCU供电电路。[SH36735XX CV1.0A.pdf，PDF/印刷页6](../../sources/SH36735XX%20CV1.0A.pdf#page=6)

高侧参考图：B−与P−/C−共地，高侧充/放电N-MOS与P-MOS预放电，VCPR/VCP电荷泵及检测端外围，独立外部晶体管供电网络至LDO_P/LDO_O，VC与RS滤波/钳位及四路NTC。参考图使用16串、3517，所以未用最高VC按其连接图处理，不可照搬给其他串数。[SH36735XX CV1.0A.pdf，PDF/印刷页7](../../sources/SH36735XX%20CV1.0A.pdf#page=7) [SH36735XX CV1.0A.pdf，PDF/印刷页8](../../sources/SH36735XX%20CV1.0A.pdf#page=8)

### 2.2 新版原理图关键值，均为参考值

- 低侧图最高节对地C6为0.1uF/100V；高侧图对应C12为0.1uF/100V，旧版均为1uF/100V。[SH36735XX CV1.0A.pdf，PDF/印刷页6](../../sources/SH36735XX%20CV1.0A.pdf#page=6) [SH36735XX CV1.0A.pdf，PDF/印刷页7](../../sources/SH36735XX%20CV1.0A.pdf#page=7) [SH36735XX CV0.2C.pdf，PDF/印刷页6](../../sources/SH36735XX%20CV0.2C.pdf#page=6) [SH36735XX CV0.2C.pdf，PDF/印刷页7](../../sources/SH36735XX%20CV0.2C.pdf#page=7)
- 高侧图R1/R6/R16/R17从56kΩ改为51kΩ；R5从100Ω改10Ω；VCPR输入R11/R12从200Ω/1206改为51Ω/0805；HCHG/HDSG/DSGD对地的85V稳压管被删除。修改必须结合整张图核对，不宜只更新BOM。[SH36735XX CV1.0A.pdf，PDF/印刷页7](../../sources/SH36735XX%20CV1.0A.pdf#page=7) [SH36735XX CV0.2C.pdf，PDF/印刷页7](../../sources/SH36735XX%20CV0.2C.pdf#page=7) [SH36735XX CV1.0A.pdf，PDF/印刷页54](../../sources/SH36735XX%20CV1.0A.pdf#page=54)
- 参考VC支路多用1kΩ与0.1uF/50V差分滤波；图上电芯最高总压去耦、VBAT、充电泵、RS和FET驱动电容各有不同额定值，不能一律使用单体耐压。两图分流器示例为两个2mΩ并联，但这不是本参考推荐的分流阻值。[SH36735XX CV1.0A.pdf，PDF/印刷页6](../../sources/SH36735XX%20CV1.0A.pdf#page=6) [SH36735XX CV1.0A.pdf，PDF/印刷页7](../../sources/SH36735XX%20CV1.0A.pdf#page=7)
- Charge Pump允许电容0.22/1/2.2uF(min/typ/max)；新版建立时间typ100/max125ms的条件是0.47uF并建立到VVCPTH，并不等价于“任意电容125ms必定完成”。[SH36735XX CV1.0A.pdf，PDF/印刷页48](../../sources/SH36735XX%20CV1.0A.pdf#page=48)

工程建议（不属于芯片厂商给出的PCB约束）：对RS1/RS2采用与功率铜皮分离的Kelvin采样；滤波器靠近AFE；数字回流避开微伏级电流采样；开关节点和高压VC链保持适当间距；按最大母线瞬态验证器件耐压/门极钳位；逐条审查NC与短接VC；使用真实FET栅电荷、分流器温漂、NTC容差和连接器热插拔条件做样机验证。手册没有给出完整PCB层叠、爬电距离、EMC布线尺寸或允许乱序上电的详细测试条件，不能把首页“支持乱序上电”扩展为任意无保护热插拔保证。[SH36735XX CV1.0A.pdf，PDF/印刷页1](../../sources/SH36735XX%20CV1.0A.pdf#page=1) [SH36735XX CV1.0A.pdf，PDF/印刷页45](../../sources/SH36735XX%20CV1.0A.pdf#page=45) [SH36735XX CV1.0A.pdf，PDF/印刷页55](../../sources/SH36735XX%20CV1.0A.pdf#page=55)

<a id="modes"></a>
## 3. 模式、复位、唤醒与配置持久性

### 3.1 功能模式矩阵

| 模式 | VADC | CADC | 保护/均衡 | SPI/LDO | 唤醒 |
|---|---|---|---|---|---|
| Normal | 电压/电流/B+/C+周期70ms；温度见采样节 | 开启时250ms | 各保护可配，内部温度保留，均衡可用 | CS低开启SPI；两个LDO开 | 默认运行态 |
| IDLE | 周期280ms | 转换4s，平均结果4/32/64/256s可配 | 保护/均衡可用，多项延时为Normal四倍 | SPI/LDO开 | 检出充/放电或SCONF1=0 |
| SLEEP | 70ms周期，仅电压与内部温度 | 关 | 外部温度/电压/电流保护、均衡、WDT、泵关；内部高温保护仍开 | SPI/LDO开 | MCU命令、按使能的充电器或负载条件 |
| Powerdown | 关 | 关 | FET/泵/其他模块关，仅充电器唤醒 | SPI/LDO关 | 充电器→WarmUp；SHIP低→SHIP |
| SHIP | 关 | 关 | 全关，充电器无作用 | 关 | 只有SHIP高退出→WarmUp |

来源 [SH36735XX CV1.0A.pdf，PDF/印刷页9](../../sources/SH36735XX%20CV1.0A.pdf#page=9) [SH36735XX CV1.0A.pdf，PDF/印刷页10](../../sources/SH36735XX%20CV1.0A.pdf#page=10) [SH36735XX CV1.0A.pdf，PDF/印刷页11](../../sources/SH36735XX%20CV1.0A.pdf#page=11) [SH36735XX CV1.0A.pdf，PDF/印刷页19](../../sources/SH36735XX%20CV1.0A.pdf#page=19) [SH36735XX CV1.0A.pdf，PDF/印刷页21](../../sources/SH36735XX%20CV1.0A.pdf#page=21) [SH36735XX CV1.0A.pdf，PDF/印刷页35](../../sources/SH36735XX%20CV1.0A.pdf#page=35)。模式表的“√(O)”表示由MCU配置使能；不可由此断言复位全关闭：SCONF5和SCONF6详细表显示很多保护及CADC复位已使能。复位配置与功能可用性是两件事。[SH36735XX CV1.0A.pdf，PDF/印刷页34](../../sources/SH36735XX%20CV1.0A.pdf#page=34)

### 3.2 进入与退出条件

- IDLE：BSTATUS2.CHGING=DSGING=0且SCONF1=0x55才进入。检出充/放电或写SCONF1=0退出，硬件清IDLE和SCONF1；非通信退出置WK_FLG，若WK_INT=1发ALARM。[SH36735XX CV1.0A.pdf，PDF/印刷页9](../../sources/SH36735XX%20CV1.0A.pdf#page=9) [SH36735XX CV1.0A.pdf，PDF/印刷页10](../../sources/SH36735XX%20CV1.0A.pdf#page=10)
- SLEEP：SCONF1=0xAA；关闭CADC、WDT、电压/电流保护、充放电FET、自动预放电(PDSGMOS=1)、泵和均衡。CGR_WK使能充电器唤醒；LD_WK=01为负载连接唤醒、10为未连接唤醒、00/11关闭。退出时清SLEEP和SCONF1，非通信退出置WK_FLG并按使能发ALARM。[SH36735XX CV1.0A.pdf，PDF/印刷页10](../../sources/SH36735XX%20CV1.0A.pdf#page=10) [SH36735XX CV1.0A.pdf，PDF/印刷页32](../../sources/SH36735XX%20CV1.0A.pdf#page=32)
- Powerdown：连续两条写命令先置SCONF2.PD_CTL=1，再写SCONF1=0x33，中间任何其他指令使PD_CTL自动清0；或PD_EN=1时任一单体低于VPD超时；或内部高温；或WDT溢出后9.8s内未清标志。此模式无论PDSGMOS如何都关预放电。[SH36735XX CV1.0A.pdf，PDF/印刷页10](../../sources/SH36735XX%20CV1.0A.pdf#page=10)
- VPD典型=VUV−200mV；Normal欠压Powerdown延时min58.04/typ62.72/max67.52s。模式转换图明确IDLE、SLEEP的欠压Powerdown时间为四倍tPD_UV；其7.1.4正文只泛称tPD_UV，驱动/测试应按图与版本记录保留模式差别。[SH36735XX CV1.0A.pdf，PDF/印刷页11](../../sources/SH36735XX%20CV1.0A.pdf#page=11) [SH36735XX CV1.0A.pdf，PDF/印刷页47](../../sources/SH36735XX%20CV1.0A.pdf#page=47) [SH36735XX CV1.0A.pdf，PDF/印刷页54](../../sources/SH36735XX%20CV1.0A.pdf#page=54)
- SHIP：引脚低至≤0.9V并达到tSHIP进入，只有高≥2.4V（最高VBAT）才退出。新版tSHIP=10/65/220us，旧版是0.8/1/1.2ms，切勿混用。高时弱下拉0.05/0.1/0.15uA@VBAT88V，低时强下拉40/60/80kΩ。[SH36735XX CV1.0A.pdf，PDF/印刷页11](../../sources/SH36735XX%20CV1.0A.pdf#page=11) [SH36735XX CV1.0A.pdf，PDF/印刷页46](../../sources/SH36735XX%20CV1.0A.pdf#page=46) [SH36735XX CV0.2C.pdf，PDF/印刷页46](../../sources/SH36735XX%20CV0.2C.pdf#page=46)

### 3.3 WarmUp、复位、RAM

上电、VCC LVR、退出Powerdown和退出SHIP会进入WarmUp，关闭所有充放电/预放电FET；tWARMUP最大10ms后VCC建立、SPI可正常通信并到Normal，RAM恢复默认值。系统复位置RST1_FLG；但FLAG1描述写明RST1_FLG“不包括软件复位”，其复位表又写1，见疑点清单。软件复位命令还明确复位RAM、VADC、CADC、SPI等所有功能模块。[SH36735XX CV1.0A.pdf，PDF/印刷页11](../../sources/SH36735XX%20CV1.0A.pdf#page=11) [SH36735XX CV1.0A.pdf，PDF/印刷页26](../../sources/SH36735XX%20CV1.0A.pdf#page=26) [SH36735XX CV1.0A.pdf，PDF/印刷页39](../../sources/SH36735XX%20CV1.0A.pdf#page=39) [SH36735XX CV1.0A.pdf，PDF/印刷页46](../../sources/SH36735XX%20CV1.0A.pdf#page=46)

手册把主机接口寄存器称为RAM。仅公开E2P_ERR状态，没有用户EEPROM编程命令、用户校准存储字段或非易失配置保存流程。因此不得承诺主机配置断电保留；上电/真正系统复位后应重新配置并读回。不同休眠唤醒不应一概等同系统复位：IDLE/SLEEP回Normal与Powerdown/SHIP经WarmUp需分开处理。[SH36735XX CV1.0A.pdf，PDF/印刷页10](../../sources/SH36735XX%20CV1.0A.pdf#page=10) [SH36735XX CV1.0A.pdf，PDF/印刷页11](../../sources/SH36735XX%20CV1.0A.pdf#page=11) [SH36735XX CV1.0A.pdf，PDF/印刷页26](../../sources/SH36735XX%20CV1.0A.pdf#page=26) [SH36735XX CV1.0A.pdf，PDF/印刷页41](../../sources/SH36735XX%20CV1.0A.pdf#page=41)

<a id="protection"></a>
## 4. 安全保护：检测、编码、延时、动作与恢复

### 4.1 统一恢复模型

保护标志是锁存历史，FLAG1所有位及FLAG2[7:2]是“读/写0”。先SCONF2.LTCLR=1授权，再对目标标志写0；一次清除后LTCLR自动清0，同时置位/自动清零时MCU置位优先。它们不是W1C。恢复条件不是“电压/温度自然回到一个独立恢复阈值”：这些小节写的是MCU清标志；手册没有给OV/UV等单独恢复电压寄存器。清除仍异常的保护条件可再次触发，不能仅凭清零就认为安全。[SH36735XX CV1.0A.pdf，PDF/印刷页12](../../sources/SH36735XX%20CV1.0A.pdf#page=12) [SH36735XX CV1.0A.pdf，PDF/印刷页13](../../sources/SH36735XX%20CV1.0A.pdf#page=13) [SH36735XX CV1.0A.pdf，PDF/印刷页14](../../sources/SH36735XX%20CV1.0A.pdf#page=14) [SH36735XX CV1.0A.pdf，PDF/印刷页15](../../sources/SH36735XX%20CV1.0A.pdf#page=15) [SH36735XX CV1.0A.pdf，PDF/印刷页31](../../sources/SH36735XX%20CV1.0A.pdf#page=31) [SH36735XX CV1.0A.pdf，PDF/印刷页39](../../sources/SH36735XX%20CV1.0A.pdf#page=39) [SH36735XX CV1.0A.pdf，PDF/印刷页40](../../sources/SH36735XX%20CV1.0A.pdf#page=40)

### 4.2 保护动作表

| 功能 | 使能与触发条件 | 标志 | 动作 | ALARM源 |
|---|---|---|---|---|
| OV过充 | OV_EN，任一电芯>VOV且超tOV | FLAG1.OV_FLG | 关充电FET，清CHG_FET | OV_INT |
| UV过放 | UV_EN，任一电芯<VUV且超tUV | FLAG1.UV_FLG | 关放电及自动预放电，清相应状态 | UV_INT |
| OCD1 | OCD_EN，RS2−RS1>VOCD1且超tOCD1 | FLAG1.OCD1_FLG | 关放电及自动预放电 | OCD_INT |
| OCD2 | OCD_EN，RS2−RS1>VOCD2且超tOCD2 | FLAG1.OCD2_FLG | 关放电及自动预放电 | OCD_INT |
| 短路SC | SC_EN，RS2−RS1>VSC且超tSC | FLAG1.SC_FLG | 关充电、放电及自动预放电 | OCD_INT |
| OCC | OCC_EN，RS2−RS1<−VOCC且超tOCC | FLAG1.OCC_FLG | 关充电 | OCC_INT |
| UTC/OTC | 已使能任一TSn低于TUTC/高于TOTC且超tTEMP | FLAG2.UTC_FLG/OTC_FLG | 关充电 | TEMP_INT |
| UTD/OTD | 已使能任一TSn低于TUTD/高于TOTD且超tTEMP | FLAG2.UTD_FLG/OTD_FLG | 关放电及自动预放电 | TEMP_INT |
| 内部高温 | 内温>TOTI且超tOTI | 未公开专用标志 | 直接Powerdown并关所有FET | 未公开专用ALARM源 |

电压 [SH36735XX CV1.0A.pdf，PDF/印刷页12](../../sources/SH36735XX%20CV1.0A.pdf#page=12)，电流 [SH36735XX CV1.0A.pdf，PDF/印刷页13](../../sources/SH36735XX%20CV1.0A.pdf#page=13) [SH36735XX CV1.0A.pdf，PDF/印刷页14](../../sources/SH36735XX%20CV1.0A.pdf#page=14)，外部/内部温度 [SH36735XX CV1.0A.pdf，PDF/印刷页14](../../sources/SH36735XX%20CV1.0A.pdf#page=14) [SH36735XX CV1.0A.pdf，PDF/印刷页15](../../sources/SH36735XX%20CV1.0A.pdf#page=15)。表中自动预放电指PDSGMOS=1；某些强制关断场景无论该位取值都关闭，见FET章节。

### 4.3 全部阈值编码与源默认值

| 参数/字段 | 编码 | 手册默认 | 电气工作范围/注意 |
|---|---|---|---|
| OV[9:0] | 5×code mV；OVH[1:0]为高2bit，OVL为低8bit | 0x348=4200mV | 电气表保证3.0–4.5V；10bit可编码范围不等于保证范围 |
| UV[9:0] | 5×code mV；UVH[1:0]+UVL | 0x21C=2700mV | 电气表保证1.0–3.5V |
| OCD1V[3:0] | 5×(code+1)mV | 9=50mV | 5–80mV |
| OCD2V[3:0] | 10×(code+1)mV | 9=100mV | 10–160mV |
| SCV[1:0] | 00/01/10/11→2/3/4/6×VOCD2 | 00→2×VOCD2 | 与OCD2阈值耦合，修改OCD2同时改变SC |
| OCCV[4:0] | 1.375×(code+1)mV | 15=22mV | 1.375–44mV，比较负向电压 |
| OWV[3:0] | 160×(code+1)mV | 5=960mV | 断线判据，不是正常欠压门限 |
| OTC / OTD | 512×RT/(10+RT)，RT单位kΩ | 0x96 / 0x76 | 由真实NTC阻温曲线定温度 |
| UTC / UTD | 512×(RT/(10+RT)−0.5)，RT单位kΩ | 0x76 / 0x9E | 不可误用高温公式；手册未指定取整规则 |

OV/UV [SH36735XX CV1.0A.pdf，PDF/印刷页36](../../sources/SH36735XX%20CV1.0A.pdf#page=36) [SH36735XX CV1.0A.pdf，PDF/印刷页37](../../sources/SH36735XX%20CV1.0A.pdf#page=37)；OCD/SC/OCC [SH36735XX CV1.0A.pdf，PDF/印刷页37](../../sources/SH36735XX%20CV1.0A.pdf#page=37) [SH36735XX CV1.0A.pdf，PDF/印刷页38](../../sources/SH36735XX%20CV1.0A.pdf#page=38)；温度公式 [SH36735XX CV1.0A.pdf，PDF/印刷页19](../../sources/SH36735XX%20CV1.0A.pdf#page=19)、默认 [SH36735XX CV1.0A.pdf，PDF/印刷页38](../../sources/SH36735XX%20CV1.0A.pdf#page=38) [SH36735XX CV1.0A.pdf，PDF/印刷页39](../../sources/SH36735XX%20CV1.0A.pdf#page=39)；OWV [SH36735XX CV1.0A.pdf，PDF/印刷页35](../../sources/SH36735XX%20CV1.0A.pdf#page=35)；电气范围 [SH36735XX CV1.0A.pdf，PDF/印刷页49](../../sources/SH36735XX%20CV1.0A.pdf#page=49)。分流阈值转换为电流I[A]=V阈值[V]/Rsense[Ω]是工程换算，必须代入实际阻值，不在此指定任何保护电流。

### 4.4 完整延时编码

| code | OVT / OCD1T / OCCT（s） | UVT（s） | PDSGT（s） | WDT（s，仅2bit） | CADCT（s，仅2bit、IDLE） |
|---|---:|---:|---:|---:|---:|
| 0 | 0.14 | 0.49 | 0.21 | 32.34 | 4 |
| 1 | 0.28 | 0.77 | 0.28 | 15.68 | 32 |
| 2 | 0.49 | 0.98 | 0.42 | 7.84 | 64 |
| 3 | 0.98 | 1.47 | 0.49 | 3.92 | 256 |
| 4 | 2.03 | 2.03 | 0.63 | 不适用 | 不适用 |
| 5 | 3.01 | 3.01 | 0.98 | 不适用 | 不适用 |
| 6 | 4.97 | 4.97 | 2.03 | 不适用 | 不适用 |
| 7 | 10.01 | 10.01 | 3.01 | 不适用 | 不适用 |

来源：PDSGT [SH36735XX CV1.0A.pdf，PDF/印刷页32](../../sources/SH36735XX%20CV1.0A.pdf#page=32)，WDT [SH36735XX CV1.0A.pdf，PDF/印刷页34](../../sources/SH36735XX%20CV1.0A.pdf#page=34)，CADCT [SH36735XX CV1.0A.pdf，PDF/印刷页35](../../sources/SH36735XX%20CV1.0A.pdf#page=35)，OV/UV/OCD1/OCC [SH36735XX CV1.0A.pdf，PDF/印刷页36](../../sources/SH36735XX%20CV1.0A.pdf#page=36) [SH36735XX CV1.0A.pdf，PDF/印刷页37](../../sources/SH36735XX%20CV1.0A.pdf#page=37) [SH36735XX CV1.0A.pdf，PDF/印刷页38](../../sources/SH36735XX%20CV1.0A.pdf#page=38)。OCD2T[3:0]单独公式t=25×(code+1)ms，默认code3=100ms，推算最大400ms；电气表却列最大0.425s，属于源内不一致，不能创造第17档。[SH36735XX CV1.0A.pdf，PDF/印刷页37](../../sources/SH36735XX%20CV1.0A.pdf#page=37) [SH36735XX CV1.0A.pdf，PDF/印刷页49](../../sources/SH36735XX%20CV1.0A.pdf#page=49)

SCT[3:0]按code 0..15顺序为：0、32、64、96、128、192、224、256、288、320、384、448、480、512、544、576us；默认code7=256us。不是统一32us步进。0档只表示没有配置短路保护延时，仍有检测/传播/驱动的物理响应。[SH36735XX CV1.0A.pdf，PDF/印刷页37](../../sources/SH36735XX%20CV1.0A.pdf#page=37) [SH36735XX CV1.0A.pdf，PDF/印刷页38](../../sources/SH36735XX%20CV1.0A.pdf#page=38) [SH36735XX CV1.0A.pdf，PDF/印刷页49](../../sources/SH36735XX%20CV1.0A.pdf#page=49)

IDLE下OV、UV、内部/外部温度、均衡超时、欠压Powerdown的延时是Normal四倍；OCD1/OCC在尚未退出IDLE时也是四倍；不影响WDT和预放电时间。未声明OCD2/SC四倍，不得自作放大。[SH36735XX CV1.0A.pdf，PDF/印刷页20](../../sources/SH36735XX%20CV1.0A.pdf#page=20)

### 4.5 精度与固定温度保护参数

- OV/UV延时偏差min=−(70ms+设定t×6%)、max=120ms+设定t×6%；OCD1/OCC偏差min=−(70ms+t×6%)、max=t×6%；OCD2偏差min=−2.5ms−t×6%、max=t×6%。[SH36735XX CV1.0A.pdf，PDF/印刷页49](../../sources/SH36735XX%20CV1.0A.pdf#page=49)
- OCD2电压精度：−40..85℃，门限<100mV为±5mV，≥100mV为±5%；SC精度门限<100mV为±10mV，≥100mV为±10%。SC延时误差0..64us，测试输入VSC+15%×VSC；旧版上限32us。[SH36735XX CV1.0A.pdf，PDF/印刷页49](../../sources/SH36735XX%20CV1.0A.pdf#page=49) [SH36735XX CV0.2C.pdf，PDF/印刷页49](../../sources/SH36735XX%20CV0.2C.pdf#page=49)
- 手册温度设定范围：OTC 40..70℃、UTC −20..10℃、OTD45..80℃、UTD−40..10℃，列为1℃一档；真实代码仍由NTC公式和阻温关系获得，不是寄存器每增1就固定增1℃。外温延时Normal min1.84/typ2.94/max4.16s。内部温度阈值min105/typ115/max125℃，Normal延时min0.92/typ1.96/max3.12s。[SH36735XX CV1.0A.pdf，PDF/印刷页19](../../sources/SH36735XX%20CV1.0A.pdf#page=19) [SH36735XX CV1.0A.pdf，PDF/印刷页49](../../sources/SH36735XX%20CV1.0A.pdf#page=49)

<a id="fet-watchdog"></a>
## 5. FET、预放电、强制导通与看门狗

### 5.1 驱动决策层次

复位时CHGMOS=DSGMOS=PDSGMOS=0，通常需要MCU重新配置开FET；但MOS_EN复位为1，反方向电流强制导通机制可能覆盖CHGMOS/DSGMOS=0。不要将写0等同于在所有状态下的隔离保证。[SH36735XX CV1.0A.pdf，PDF/印刷页17](../../sources/SH36735XX%20CV1.0A.pdf#page=17) [SH36735XX CV1.0A.pdf，PDF/印刷页31](../../sources/SH36735XX%20CV1.0A.pdf#page=31) [SH36735XX CV1.0A.pdf，PDF/印刷页34](../../sources/SH36735XX%20CV1.0A.pdf#page=34)

正常充电允许条件：CHGMOS=1、没有OV/OCC/UTC/OTC/SC/内部高温、无WDT溢出或仍在溢出后630ms内、非SLEEP/Powerdown/SHIP。正常放电允许条件：DSGMOS=1、没有UV/OCD1/OCD2/SC/UTD/OTD/内部高温、同样的WDT/模式条件。[SH36735XX CV1.0A.pdf，PDF/印刷页17](../../sources/SH36735XX%20CV1.0A.pdf#page=17) [SH36735XX CV1.0A.pdf，PDF/印刷页18](../../sources/SH36735XX%20CV1.0A.pdf#page=18)

低侧CHG/DSG直接反映CHG_FET/DSG_FET；高侧HCHG_FET=PUMP_EN AND CHG_FET、HDSG_FET=PUMP_EN AND DSG_FET。所以低侧状态为1不代表高侧栅极已经导通。启用泵、等泵建立，再配置CHGMOS/DSGMOS是手册流程；泵关时高侧FET关。[SH36735XX CV1.0A.pdf，PDF/印刷页22](../../sources/SH36735XX%20CV1.0A.pdf#page=22) [SH36735XX CV1.0A.pdf，PDF/印刷页24](../../sources/SH36735XX%20CV1.0A.pdf#page=24)

MOS_EN=1时，因保护关闭的充电FET若检测到放电可强制打开（WDT关闭例外）；关闭的放电FET若检测到充电可强制打开（OCD2、SC、WDT关闭例外）。7.12简化恢复条件没有重复全部例外，采用7.10更具体的说明并保留两处来源。[SH36735XX CV1.0A.pdf，PDF/印刷页17](../../sources/SH36735XX%20CV1.0A.pdf#page=17) [SH36735XX CV1.0A.pdf，PDF/印刷页18](../../sources/SH36735XX%20CV1.0A.pdf#page=18)

### 5.2 预放电时序

PDSGMOS=1：内部控制，在开启放电FET前先开预放电，持续tPDSGON后开放电，再延时tPDSGAD后关预放电；tPDSGAD为70/140/210ms，tPDSGON按PDSGT表选择。UV/OCD/SC/UTD/OTD/内部高温/WDT、DSGMOS=0、SLEEP/Powerdown/SHIP、VCC LVR都会按说明关自动预放电。[SH36735XX CV1.0A.pdf，PDF/印刷页17](../../sources/SH36735XX%20CV1.0A.pdf#page=17) [SH36735XX CV1.0A.pdf，PDF/印刷页18](../../sources/SH36735XX%20CV1.0A.pdf#page=18) [SH36735XX CV1.0A.pdf，PDF/印刷页32](../../sources/SH36735XX%20CV1.0A.pdf#page=32) [SH36735XX CV1.0A.pdf，PDF/印刷页48](../../sources/SH36735XX%20CV1.0A.pdf#page=48)

PDSGMOS=0：MCU置PDSG_CTL=1触发预放电，到tPDSGON自动关且清PDSG_CTL；MCU清PDSG_CTL也关闭。WarmUp、Powerdown、SHIP、LVR等强制场景无论PDSGMOS都关。WDT关预放电在7.9与8.2描述不一致（前者限定PDSGMOS=1，后者不管PDSGMOS），不得依靠手动模式绕开WDT，需厂商确认。[SH36735XX CV1.0A.pdf，PDF/印刷页10](../../sources/SH36735XX%20CV1.0A.pdf#page=10) [SH36735XX CV1.0A.pdf，PDF/印刷页11](../../sources/SH36735XX%20CV1.0A.pdf#page=11) [SH36735XX CV1.0A.pdf，PDF/印刷页17](../../sources/SH36735XX%20CV1.0A.pdf#page=17) [SH36735XX CV1.0A.pdf，PDF/印刷页22](../../sources/SH36735XX%20CV1.0A.pdf#page=22)

### 5.3 WDT时间线和通信喂狗

WDT_EN复位0；WDT[1:0]超时32.34/15.68/7.84/3.92s。有效SPI读/写/复位命令重置递减计数器；不能把无效CRC包、空时钟或CS活动当作已确认喂狗。溢出时置WDT_FLG、按WDT_INT发ALARM；630ms后关FET；再630ms后RESET开漏输出低脉冲；从溢出起9.8s仍不清标志则Powerdown。RESET低脉宽10/20/30ms。清WDT_FLG也须LTCLR许可。IDLE不改变WDT相关时间。[SH36735XX CV1.0A.pdf，PDF/印刷页17](../../sources/SH36735XX%20CV1.0A.pdf#page=17) [SH36735XX CV1.0A.pdf，PDF/印刷页20](../../sources/SH36735XX%20CV1.0A.pdf#page=20) [SH36735XX CV1.0A.pdf，PDF/印刷页22](../../sources/SH36735XX%20CV1.0A.pdf#page=22) [SH36735XX CV1.0A.pdf，PDF/印刷页34](../../sources/SH36735XX%20CV1.0A.pdf#page=34) [SH36735XX CV1.0A.pdf，PDF/印刷页46](../../sources/SH36735XX%20CV1.0A.pdf#page=46)

<a id="measurements"></a>
## 6. ADC结果、单位、符号、时效与校准

### 6.1 完整采样数据区

| 数据 | 高/低地址 | 转换与单位 | 更新语义 |
|---|---|---|---|
| TEMP1..4 | 0x5D/5E、5F/60、61/62、63/64 | RT[kΩ]=10×code/(32768−code)，再查该NTC阻温曲线 | Normal每0.98s温度采样；TSn_EN=0仅关保护不关采样 |
| TEMPI | 0x65/66 | T[℃]=0.612×(90×code/26214.4−56.25)/0.109+41 | 内部温度，Normal0.98s时序；SLEEP仍采 |
| CUR | 0x67/68 | I[mA]=100×code/(29127×Rsense[Ω]) | VADC电流，Normal70ms、IDLE280ms |
| CELL1..20 | CELLnH=0x69+2(n−1)，CELLnL=高地址+1 | Vcell[mV]=code×5/32 | Normal70ms、IDLE280ms；未启用CELL清0 |
| CADCD | 0x91/92 | I[mA]=100×code/(29127×Rsense[Ω]) | Normal250ms；IDLE每4s转换，按CADCT平均输出 |
| VTOP | 0x93/94 | B+[mV]=code×5/32×25 | 最高物理VC端的总压 |
| VCHGR | 0x95/96 | C+[mV]=code×5/32×25 | CRLD_EN=01才开C+通道，Normal/IDLE |
| OWDH/M/L | 0x97/98/99 | 位图，每bit对应电芯，非ADC码 | 两次奇偶触发完成整轮 |

公式从原图检查，来源 [SH36735XX CV1.0A.pdf，PDF/印刷页19](../../sources/SH36735XX%20CV1.0A.pdf#page=19) [SH36735XX CV1.0A.pdf，PDF/印刷页21](../../sources/SH36735XX%20CV1.0A.pdf#page=21)；地址和数据说明 [SH36735XX CV1.0A.pdf，PDF/印刷页29](../../sources/SH36735XX%20CV1.0A.pdf#page=29) [SH36735XX CV1.0A.pdf，PDF/印刷页30](../../sources/SH36735XX%20CV1.0A.pdf#page=30) [SH36735XX CV1.0A.pdf，PDF/印刷页42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) [SH36735XX CV1.0A.pdf，PDF/印刷页43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) [SH36735XX CV1.0A.pdf，PDF/印刷页44](../../sources/SH36735XX%20CV1.0A.pdf#page=44)；时序 [SH36735XX CV1.0A.pdf，PDF/印刷页20](../../sources/SH36735XX%20CV1.0A.pdf#page=20)；C+使能 [SH36735XX CV1.0A.pdf，PDF/印刷页23](../../sources/SH36735XX%20CV1.0A.pdf#page=23) [SH36735XX CV1.0A.pdf，PDF/印刷页32](../../sources/SH36735XX%20CV1.0A.pdf#page=32)。表中地址公式仅对物理存在且已启用的通道有测量意义。

### 6.2 字节拼装与符号必须分开

高字节在较低地址，原始16位字先拼成(high<<8)|low。VADC章节将转换值称为“有符号16bit”；CUR.15和CADCD.15又明确“1表示放电、0表示充电”。手册未明确给出二进制负数是补码、原码还是其他传输表示，也未给负电流数值示例。JSON因此signed_binary_encoding=null，不能仅凭常见AFE习惯直接把两字节cast为int16_t后声称验证完毕。必须结合已知正/负微小差分输入或厂商示例确认数值解释。[SH36735XX CV1.0A.pdf，PDF/印刷页19](../../sources/SH36735XX%20CV1.0A.pdf#page=19) [SH36735XX CV1.0A.pdf，PDF/印刷页42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) [SH36735XX CV1.0A.pdf，PDF/印刷页43](../../sources/SH36735XX%20CV1.0A.pdf#page=43)

特别注意：保护检测用RS2−RS1正向判放电、负向判充电，而数据最高位却标“1放电”。方向约定与应用对外“充电正/放电正”的约定也不应混为一谈；建议应用层独立设置方向映射，保留raw值和实测验证记录。[SH36735XX CV1.0A.pdf，PDF/印刷页13](../../sources/SH36735XX%20CV1.0A.pdf#page=13) [SH36735XX CV1.0A.pdf，PDF/印刷页14](../../sources/SH36735XX%20CV1.0A.pdf#page=14) [SH36735XX CV1.0A.pdf，PDF/印刷页22](../../sources/SH36735XX%20CV1.0A.pdf#page=22) [SH36735XX CV1.0A.pdf，PDF/印刷页23](../../sources/SH36735XX%20CV1.0A.pdf#page=23) [SH36735XX CV1.0A.pdf，PDF/印刷页42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) [SH36735XX CV1.0A.pdf，PDF/印刷页43](../../sources/SH36735XX%20CV1.0A.pdf#page=43)

### 6.3 新鲜度与一致性

VADC每tcycle结束置FLAG2.VADC_FLG；CADC完成更新置CADC_FLG；读取FLAG2会清这两位。ALARM只是低脉冲，读取其他状态不能替代抓取ADC完成历史。TEMP没有单独数据就绪位，VADC_FLG为1也不代表所有温度通道刚在本周期更新。Normal电压/电流/B+/C+每70ms，温度每0.98s；IDLE慢四倍的时序应纳入上层时效判断。[SH36735XX CV1.0A.pdf，PDF/印刷页19](../../sources/SH36735XX%20CV1.0A.pdf#page=19) [SH36735XX CV1.0A.pdf，PDF/印刷页20](../../sources/SH36735XX%20CV1.0A.pdf#page=20) [SH36735XX CV1.0A.pdf，PDF/印刷页40](../../sources/SH36735XX%20CV1.0A.pdf#page=40)

CADC Normal4Hz；IDLE固定1/4Hz转换，CADCT=00/01/10/11结果更新4/32/64/256s，结果为该窗口平均值。例：32s窗口为8个4s转换平均，不能按瞬时电流解读。复位后的零寄存器也不是有效零电流，先等首次完成。[SH36735XX CV1.0A.pdf，PDF/印刷页21](../../sources/SH36735XX%20CV1.0A.pdf#page=21) [SH36735XX CV1.0A.pdf，PDF/印刷页35](../../sources/SH36735XX%20CV1.0A.pdf#page=35) [SH36735XX CV1.0A.pdf，PDF/印刷页43](../../sources/SH36735XX%20CV1.0A.pdf#page=43)

手册未明确保证多寄存器快照锁存/跨字节原子性，也未规定读期间更新行为。工程建议：一次连续读高低字节，校验整帧CRC，记录读取时间/模式/就绪标志；关键场景重复读或采用经验证的同步策略；发生SPI/ADC复位后丢弃旧时效。它只是风险控制建议，不构成手册原子性承诺。

### 6.4 转换精度与校准边界

- 单体VADC输入0..5V；1..4.5V测量精度25℃±5mV、−20..60℃±15mV、−40..85℃±20mV；B+/C+在VBAT88V、−40..85℃为±1V。[SH36735XX CV1.0A.pdf，PDF/印刷页47](../../sources/SH36735XX%20CV1.0A.pdf#page=47)
- 外部温度精度25℃±1℃、−40..85℃±2℃；内部温度±10℃。外部NTC自身公差、布置误差不由这些值自动覆盖。[SH36735XX CV1.0A.pdf，PDF/印刷页47](../../sources/SH36735XX%20CV1.0A.pdf#page=47)
- VADC电流输入±100mV；−40..85℃，|VIN|在(0,2]mV精度±0.15mV、(2,10]±0.5mV、(10,50]±1mV、(50,100]±2mV。[SH36735XX CV1.0A.pdf，PDF/印刷页47](../../sources/SH36735XX%20CV1.0A.pdf#page=47)
- CADC输入±100mV；Normal转换237.5/250/262.5ms；INL和失调分别典型±3LSB，增益误差最大±1%FSR（−40..85℃，FSR=±100mV）。手册LSB=112.5mV/2^15≈3.43uV，不能误读成112.5mV/(2^16−1)。[SH36735XX CV1.0A.pdf，PDF/印刷页47](../../sources/SH36735XX%20CV1.0A.pdf#page=47) [SH36735XX CV1.0A.pdf，PDF/印刷页48](../../sources/SH36735XX%20CV1.0A.pdf#page=48)
- 未公开用户增益/偏移校准寄存器和内部校准重触发命令。工程上可在MCU端对实际Rsense与零偏校正，但此参考不虚构厂商校准流程或校准系数。E2P_ERR=1应作为硬件数据异常处理，手册未给修复步骤。[SH36735XX CV1.0A.pdf，PDF/印刷页28](../../sources/SH36735XX%20CV1.0A.pdf#page=28) [SH36735XX CV1.0A.pdf，PDF/印刷页29](../../sources/SH36735XX%20CV1.0A.pdf#page=29) [SH36735XX CV1.0A.pdf，PDF/印刷页30](../../sources/SH36735XX%20CV1.0A.pdf#page=30) [SH36735XX CV1.0A.pdf，PDF/印刷页41](../../sources/SH36735XX%20CV1.0A.pdf#page=41)

<a id="diagnostics-balance"></a>
## 7. 断线检测与均衡

### 7.1 断线状态机

置OWD_EN，再OWD_TRG=1触发一次。硬件清触发位，奇/偶交替采样，转换完成置OWD_FLG；OWD_IND=1表示奇数节、0表示偶数节，只有OWD_FLG=1时有效；按OWD_INT发ALARM。对应断线采样电压低于OWV则置OWDn，否则清；读FLAG3清OWD_FLG，所以必须保存同一次读出的IND。重复触发两次获得整轮。[SH36735XX CV1.0A.pdf，PDF/印刷页16](../../sources/SH36735XX%20CV1.0A.pdf#page=16) [SH36735XX CV1.0A.pdf，PDF/印刷页32](../../sources/SH36735XX%20CV1.0A.pdf#page=32) [SH36735XX CV1.0A.pdf，PDF/印刷页40](../../sources/SH36735XX%20CV1.0A.pdf#page=40) [SH36735XX CV1.0A.pdf，PDF/印刷页41](../../sources/SH36735XX%20CV1.0A.pdf#page=41) [SH36735XX CV1.0A.pdf，PDF/印刷页44](../../sources/SH36735XX%20CV1.0A.pdf#page=44)

CV1.0A新增示例写IND=0用0x00055555获取偶数、IND=1用0x000AAAAA获取奇数；但按OWDL bit0=OWD1的寄存器映射，0x55555选1/3/5…，0xAAAAA选2/4/6…。这是源内冲突，不在JSON中默默换成“正确代码”。验证前应保留原始IND/OWD位图与电芯编号，按明确映射逐位测试，禁止把这个示例直接用于安全断线判决。[SH36735XX CV1.0A.pdf，PDF/印刷页16](../../sources/SH36735XX%20CV1.0A.pdf#page=16) [SH36735XX CV1.0A.pdf，PDF/印刷页30](../../sources/SH36735XX%20CV1.0A.pdf#page=30) [SH36735XX CV1.0A.pdf，PDF/印刷页44](../../sources/SH36735XX%20CV1.0A.pdf#page=44)

OWV=160×(code+1)mV，默认960mV；它比较特定断线检测时序的采样值，不可用正常CELL阈值检测代替。手册没有提供完整断线故障树、线束开路定位唯一性、转换总耗时或一次断线触发后的普通CELL数据污染/恢复保证，软件应显式标记诊断进行中的数据质量。[SH36735XX CV1.0A.pdf，PDF/印刷页16](../../sources/SH36735XX%20CV1.0A.pdf#page=16) [SH36735XX CV1.0A.pdf，PDF/印刷页35](../../sources/SH36735XX%20CV1.0A.pdf#page=35)

### 7.2 均衡控制

BALANCEH/M/L地址0x55..57，低20位CB1..20映射电芯，H高四位保留。MCU任意CBn=1请求该回路，BSTATUS2.BAL表示至少一节实际均衡；全部关闭后BAL清0。Normal持续30.38s后自动关且清BALANCE全部位；任何对BALANCE写操作重启30.38s计时；IDLE均衡持续时间四倍。[SH36735XX CV1.0A.pdf，PDF/印刷页16](../../sources/SH36735XX%20CV1.0A.pdf#page=16) [SH36735XX CV1.0A.pdf，PDF/印刷页20](../../sources/SH36735XX%20CV1.0A.pdf#page=20) [SH36735XX CV1.0A.pdf，PDF/印刷页39](../../sources/SH36735XX%20CV1.0A.pdf#page=39) [SH36735XX CV1.0A.pdf，PDF/印刷页41](../../sources/SH36735XX%20CV1.0A.pdf#page=41)

内部奇偶交替：Normal图示两段70ms采样后350ms奇数均衡，再两段70ms后350ms偶数均衡；新版图注在相应阶段补明电压/电流/温度采集。此机制不能被当成“任意组合连续直流放电”。均衡内阻75/250/400Ω，测试单体3.7V；应结合外部支路电阻与占空比算实际电流、热耗，不采用典型内阻保证最坏值。[SH36735XX CV1.0A.pdf，PDF/印刷页16](../../sources/SH36735XX%20CV1.0A.pdf#page=16) [SH36735XX CV1.0A.pdf，PDF/印刷页48](../../sources/SH36735XX%20CV1.0A.pdf#page=48)

工程建议：保护/温度/模式变化时重新评估均衡请求；不要通过后台无条件周期写寄存器意外延长均衡；开始新一轮时检查BAL、已启用电芯范围、温度和单体有效性。手册未公开主动均衡、自动压差选择或自动SOC均衡算法。

<a id="interfaces"></a>
## 8. ALARM、RESET、充放电与充电器/负载检测

ALARM正常开漏释放，必须外部上拉；事件被对应中断使能允许时发低脉冲，宽min0.8/typ1/max1.2ms。不是一直保持到所有标志清零的水平中断。OWV/ALARMH低4位管LOADON/LOADOFF/VADC/CADC，ALARML管WK/WDT/OWD/TEMP/OCC/OCD/UV/OV。默认ALARMH低4位0111、ALARML0xFF，不要在修改OWV时覆盖低位中断配置。[SH36735XX CV1.0A.pdf，PDF/印刷页22](../../sources/SH36735XX%20CV1.0A.pdf#page=22) [SH36735XX CV1.0A.pdf，PDF/印刷页35](../../sources/SH36735XX%20CV1.0A.pdf#page=35) [SH36735XX CV1.0A.pdf，PDF/印刷页36](../../sources/SH36735XX%20CV1.0A.pdf#page=36) [SH36735XX CV1.0A.pdf，PDF/印刷页47](../../sources/SH36735XX%20CV1.0A.pdf#page=47)

RESET是开漏输出，WDT故障序列可复位外部MCU；不要用MCU输出强驱此脚尝试复位AFE。[SH36735XX CV1.0A.pdf，PDF/印刷页4](../../sources/SH36735XX%20CV1.0A.pdf#page=4) [SH36735XX CV1.0A.pdf，PDF/印刷页22](../../sources/SH36735XX%20CV1.0A.pdf#page=22)

### 8.1 充放电状态识别

VADC方式：VCD1=137.5×CDV+192.5uV，CDV默认4即742.5uV。RS2−RS1≤−VCD1持续>4×tcycle进入充电；≥VCD1持续>4×tcycle进入放电。回到门限内持续>2×tcycle且比较器未检出对应方向才退出。比较器方式：≤−VCD2或≥VCD2持续>tCD直接置对应状态并清另一状态；VCD2 min3/typ5/max7mV，精度±2mV@−40..85℃，tCD min1/typ5/max9ms。[SH36735XX CV1.0A.pdf，PDF/印刷页22](../../sources/SH36735XX%20CV1.0A.pdf#page=22) [SH36735XX CV1.0A.pdf，PDF/印刷页23](../../sources/SH36735XX%20CV1.0A.pdf#page=23) [SH36735XX CV1.0A.pdf，PDF/印刷页35](../../sources/SH36735XX%20CV1.0A.pdf#page=35) [SH36735XX CV1.0A.pdf，PDF/印刷页50](../../sources/SH36735XX%20CV1.0A.pdf#page=50)

状态检测会退出IDLE并影响MOS_EN强制导通，不只是给UI显示的电流方向位。[SH36735XX CV1.0A.pdf，PDF/印刷页9](../../sources/SH36735XX%20CV1.0A.pdf#page=9) [SH36735XX CV1.0A.pdf，PDF/印刷页10](../../sources/SH36735XX%20CV1.0A.pdf#page=10) [SH36735XX CV1.0A.pdf，PDF/印刷页17](../../sources/SH36735XX%20CV1.0A.pdf#page=17)

### 8.2 负载和C+互斥选择

Normal/IDLE时CRLD_EN=10开DSGD负载状态检测，内部向VBAT上拉；VBAT−DSGD≥VDIFF且持续>tLOAD视为连接，置LOADON清LOADOFF及未连接计数；压差<VDIFF且超时视为未连接，反向置位/清计数。对应ALARM可单独使能。CRLD_EN=01开CHGD C+采样；00/11关闭，不能用同一个编码同时开两功能。[SH36735XX CV1.0A.pdf，PDF/印刷页23](../../sources/SH36735XX%20CV1.0A.pdf#page=23) [SH36735XX CV1.0A.pdf，PDF/印刷页32](../../sources/SH36735XX%20CV1.0A.pdf#page=32)

RLD=0：上拉40/60/80uA、压差阈值0.8/1.5/2.2V；RLD=1：上拉300/500/700uA、阈值0.9/1.8/2.6V；tLOAD55/60/65ms（−40..85℃）。[SH36735XX CV1.0A.pdf，PDF/印刷页50](../../sources/SH36735XX%20CV1.0A.pdf#page=50)

SLEEP下LD_WK=01/10分别由连接/未连接条件唤醒，仍使用RLD上拉和同样压差/延时。CHGD C+采样内部下拉RCHGD1为0.6/1/1.4MΩ；SLEEP充电唤醒已使能或Powerdown时改RCHGD2=30/50/70kΩ。CHGD超过VCHGD=0.9/1.5/2.1V持续tCHGD=35/60/85ms时唤醒；这些是CHGD管脚电平，不是自动等于外部充电器标称电压。CHGD/DSGD禁用检测时漏电最大50nA，需遵守p50测试条件。[SH36735XX CV1.0A.pdf，PDF/印刷页23](../../sources/SH36735XX%20CV1.0A.pdf#page=23) [SH36735XX CV1.0A.pdf，PDF/印刷页24](../../sources/SH36735XX%20CV1.0A.pdf#page=24) [SH36735XX CV1.0A.pdf，PDF/印刷页50](../../sources/SH36735XX%20CV1.0A.pdf#page=50)

<a id="spi"></a>
## 9. SPI帧、CRC、时序与读写副作用

### 9.1 物理接口

SPI只支持从机、MSB先行、CPOL=1、CPHA=1（Mode3），最高1MHz。CS低选择；CS高时SDO高阻，CS低但无通信时SDO高；SDI/SCK内部上拉始终开启。同一时刻只选择一个从机避免SDO冲突。CS在整帧时序图中保持低；不要把正文“每字节保持低”误解为允许逐字节翻转CS而不重启帧。[SH36735XX CV1.0A.pdf，PDF/印刷页25](../../sources/SH36735XX%20CV1.0A.pdf#page=25) [SH36735XX CV1.0A.pdf，PDF/印刷页26](../../sources/SH36735XX%20CV1.0A.pdf#page=26)

### 9.2 帧的逐字节定义

写寄存器：只写0x40..0x59，一次固定1字节。

| 字节位置 | 0 | 1 | 2 | 3 | 4 |
|---|---|---|---|---|---|
| MCU→SDI | 0x01 | 地址 | 数据 | CRC8 | 0x00 dummy |
| AFE→SDO | 0xFF无效 | 0x01回显 | 地址回显 | 数据回显 | 成功0xA5 / 失败0xFF |

原图没有“写长度”字节；9.5.4 CRC正文却列“写数据长度”参与CRC。两版均存在此冲突；本表忠实转录图9，CRC写入覆盖集合不能在未验证前当成无歧义。推荐实物/厂商示例确认究竟为CRC(01,address,data)，不得凭正文擅加长度破坏图示帧。CRC失败时手册说明不更新寄存器。[SH36735XX CV1.0A.pdf，PDF/印刷页26](../../sources/SH36735XX%20CV1.0A.pdf#page=26) [SH36735XX CV0.2C.pdf，PDF/印刷页26](../../sources/SH36735XX%20CV0.2C.pdf#page=26)

读寄存器：可读0x40..0x99，发送N为字节数（不含尾CRC）。

| 相位 | MCU→SDI | AFE→SDO |
|---|---|---|
| 字节0 | 0x02 | 0xFF |
| 字节1 | 起始地址 | 0x02 |
| 字节2 | N | 起始地址 |
| 字节3 | 0x00 | N |
| 接下来N字节 | 0x00 dummy | Data1..DataN |
| 末字节 | 0x00 dummy | CRC8 |

从图10推导整帧需要N+5个字节时钟；读CRC正文明确覆盖AFE输出的0xFF、0x02、地址、N、N个数据。该推导与来源事实分开：驱动需确认实际总线捕获。手册没有明确N=0、地址越界、跨0x99或非法地址行为，应在主机侧拒绝而非试探生产设备。[SH36735XX CV1.0A.pdf，PDF/印刷页26](../../sources/SH36735XX%20CV1.0A.pdf#page=26) [SH36735XX CV1.0A.pdf，PDF/印刷页27](../../sources/SH36735XX%20CV1.0A.pdf#page=27)

软件复位帧：SDI=[0x0B,0xBB,0xCC,CRC8,0x00]，SDO=[0xFF,0x0B,0xBB,0xCC,ACK]；ACK成功0xA5、失败0xFF。复位是高副作用操作，不能用作普通“探活”。[SH36735XX CV1.0A.pdf，PDF/印刷页26](../../sources/SH36735XX%20CV1.0A.pdf#page=26)

### 9.3 CRC具体已知/未知

多项式x^8+x^2+x+1、初始值0x00；省略最高项的常用数值写法为0x07（这是代数表示）。手册未显式给反射选项、最终异或值或完整代码/测试向量，因此JSON/驱动规格不能把它们编造为厂商保证。MSB传输不自动证明CRC实现的全部参数。读帧起始0xFF不能漏算。[SH36735XX CV1.0A.pdf，PDF/印刷页26](../../sources/SH36735XX%20CV1.0A.pdf#page=26) [SH36735XX CV1.0A.pdf，PDF/印刷页27](../../sources/SH36735XX%20CV1.0A.pdf#page=27)

工程建议：校验回显、长度、CRC及ACK；失败不更新软件影子配置；对已知配置进行读回确认。读FLAG2/FLAG3有硬件清位副作用，不能自动重试并假设返回标志仍与第一次一致；在CRC失败时保留“可能已经消费事件”的不确定性。写复位、模式切换、清标志、OWD触发、均衡重置计时等也不可一律按幂等操作重发。

### 9.4 全部SPI时序参数

| 参数 | min / typ / max | 含义 |
|---|---|---|
| TLS1 / TLS2 | 100 / 未给 / 未给 ns | CS建立/保持 |
| TCL / TCH | 500 / 未给 / 未给 ns | SCK低/高 |
| TSET / THOL | 50 / 未给 / 未给 ns | 数据建立/保持 |
| TVAL1 | 10 / 未给 / 100 ns | CS下降至输出变化 |
| TLZ | 10 / 未给 / 100 ns | 输出禁止时间 |
| TVAL2 | 未给 / 未给 / 100 ns | SCK下降至输出有效 |
| tSPIDIS | 10 / 30 / 50 ns | CS高持续后关闭SPI模块 |
| tSPIRST | 0.9 / 1 / 1.1 s | CS低期间无SCK下降沿时SPI复位 |

时序原图 [SH36735XX CV1.0A.pdf，PDF/印刷页51](../../sources/SH36735XX%20CV1.0A.pdf#page=51)，功能说明 [SH36735XX CV1.0A.pdf，PDF/印刷页27](../../sources/SH36735XX%20CV1.0A.pdf#page=27)。tSPIDIS手册单位确为ns，不能因觉得异常就改为ms；tSPIRST仅SPI模块复位，不等同软件全系统复位。

<a id="registers"></a>
## 10. 寄存器总表与可执行解释

### 10.1 读写规则

地址是字节地址；0x40..0x57为一般读写控制，0x58为FLAG1写0清，0x59混合FLAG2写0清与ADC读清，0x5A..0x99只读。所有保留位都保留在JSON中，不因为表中把保留位标成“读写”就将其视为有定义功能。建议对正常配置使用受控影子值，保留未修改位；不要对读清状态寄存器套普通读改写工具。[SH36735XX CV1.0A.pdf，PDF/印刷页26](../../sources/SH36735XX%20CV1.0A.pdf#page=26) [SH36735XX CV1.0A.pdf，PDF/印刷页28](../../sources/SH36735XX%20CV1.0A.pdf#page=28) [SH36735XX CV1.0A.pdf，PDF/印刷页39](../../sources/SH36735XX%20CV1.0A.pdf#page=39) [SH36735XX CV1.0A.pdf，PDF/印刷页40](../../sources/SH36735XX%20CV1.0A.pdf#page=40)

关键初值：SCONF2=0x50（PD_EN=1、PUMP_EN=1）；SCONF4=0x7F；SCONF5=0x38（MOS_EN/OCC_EN/CADC_EN=1，WDT_EN=0）；SCONF6=0xFF（温度/SC/OCD/UV/OV全部使能）；SCONF7=0x04；OWV/ALARMH=0x57；ALARML=0xFF。复位并非完全“无保护”，也并非“所有FET自动开”。[SH36735XX CV1.0A.pdf，PDF/印刷页31](../../sources/SH36735XX%20CV1.0A.pdf#page=31) [SH36735XX CV1.0A.pdf，PDF/印刷页32](../../sources/SH36735XX%20CV1.0A.pdf#page=32) [SH36735XX CV1.0A.pdf，PDF/印刷页34](../../sources/SH36735XX%20CV1.0A.pdf#page=34) [SH36735XX CV1.0A.pdf，PDF/印刷页35](../../sources/SH36735XX%20CV1.0A.pdf#page=35)

全表以下bit标签按总表原样展示，因此0x92 bit1的CADCD.0重复在视觉表中保留；JSON结构化bitfields采用详细表p43的CADCD.1并显式记下冲突。CELL6H/CELL16H详细表重复.12而总表为.11，采用总表并记录。[SH36735XX CV1.0A.pdf，PDF/印刷页29](../../sources/SH36735XX%20CV1.0A.pdf#page=29) [SH36735XX CV1.0A.pdf，PDF/印刷页30](../../sources/SH36735XX%20CV1.0A.pdf#page=30) [SH36735XX CV1.0A.pdf，PDF/印刷页42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) [SH36735XX CV1.0A.pdf，PDF/印刷页43](../../sources/SH36735XX%20CV1.0A.pdf#page=43)

| 地址 | 名称 | 复位 | 访问 | bit7 → bit0 | CV1.0A来源 |
|---|---|---|---|---|---|
| 0x40 | SCONF1 | 0x00 | RW | PIN.7 / PIN.6 / PIN.5 / PIN.4 / PIN.3 / PIN.2 / PIN.1 / PIN.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 31](../../sources/SH36735XX%20CV1.0A.pdf#page=31) |
| 0x41 | SCONF2 | 0x50 | RW | LTCLR / PD_EN / PD_CTL / PUMP_EN / PDSG_CTL / PDSGMOS / DSGMOS / CHGMOS | [SH36735XX CV1.0A.pdf, PDF/印刷页 31](../../sources/SH36735XX%20CV1.0A.pdf#page=31) |
| 0x42 | SCONF3 | 0x00 | RW | - / CGR_WK / LD_WK.1 / LD_WK.0 / CRLD_EN.1 / CRLD_EN.0 / OWD_EN / OWD_TRG | [SH36735XX CV1.0A.pdf, PDF/印刷页 32](../../sources/SH36735XX%20CV1.0A.pdf#page=32) |
| 0x43 | SCONF4 | 0x7F | RW | PDSGT.2 / PDSGT.1 / PDSGT.0 / CN.4 / CN.3 / CN.2 / CN.1 / CN.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 32](../../sources/SH36735XX%20CV1.0A.pdf#page=32) |
| 0x44 | SCONF5 | 0x38 | RW | - / - / MOS_EN / OCC_EN / CADC_EN / WDT_EN / WDT.1 / WDT.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 34](../../sources/SH36735XX%20CV1.0A.pdf#page=34) |
| 0x45 | SCONF6 | 0xFF | RW | TS4_EN / TS3_EN / TS2_EN / TS1_EN / SC_EN / OCD_EN / UV_EN / OV_EN | [SH36735XX CV1.0A.pdf, PDF/印刷页 34](../../sources/SH36735XX%20CV1.0A.pdf#page=34) |
| 0x46 | SCONF7 | 0x04 | RW | - / RLD / CADCT.1 / CADCT.0 / - / CDV.2 / CDV.1 / CDV.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 35](../../sources/SH36735XX%20CV1.0A.pdf#page=35) |
| 0x47 | OWV/ALARMH | 0x57 | RW | OWV.3 / OWV.2 / OWV.1 / OWV.0 / LOADON_INT / LOADOFF_INT / VADC_INT / CADC_INT | [SH36735XX CV1.0A.pdf, PDF/印刷页 35](../../sources/SH36735XX%20CV1.0A.pdf#page=35) |
| 0x48 | ALARML | 0xFF | RW | WK_INT / WDT_INT / OWD_INT / TEMP_INT / OCC_INT / OCD_INT / UV_INT / OV_INT | [SH36735XX CV1.0A.pdf, PDF/印刷页 35](../../sources/SH36735XX%20CV1.0A.pdf#page=35) |
| 0x49 | OVT/OVH | 0x33 | RW | - / OVT.2 / OVT.1 / OVT.0 / - / - / OV.9 / OV.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 36](../../sources/SH36735XX%20CV1.0A.pdf#page=36) |
| 0x4A | OVL | 0x48 | RW | OV.7 / OV.6 / OV.5 / OV.4 / OV.3 / OV.2 / OV.1 / OV.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 36](../../sources/SH36735XX%20CV1.0A.pdf#page=36) |
| 0x4B | UVT/UVH | 0x22 | RW | - / UVT.2 / UVT.1 / UVT.0 / - / - / UV.9 / UV.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 36](../../sources/SH36735XX%20CV1.0A.pdf#page=36) |
| 0x4C | UVL | 0x1C | RW | UV.7 / UV.6 / UV.5 / UV.4 / UV.3 / UV.2 / UV.1 / UV.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 36](../../sources/SH36735XX%20CV1.0A.pdf#page=36) |
| 0x4D | OCD1V/OCD1T | 0x39 | RW | - / OCD1T.2 / OCD1T.1 / OCD1T.0 / OCD1V.3 / OCD1V.2 / OCD1V.1 / OCD1V.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 37](../../sources/SH36735XX%20CV1.0A.pdf#page=37) |
| 0x4E | OCD2V/OCD2T | 0x39 | RW | OCD2T.3 / OCD2T.2 / OCD2T.1 / OCD2T.0 / OCD2V.3 / OCD2V.2 / OCD2V.1 / OCD2V.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 37](../../sources/SH36735XX%20CV1.0A.pdf#page=37) |
| 0x4F | SCV/SCT | 0x07 | RW | - / - / SCV.1 / SCV.0 / SCT.3 / SCT.2 / SCT.1 / SCT.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 37](../../sources/SH36735XX%20CV1.0A.pdf#page=37) |
| 0x50 | OCCV/OCCT | 0x6F | RW | OCCT.2 / OCCT.1 / OCCT.0 / OCCV.4 / OCCV.3 / OCCV.2 / OCCV.1 / OCCV.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 38](../../sources/SH36735XX%20CV1.0A.pdf#page=38) |
| 0x51 | OTC | 0x96 | RW | OTC.7 / OTC.6 / OTC.5 / OTC.4 / OTC.3 / OTC.2 / OTC.1 / OTC.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 38](../../sources/SH36735XX%20CV1.0A.pdf#page=38) |
| 0x52 | OTD | 0x76 | RW | OTD.7 / OTD.6 / OTD.5 / OTD.4 / OTD.3 / OTD.2 / OTD.1 / OTD.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 38](../../sources/SH36735XX%20CV1.0A.pdf#page=38) |
| 0x53 | UTC | 0x76 | RW | UTC.7 / UTC.6 / UTC.5 / UTC.4 / UTC.3 / UTC.2 / UTC.1 / UTC.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 38](../../sources/SH36735XX%20CV1.0A.pdf#page=38) |
| 0x54 | UTD | 0x9E | RW | UTD.7 / UTD.6 / UTD.5 / UTD.4 / UTD.3 / UTD.2 / UTD.1 / UTD.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 39](../../sources/SH36735XX%20CV1.0A.pdf#page=39) |
| 0x55 | BALANCEH | 0x00 | RW | - / - / - / - / CB20 / CB19 / CB18 / CB17 | [SH36735XX CV1.0A.pdf, PDF/印刷页 39](../../sources/SH36735XX%20CV1.0A.pdf#page=39) |
| 0x56 | BALANCEM | 0x00 | RW | CB16 / CB15 / CB14 / CB13 / CB12 / CB11 / CB10 / CB9 | [SH36735XX CV1.0A.pdf, PDF/印刷页 39](../../sources/SH36735XX%20CV1.0A.pdf#page=39) |
| 0x57 | BALANCEL | 0x00 | RW | CB8 / CB7 / CB6 / CB5 / CB4 / CB3 / CB2 / CB1 | [SH36735XX CV1.0A.pdf, PDF/印刷页 39](../../sources/SH36735XX%20CV1.0A.pdf#page=39) |
| 0x58 | FLAG1 | 0x80 | RW0C | RST1_FLG / WK_FLG / OCC_FLG / SC_FLG / OCD2_FLG / OCD1_FLG / UV_FLG / OV_FLG | [SH36735XX CV1.0A.pdf, PDF/印刷页 39](../../sources/SH36735XX%20CV1.0A.pdf#page=39) |
| 0x59 | FLAG2 | 0x00 | mixed_RW0C_read_clear | OTD_FLG / UTD_FLG / OTC_FLG / UTC_FLG / RST2_FLG / WDT_FLG / VADC_FLG / CADC_FLG | [SH36735XX CV1.0A.pdf, PDF/印刷页 40](../../sources/SH36735XX%20CV1.0A.pdf#page=40) |
| 0x5A | FLAG3 | 0x00 | RO | - / - / - / - / - / - / OWD_IND / OWD_FLG | [SH36735XX CV1.0A.pdf, PDF/印刷页 40](../../sources/SH36735XX%20CV1.0A.pdf#page=40) |
| 0x5B | BSTATUS1 | 0x00 | RO | - / E2P_ERR / HDSG_FET / HCHG_FET / - / PDSG_FET / DSG_FET / CHG_FET | [SH36735XX CV1.0A.pdf, PDF/印刷页 41](../../sources/SH36735XX%20CV1.0A.pdf#page=41) |
| 0x5C | BSTATUS2 | 0x00 | RO | CHGING / DSGING / SLEEP / IDLE / BAL / - / LOADON / LOADOFF | [SH36735XX CV1.0A.pdf, PDF/印刷页 41](../../sources/SH36735XX%20CV1.0A.pdf#page=41) |
| 0x5D | TEMP1H | 0x00 | RO | TEMP1.15 / TEMP1.14 / TEMP1.13 / TEMP1.12 / TEMP1.11 / TEMP1.10 / TEMP1.9 / TEMP1.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x5E | TEMP1L | 0x00 | RO | TEMP1.7 / TEMP1.6 / TEMP1.5 / TEMP1.4 / TEMP1.3 / TEMP1.2 / TEMP1.1 / TEMP1.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x5F | TEMP2H | 0x00 | RO | TEMP2.15 / TEMP2.14 / TEMP2.13 / TEMP2.12 / TEMP2.11 / TEMP2.10 / TEMP2.9 / TEMP2.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x60 | TEMP2L | 0x00 | RO | TEMP2.7 / TEMP2.6 / TEMP2.5 / TEMP2.4 / TEMP2.3 / TEMP2.2 / TEMP2.1 / TEMP2.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x61 | TEMP3H | 0x00 | RO | TEMP3.15 / TEMP3.14 / TEMP3.13 / TEMP3.12 / TEMP3.11 / TEMP3.10 / TEMP3.9 / TEMP3.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x62 | TEMP3L | 0x00 | RO | TEMP3.7 / TEMP3.6 / TEMP3.5 / TEMP3.4 / TEMP3.3 / TEMP3.2 / TEMP3.1 / TEMP3.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x63 | TEMP4H | 0x00 | RO | TEMP4.15 / TEMP4.14 / TEMP4.13 / TEMP4.12 / TEMP4.11 / TEMP4.10 / TEMP4.9 / TEMP4.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x64 | TEMP4L | 0x00 | RO | TEMP4.7 / TEMP4.6 / TEMP4.5 / TEMP4.4 / TEMP4.3 / TEMP4.2 / TEMP4.1 / TEMP4.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x65 | TEMPIH | 0x00 | RO | TEMPI.15 / TEMPI.14 / TEMPI.13 / TEMPI.12 / TEMPI.11 / TEMPI.10 / TEMPI.9 / TEMPI.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x66 | TEMPIL | 0x00 | RO | TEMPI.7 / TEMPI.6 / TEMPI.5 / TEMPI.4 / TEMPI.3 / TEMPI.2 / TEMPI.1 / TEMPI.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x67 | CURH | 0x00 | RO | CUR.15 / CUR.14 / CUR.13 / CUR.12 / CUR.11 / CUR.10 / CUR.9 / CUR.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x68 | CURL | 0x00 | RO | CUR.7 / CUR.6 / CUR.5 / CUR.4 / CUR.3 / CUR.2 / CUR.1 / CUR.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x69 | CELL1H | 0x00 | RO | CELL1.15 / CELL1.14 / CELL1.13 / CELL1.12 / CELL1.11 / CELL1.10 / CELL1.9 / CELL1.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x6A | CELL1L | 0x00 | RO | CELL1.7 / CELL1.6 / CELL1.5 / CELL1.4 / CELL1.3 / CELL1.2 / CELL1.1 / CELL1.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x6B | CELL2H | 0x00 | RO | CELL2.15 / CELL2.14 / CELL2.13 / CELL2.12 / CELL2.11 / CELL2.10 / CELL2.9 / CELL2.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x6C | CELL2L | 0x00 | RO | CELL2.7 / CELL2.6 / CELL2.5 / CELL2.4 / CELL2.3 / CELL2.2 / CELL2.1 / CELL2.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x6D | CELL3H | 0x00 | RO | CELL3.15 / CELL3.14 / CELL3.13 / CELL3.12 / CELL3.11 / CELL3.10 / CELL3.9 / CELL3.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x6E | CELL3L | 0x00 | RO | CELL3.7 / CELL3.6 / CELL3.5 / CELL3.4 / CELL3.3 / CELL3.2 / CELL3.1 / CELL3.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x6F | CELL4H | 0x00 | RO | CELL4.15 / CELL4.14 / CELL4.13 / CELL4.12 / CELL4.11 / CELL4.10 / CELL4.9 / CELL4.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x70 | CELL4L | 0x00 | RO | CELL4.7 / CELL4.6 / CELL4.5 / CELL4.4 / CELL4.3 / CELL4.2 / CELL4.1 / CELL4.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x71 | CELL5H | 0x00 | RO | CELL5.15 / CELL5.14 / CELL5.13 / CELL5.12 / CELL5.11 / CELL5.10 / CELL5.9 / CELL5.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x72 | CELL5L | 0x00 | RO | CELL5.7 / CELL5.6 / CELL5.5 / CELL5.4 / CELL5.3 / CELL5.2 / CELL5.1 / CELL5.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x73 | CELL6H | 0x00 | RO | CELL6.15 / CELL6.14 / CELL6.13 / CELL6.12 / CELL6.11 / CELL6.10 / CELL6.9 / CELL6.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x74 | CELL6L | 0x00 | RO | CELL6.7 / CELL6.6 / CELL6.5 / CELL6.4 / CELL6.3 / CELL6.2 / CELL6.1 / CELL6.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) |
| 0x75 | CELL7H | 0x00 | RO | CELL7.15 / CELL7.14 / CELL7.13 / CELL7.12 / CELL7.11 / CELL7.10 / CELL7.9 / CELL7.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x76 | CELL7L | 0x00 | RO | CELL7.7 / CELL7.6 / CELL7.5 / CELL7.4 / CELL7.3 / CELL7.2 / CELL7.1 / CELL7.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x77 | CELL8H | 0x00 | RO | CELL8.15 / CELL8.14 / CELL8.13 / CELL8.12 / CELL8.11 / CELL8.10 / CELL8.9 / CELL8.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x78 | CELL8L | 0x00 | RO | CELL8.7 / CELL8.6 / CELL8.5 / CELL8.4 / CELL8.3 / CELL8.2 / CELL8.1 / CELL8.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x79 | CELL9H | 0x00 | RO | CELL9.15 / CELL9.14 / CELL9.13 / CELL9.12 / CELL9.11 / CELL9.10 / CELL9.9 / CELL9.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x7A | CELL9L | 0x00 | RO | CELL9.7 / CELL9.6 / CELL9.5 / CELL9.4 / CELL9.3 / CELL9.2 / CELL9.1 / CELL9.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x7B | CELL10H | 0x00 | RO | CELL10.15 / CELL10.14 / CELL10.13 / CELL10.12 / CELL10.11 / CELL10.10 / CELL10.9 / CELL10.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x7C | CELL10L | 0x00 | RO | CELL10.7 / CELL10.6 / CELL10.5 / CELL10.4 / CELL10.3 / CELL10.2 / CELL10.1 / CELL10.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x7D | CELL11H | 0x00 | RO | CELL11.15 / CELL11.14 / CELL11.13 / CELL11.12 / CELL11.11 / CELL11.10 / CELL11.9 / CELL11.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x7E | CELL11L | 0x00 | RO | CELL11.7 / CELL11.6 / CELL11.5 / CELL11.4 / CELL11.3 / CELL11.2 / CELL11.1 / CELL11.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x7F | CELL12H | 0x00 | RO | CELL12.15 / CELL12.14 / CELL12.13 / CELL12.12 / CELL12.11 / CELL12.10 / CELL12.9 / CELL12.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x80 | CELL12L | 0x00 | RO | CELL12.7 / CELL12.6 / CELL12.5 / CELL12.4 / CELL12.3 / CELL12.2 / CELL12.1 / CELL12.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x81 | CELL13H | 0x00 | RO | CELL13.15 / CELL13.14 / CELL13.13 / CELL13.12 / CELL13.11 / CELL13.10 / CELL13.9 / CELL13.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x82 | CELL13L | 0x00 | RO | CELL13.7 / CELL13.6 / CELL13.5 / CELL13.4 / CELL13.3 / CELL13.2 / CELL13.1 / CELL13.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x83 | CELL14H | 0x00 | RO | CELL14.15 / CELL14.14 / CELL14.13 / CELL14.12 / CELL14.11 / CELL14.10 / CELL14.9 / CELL14.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x84 | CELL14L | 0x00 | RO | CELL14.7 / CELL14.6 / CELL14.5 / CELL14.4 / CELL14.3 / CELL14.2 / CELL14.1 / CELL14.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x85 | CELL15H | 0x00 | RO | CELL15.15 / CELL15.14 / CELL15.13 / CELL15.12 / CELL15.11 / CELL15.10 / CELL15.9 / CELL15.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x86 | CELL15L | 0x00 | RO | CELL15.7 / CELL15.6 / CELL15.5 / CELL15.4 / CELL15.3 / CELL15.2 / CELL15.1 / CELL15.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x87 | CELL16H | 0x00 | RO | CELL16.15 / CELL16.14 / CELL16.13 / CELL16.12 / CELL16.11 / CELL16.10 / CELL16.9 / CELL16.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x88 | CELL16L | 0x00 | RO | CELL16.7 / CELL16.6 / CELL16.5 / CELL16.4 / CELL16.3 / CELL16.2 / CELL16.1 / CELL16.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x89 | CELL17H | 0x00 | RO | CELL17.15 / CELL17.14 / CELL17.13 / CELL17.12 / CELL17.11 / CELL17.10 / CELL17.9 / CELL17.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x8A | CELL17L | 0x00 | RO | CELL17.7 / CELL17.6 / CELL17.5 / CELL17.4 / CELL17.3 / CELL17.2 / CELL17.1 / CELL17.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x8B | CELL18H | 0x00 | RO | CELL18.15 / CELL18.14 / CELL18.13 / CELL18.12 / CELL18.11 / CELL18.10 / CELL18.9 / CELL18.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x8C | CELL18L | 0x00 | RO | CELL18.7 / CELL18.6 / CELL18.5 / CELL18.4 / CELL18.3 / CELL18.2 / CELL18.1 / CELL18.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x8D | CELL19H | 0x00 | RO | CELL19.15 / CELL19.14 / CELL19.13 / CELL19.12 / CELL19.11 / CELL19.10 / CELL19.9 / CELL19.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x8E | CELL19L | 0x00 | RO | CELL19.7 / CELL19.6 / CELL19.5 / CELL19.4 / CELL19.3 / CELL19.2 / CELL19.1 / CELL19.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x8F | CELL20H | 0x00 | RO | CELL20.15 / CELL20.14 / CELL20.13 / CELL20.12 / CELL20.11 / CELL20.10 / CELL20.9 / CELL20.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x90 | CELL20L | 0x00 | RO | CELL20.7 / CELL20.6 / CELL20.5 / CELL20.4 / CELL20.3 / CELL20.2 / CELL20.1 / CELL20.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x91 | CADCDH | 0x00 | RO | CADCD.15 / CADCD.14 / CADCD.13 / CADCD.12 / CADCD.11 / CADCD.10 / CADCD.9 / CADCD.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x92 | CADCDL | 0x00 | RO | CADCD.7 / CADCD.6 / CADCD.5 / CADCD.4 / CADCD.3 / CADCD.2 / CADCD.0 / CADCD.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x93 | VTOPH | 0x00 | RO | VTOP.15 / VTOP.14 / VTOP.13 / VTOP.12 / VTOP.11 / VTOP.10 / VTOP.9 / VTOP.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x94 | VTOPL | 0x00 | RO | VTOP.7 / VTOP.6 / VTOP.5 / VTOP.4 / VTOP.3 / VTOP.2 / VTOP.1 / VTOP.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x95 | VCHGRH | 0x00 | RO | VCHGR.15 / VCHGR.14 / VCHGR.13 / VCHGR.12 / VCHGR.11 / VCHGR.10 / VCHGR.9 / VCHGR.8 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x96 | VCHGRL | 0x00 | RO | VCHGR.7 / VCHGR.6 / VCHGR.5 / VCHGR.4 / VCHGR.3 / VCHGR.2 / VCHGR.1 / VCHGR.0 | [SH36735XX CV1.0A.pdf, PDF/印刷页 43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) |
| 0x97 | OWDH | 0x00 | RO | - / - / - / - / OWD20 / OWD19 / OWD18 / OWD17 | [SH36735XX CV1.0A.pdf, PDF/印刷页 44](../../sources/SH36735XX%20CV1.0A.pdf#page=44) |
| 0x98 | OWDM | 0x00 | RO | OWD16 / OWD15 / OWD14 / OWD13 / OWD12 / OWD11 / OWD10 / OWD9 | [SH36735XX CV1.0A.pdf, PDF/印刷页 44](../../sources/SH36735XX%20CV1.0A.pdf#page=44) |
| 0x99 | OWDL | 0x00 | RO | OWD8 / OWD7 / OWD6 / OWD5 / OWD4 / OWD3 / OWD2 / OWD1 | [SH36735XX CV1.0A.pdf, PDF/印刷页 44](../../sources/SH36735XX%20CV1.0A.pdf#page=44) |

### 10.2 配置字段完整索引

- SCONF1 PIN：Normal/IDLE/SLEEP/Powerdown命令及硬件自动清零，见模式章节 [SH36735XX CV1.0A.pdf，PDF/印刷页31](../../sources/SH36735XX%20CV1.0A.pdf#page=31)
- SCONF2：LTCLR、PD_EN、PD_CTL、PUMP_EN、PDSG_CTL、PDSGMOS、DSGMOS、CHGMOS均已在模式/FET/保护节解释 [SH36735XX CV1.0A.pdf，PDF/印刷页31](../../sources/SH36735XX%20CV1.0A.pdf#page=31)
- SCONF3：bit7保留；CGR_WK、LD_WK、CRLD_EN、OWD_EN、OWD_TRG，见接口和断线节 [SH36735XX CV1.0A.pdf，PDF/印刷页32](../../sources/SH36735XX%20CV1.0A.pdf#page=32)
- SCONF4：PDSGT、CN，完整查表和型号界限见前文 [SH36735XX CV1.0A.pdf，PDF/印刷页32](../../sources/SH36735XX%20CV1.0A.pdf#page=32) [SH36735XX CV1.0A.pdf，PDF/印刷页33](../../sources/SH36735XX%20CV1.0A.pdf#page=33)
- SCONF5：bit7:6保留；MOS_EN、OCC_EN、CADC_EN、WDT_EN、WDT [SH36735XX CV1.0A.pdf，PDF/印刷页34](../../sources/SH36735XX%20CV1.0A.pdf#page=34)
- SCONF6：四路TS保护使能、SC_EN、OCD_EN、UV_EN、OV_EN；TS使能不控制采样 [SH36735XX CV1.0A.pdf，PDF/印刷页34](../../sources/SH36735XX%20CV1.0A.pdf#page=34) [SH36735XX CV1.0A.pdf，PDF/印刷页42](../../sources/SH36735XX%20CV1.0A.pdf#page=42)
- SCONF7：bit7/3保留；RLD、CADCT、CDV，完整编码见测量/接口节 [SH36735XX CV1.0A.pdf，PDF/印刷页35](../../sources/SH36735XX%20CV1.0A.pdf#page=35)
- 0x47/48：OWV与全部ALARM源，共用字节要分离掩码 [SH36735XX CV1.0A.pdf，PDF/印刷页35](../../sources/SH36735XX%20CV1.0A.pdf#page=35) [SH36735XX CV1.0A.pdf，PDF/印刷页36](../../sources/SH36735XX%20CV1.0A.pdf#page=36)
- 0x49..54：所有电压、电流、温度阈值与延时，见保护节；OV/UV高字节bit7与bit3:2保留，OCD1 bit7保留，SC bit7:6保留 [SH36735XX CV1.0A.pdf，PDF/印刷页36](../../sources/SH36735XX%20CV1.0A.pdf#page=36) [SH36735XX CV1.0A.pdf，PDF/印刷页37](../../sources/SH36735XX%20CV1.0A.pdf#page=37) [SH36735XX CV1.0A.pdf，PDF/印刷页38](../../sources/SH36735XX%20CV1.0A.pdf#page=38) [SH36735XX CV1.0A.pdf，PDF/印刷页39](../../sources/SH36735XX%20CV1.0A.pdf#page=39)
- 0x55..57：20路均衡，0x55高4bit保留 [SH36735XX CV1.0A.pdf，PDF/印刷页39](../../sources/SH36735XX%20CV1.0A.pdf#page=39)
- FLAG1：RST1/WK/OCC/SC/OCD2/OCD1/UV/OV全为锁存，LTCLR后写0清 [SH36735XX CV1.0A.pdf，PDF/印刷页39](../../sources/SH36735XX%20CV1.0A.pdf#page=39) [SH36735XX CV1.0A.pdf，PDF/印刷页40](../../sources/SH36735XX%20CV1.0A.pdf#page=40)
- FLAG2：OTD/UTD/OTC/UTC/RST2/WDT是锁存写0清；VADC/CADC读清 [SH36735XX CV1.0A.pdf，PDF/印刷页40](../../sources/SH36735XX%20CV1.0A.pdf#page=40)
- FLAG3：bit7:2保留，OWD_IND只在OWD_FLG有效时解释；OWD_FLG读清 [SH36735XX CV1.0A.pdf，PDF/印刷页40](../../sources/SH36735XX%20CV1.0A.pdf#page=40) [SH36735XX CV1.0A.pdf，PDF/印刷页41](../../sources/SH36735XX%20CV1.0A.pdf#page=41)
- BSTATUS1：bit7/3保留、E2P_ERR、三种FET状态和两高侧FET状态；BSTATUS2：CHGING/DSGING/SLEEP/IDLE/BAL、bit2保留、LOADON/LOADOFF [SH36735XX CV1.0A.pdf，PDF/印刷页41](../../sources/SH36735XX%20CV1.0A.pdf#page=41) [SH36735XX CV1.0A.pdf，PDF/印刷页42](../../sources/SH36735XX%20CV1.0A.pdf#page=42)
- 0x5D..96测量与0x97..99断线位图：全只读，复位0，转换后更新；完整字/通道/公式见测量节和JSON [SH36735XX CV1.0A.pdf，PDF/印刷页42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) [SH36735XX CV1.0A.pdf，PDF/印刷页43](../../sources/SH36735XX%20CV1.0A.pdf#page=43) [SH36735XX CV1.0A.pdf，PDF/印刷页44](../../sources/SH36735XX%20CV1.0A.pdf#page=44)

<a id="electrical"></a>
## 11. 电气限值与设计校核

### 11.1 绝对最大值，不能作为推荐运行点

| 端口 | 极限范围（V） |
|---|---|
| VBAT/VCPR/SHIP；VC1..VC20 | VSS−0.3 .. 100 |
| VCP；CHGD/DSGD；HCHG/HDSG；PDSG | VSS−0.3 .. 110 |
| LDO_P；CS/SCK/SDI；RESET/ALARM/SDO；VCC；LDO_O | VSS−0.3 .. 6.5 |
| RS1 | VSS−0.3 .. VSS+0.3 |
| VC0 | VSS−0.3 .. 5.5 |
| RS2；TS1..TS4 | VSS−0.3 .. VCC+0.3 |
| CHG/DSG | VSS−0.3 .. 15 |

全部为 [SH36735XX CV1.0A.pdf，PDF/印刷页45](../../sources/SH36735XX%20CV1.0A.pdf#page=45)。原表注释还写VCn−VC(n−1)耐压范围同时应满足VSS−0.3..100V；这是原文，不能将其作为允许单体正常测量100V的依据，电芯VADC正常输入范围另为0..5V。超过绝对最大可能永久损坏。手册此处未列总功耗、结温/热阻、存储温度等完整极限参数，不能补造。[SH36735XX CV1.0A.pdf，PDF/印刷页45](../../sources/SH36735XX%20CV1.0A.pdf#page=45) [SH36735XX CV1.0A.pdf，PDF/印刷页47](../../sources/SH36735XX%20CV1.0A.pdf#page=47)

### 11.2 模式电流，严格保持测试条件

| 模式和条件 | 25℃typ/max uA | −40..85℃typ/max uA |
|---|---:|---:|
| Normal，所有模块开、无通信/均衡/断线/负载检测，两LDO无负载 | 150/200 | 200/250 |
| IDLE，SPI关、其他模块开、无断线/负载检测，两LDO无负载 | 110/130 | 110/160 |
| IDLE，另全部FET关，其他模块仍开含PUMP | 60/75 | 60/90 |
| SLEEP，SPI关、两LDO无负载、无负载唤醒 | 35/45 | 35/60 |
| Powerdown | 2.5/4 | 2.5/5.5 |
| SHIP | 2.5/4 | 2.5/5.5 |

全部在芯片VSS处测试，来源 [SH36735XX CV1.0A.pdf，PDF/印刷页46](../../sources/SH36735XX%20CV1.0A.pdf#page=46)；不能把这些数值当整板待机电流。电芯脚漏电在−40..85℃、每节3.7V、无断线检测：VC0..VC19为−1..1uA，VC20为−5..5uA。[SH36735XX CV1.0A.pdf，PDF/印刷页46](../../sources/SH36735XX%20CV1.0A.pdf#page=46)

### 11.3 驱动电气参数完整补充

| 参数 | min / typ / max | 条件和参考点 |
|---|---|---|
| CCP | 0.22 / 1 / 2.2uF | 电荷泵电容 |
| tCP | 未给 / 100 / 125ms | CCP=0.47uF，建立到VVCPTH |
| VVCPTH | 6 / 7 / 8V | 超过才可开MOS；表未清楚注明差分参考点，不擅加 |
| HCHG开启 | 10.5 / 11.5 / 12.5V | VHCHG−VVCPR，VBAT≥8V，CL=30nF |
| HCHG关闭 | 未给 / 未给 / 0.3V | VHCHG−VVCPR，同上 |
| HDSG开启 | 10.5 / 11.5 / 12.5V | VHDSG−VVCPR，同上 |
| HDSG关闭 | 未给 / 未给 / 0.3V | VHDSG−VDSGD，同上，参考点不同 |
| HCHG下拉 | 未给 / 150 / 250us | CCP1uF，CL30nF，串1kΩ，并10MΩ，90%→10% |
| HDSG下拉 | 未给 / 300 / 400us | 同上，DSGD另串1kΩ，VPACK+=VB+ |
| HCHG上拉 | 未给 / 200 / 350us | 同上，10%→90% |
| HDSG上拉 | 未给 / 200 / 400us | 同上，DSGD串1kΩ，VPACK+=VB+；旧版max360us |
| PDSG开启下拉电流 | 80 / 125 / 170uA | 原表未另列测试条件 |
| CHG/DSG高 | 10.5 / 12 / 14V | VBAT>13V，对地1MΩ |
| CHG/DSG高（低VBAT） | ≥VBAT−2.5V | 8V<VBAT≤13V，对地1MΩ |
| CHG/DSG低 | max1V | IOL=0.5mA |
| CHG/DSG上/下拉 | typ250/max350us | CL50nF、串1kΩ，DSG并10MΩ、CHG并1MΩ，10%↔90% |

来源 [SH36735XX CV1.0A.pdf，PDF/印刷页48](../../sources/SH36735XX%20CV1.0A.pdf#page=48) [SH36735XX CV1.0A.pdf，PDF/印刷页49](../../sources/SH36735XX%20CV1.0A.pdf#page=49)；旧版 [SH36735XX CV0.2C.pdf，PDF/印刷页48](../../sources/SH36735XX%20CV0.2C.pdf#page=48)。预放电开启范围电气表写0.2..3s，而编码表0.21..3.01s，两者均保留，不以近似范围改掉实际编码。[SH36735XX CV1.0A.pdf，PDF/印刷页32](../../sources/SH36735XX%20CV1.0A.pdf#page=32) [SH36735XX CV1.0A.pdf，PDF/印刷页48](../../sources/SH36735XX%20CV1.0A.pdf#page=48)

其余电气表项目已逐项归入相关章节：系统/LDO/数字端口见§1与§3，VADC/CADC精度见§6，保护和延时误差见§4，负载/充电器/状态检测见§8，SPI见§9；没有把typ值升级成min/max保证。

<a id="package"></a>
## 12. 封装与订购

CV1.0A称TQFP48；CV0.2C称TQFP48L。两版尺寸表相同：A最大1.2mm；A1 0.05–0.15；A2 0.9–1.05；D/E 6.85–7.15；HD/HE 8.8–9.2；b 0.15–0.27；e典型0.500；c 0.090–0.200；L 0.45–0.75；L1 0.85–1.15mm；θ2 0–10°。未另规定时容差±0.1mm，共面性0.1mm；不含毛边/门毛刺，毫米为控制尺寸。不能只凭名称变更推断焊盘尺寸变更。[SH36735XX CV1.0A.pdf，PDF/印刷页52](../../sources/SH36735XX%20CV1.0A.pdf#page=52) [SH36735XX CV0.2C.pdf，PDF/印刷页52](../../sources/SH36735XX%20CV0.2C.pdf#page=52)

订购号SH3673510U/048UR、SH3673514U/048UR、SH3673517U/048UR、SH3673520U/048UR，Tray盘包装，最小起订量2.5K（历史手册信息，不代表当前库存/商务条件）。[SH36735XX CV1.0A.pdf，PDF/印刷页53](../../sources/SH36735XX%20CV1.0A.pdf#page=53)

<a id="versions"></a>
## 13. 版本选择与疑点处理

[完整差异](version_differences.md)独立列出新旧证据。关键：V1.0A改变泵和SHIP时序、SC精度上限与HDSG上拉、参考原理图，同时新增断线示例。寄存器地址/字段/初值经两版独立解析和比较未发现实质变化；版次仍必须与每行绑定，不能因为地图相同就删除旧版证据。[SH36735XX CV1.0A.pdf，PDF/印刷页54](../../sources/SH36735XX%20CV1.0A.pdf#page=54)

未解决、足以阻止盲目量产驱动的问题：

1. SPI写CRC正文含“写数据长度”，图9没有长度字段。两版相同问题。[SH36735XX CV1.0A.pdf，PDF/印刷页26](../../sources/SH36735XX%20CV1.0A.pdf#page=26) [SH36735XX CV0.2C.pdf，PDF/印刷页26](../../sources/SH36735XX%20CV0.2C.pdf#page=26)
2. 电流符号位方向清楚但负数编码未明确，无官方负值示例。[SH36735XX CV1.0A.pdf，PDF/印刷页19](../../sources/SH36735XX%20CV1.0A.pdf#page=19) [SH36735XX CV1.0A.pdf，PDF/印刷页42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) [SH36735XX CV1.0A.pdf，PDF/印刷页43](../../sources/SH36735XX%20CV1.0A.pdf#page=43)
3. 新版断线示例的奇偶掩码与OWD1在bit0的映射相反。旧版没有该示例，不能说旧版也给了错误掩码。[SH36735XX CV1.0A.pdf，PDF/印刷页16](../../sources/SH36735XX%20CV1.0A.pdf#page=16) [SH36735XX CV1.0A.pdf，PDF/印刷页30](../../sources/SH36735XX%20CV1.0A.pdf#page=30) [SH36735XX CV1.0A.pdf，PDF/印刷页44](../../sources/SH36735XX%20CV1.0A.pdf#page=44) [SH36735XX CV0.2C.pdf，PDF/印刷页16](../../sources/SH36735XX%20CV0.2C.pdf#page=16)
4. 总表CADCDL bit1重复.0，详细表.1；CELL6H/CELL16H详细表bit3重复.12，而总表.11。结构化输出已分别说明采用哪处，保留原标签。[SH36735XX CV1.0A.pdf，PDF/印刷页29](../../sources/SH36735XX%20CV1.0A.pdf#page=29) [SH36735XX CV1.0A.pdf，PDF/印刷页30](../../sources/SH36735XX%20CV1.0A.pdf#page=30) [SH36735XX CV1.0A.pdf，PDF/印刷页42](../../sources/SH36735XX%20CV1.0A.pdf#page=42) [SH36735XX CV1.0A.pdf，PDF/印刷页43](../../sources/SH36735XX%20CV1.0A.pdf#page=43)
5. OCD2延时4bit公式最大400ms，但电气表425ms；UV最大延时编码10.01s，但电气表10.1s；预放电编码最大3.01s、电气表3s。[SH36735XX CV1.0A.pdf，PDF/印刷页32](../../sources/SH36735XX%20CV1.0A.pdf#page=32) [SH36735XX CV1.0A.pdf，PDF/印刷页36](../../sources/SH36735XX%20CV1.0A.pdf#page=36) [SH36735XX CV1.0A.pdf，PDF/印刷页37](../../sources/SH36735XX%20CV1.0A.pdf#page=37) [SH36735XX CV1.0A.pdf，PDF/印刷页48](../../sources/SH36735XX%20CV1.0A.pdf#page=48) [SH36735XX CV1.0A.pdf，PDF/印刷页49](../../sources/SH36735XX%20CV1.0A.pdf#page=49)
6. WDT关预放电是否受PDSGMOS限制，7.9与8.2不同；不能假设手动预放电可在故障继续导通。[SH36735XX CV1.0A.pdf，PDF/印刷页17](../../sources/SH36735XX%20CV1.0A.pdf#page=17) [SH36735XX CV1.0A.pdf，PDF/印刷页22](../../sources/SH36735XX%20CV1.0A.pdf#page=22)
7. RST1_FLG表复位值1但说明不含软件复位，WarmUp包含退出SHIP而FLAG1细节列举未写SHIP。需按复位来源实测/询问厂商。[SH36735XX CV1.0A.pdf，PDF/印刷页11](../../sources/SH36735XX%20CV1.0A.pdf#page=11) [SH36735XX CV1.0A.pdf，PDF/印刷页26](../../sources/SH36735XX%20CV1.0A.pdf#page=26) [SH36735XX CV1.0A.pdf，PDF/印刷页39](../../sources/SH36735XX%20CV1.0A.pdf#page=39)
8. 模式表以√(O)表达可配置使能，不等价于默认关闭；详细寄存器列初值启用多种保护。[SH36735XX CV1.0A.pdf，PDF/印刷页9](../../sources/SH36735XX%20CV1.0A.pdf#page=9) [SH36735XX CV1.0A.pdf，PDF/印刷页34](../../sources/SH36735XX%20CV1.0A.pdf#page=34)
9. 原理图目录反序、VVCPTH参考点未清晰标注、读帧跨字节一致性未承诺，分别按正文/原图/未知处理。[SH36735XX CV1.0A.pdf，PDF/印刷页6](../../sources/SH36735XX%20CV1.0A.pdf#page=6) [SH36735XX CV1.0A.pdf，PDF/印刷页7](../../sources/SH36735XX%20CV1.0A.pdf#page=7) [SH36735XX CV1.0A.pdf，PDF/印刷页48](../../sources/SH36735XX%20CV1.0A.pdf#page=48) [SH36735XX CV1.0A.pdf，PDF/印刷页56](../../sources/SH36735XX%20CV1.0A.pdf#page=56)

<a id="driver-checklist"></a>
## 14. 安全驱动与台架验证清单（工程建议）

- 固定具体芯片型号、PDF版本、物理串数、分流器阻值/方向、NTC曲线、FET拓扑；无这些输入不生成“量产默认配置”
- 上电保持外部系统安全，等待相应WarmUp；读取复位原因、E2P_ERR、FET实际状态；对读清标志只读一次并留档
- SPI使用Mode3、MSB，遵守全部CS/时钟/数据时序；逐帧记录ACK/回显/CRC；验证写CRC真实算法和读CRC包含0xFF
- 明确所有保留位和混合语义；FLAG1/FLAG2的W0C绝不能套W1C驱动；一轮清除消耗LTCLR，清另一个寄存器前重新授权
- 先选择CN和对应线束，再写入与电芯/分流器/NTC匹配且在电气范围内的阈值，完整读回；保护码宽可编码不等于全范围获保证
- 建立泵并核实高侧实际状态后才发充放电允许；将MOS_EN反向强制导通列入隔离/故障测试
- 用已知正负分流电压确认CUR/CADCD数值编码和应用方向；验证所有通道单位、温度分母边界、ADC复位零值和采样超时
- 明确Normal/IDLE保护延时差异和CADC平均窗口；SLEEP期间的外温/电流不能标记“实时正常”
- 测量标志CRC失败可能已被读清，标记事件不确定而非重读后说“没有故障”；跨字节/多通道读取一致性通过台架确认
- 逐电芯开路测试断线IND/位图，解决新版掩码冲突；均衡超时与写入续期做实测，验证相邻通道测量和热负荷
- 测试各OV/UV/OCD1/OCD2/SC/OCC/温度故障的检测、FET关断、ALARM、清除再触发；在安全限能条件下测实际总关断延迟
- 验证WDT逐阶段、RESET脉冲、未清标志进入Powerdown、充电唤醒、SHIP无充电唤醒；确认自动/手动预放电的故障关断
- Powerdown/SHIP退出和软件复位后重新校核RAM与保护初值，不使用旧影子值；未确认非易失接口前一律不承诺配置持久化
- 将上文源内矛盾提交厂商，保留答复版本；未验证的关键项应使驱动进入安全失败路径，不默认放行FET

<a id="limitations"></a>
## 15. 厂商声明与非覆盖事项

厂商重要声明强调半导体有失效概率，用户需采取冗余、防火等安全设计，参考应用电路不保证适用于特定量产应用；禁止其列出的军事、国防、核能、医疗及可能导致人身伤害/死亡/环境破坏等使用领域，最终约束应直接阅读原声明和适用协议。手册可被厂商无预告修改，订购前建议咨询销售。此处是文件内容摘要，不提供法律解释。[SH36735XX CV1.0A.pdf，PDF/印刷页55](../../sources/SH36735XX%20CV1.0A.pdf#page=55)

手册没有提供本项目电池化学体系、功率级目标、固件完整示例、公开EEPROM校准协议、认证结论或整板安规符合性；本参考也没有补造这些项目。

<a id="navigation"></a>
## 16. 来源导航与覆盖方式

- [CV1.0A原始PDF](../../sources/SH36735XX%20CV1.0A.pdf)、[CV0.2C原始PDF](../../sources/SH36735XX%20CV0.2C.pdf)
- [CV1.0A完整逐页文本](../../extracted/SH36735XX_CV1_0A/full_by_page.md)、[CV0.2C完整逐页文本](../../extracted/SH36735XX_CV0_2C/full_by_page.md)：保留完整上下文，但数学公式和电路连线必须回到PDF图像，不把抽取缺失当手册未写
- [机器可读寄存器](registers.json)：两个版本各90行，不合并来源
- [逐页覆盖表](coverage.json)：114页各有主体章节、原文和来源映射，目录/声明/更改记录也包含
- [原图验证与限制](validation.md)、[版本差异](version_differences.md)

原目录是页56–57；正文各节定位请优先本导航和正文标题，原目录应用电路顺序存在错误。[SH36735XX CV1.0A.pdf，PDF/印刷页56](../../sources/SH36735XX%20CV1.0A.pdf#page=56) [SH36735XX CV1.0A.pdf，PDF/印刷页57](../../sources/SH36735XX%20CV1.0A.pdf#page=57)
