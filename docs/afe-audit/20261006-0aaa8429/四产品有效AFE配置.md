# 四产品有效 AFE 配置对照

基线为本地 HEAD 0aaa842938c37b0016a23d8a29ce2aa416ca2c4d 加冻结工作区 05fb3147811468a347d3b27538b364d6ce92d56c533a17d713bcc8e1ac689613。下面分清请求、原厂解码和运行overlay；不是实机寄存器读回。

# D008 三个生产 profile 的有效 AFE 配置

固定 SHA：本地最新 clean 提交 `0aaa842938c37b0016a23d8a29ce2aa416ca2c4d`，冻结内容 SHA256 `05fb3147811468a347d3b27538b364d6ce92d56c533a17d713bcc8e1ac689613`。以下为**当前函数host寄存器模型实际写入**与原厂独立oracle比对的干净默认结果，非实装寄存器读回。脚本`register_host.py`从当前TC32条件展开`.i`抽取默认构造、basic、保护和新的`DVC1124_ApplyProjectOperatingConfig`原函数，用原PDF几何表复位值初始化bank，3 profile×36个寄存器（108项）独立编码期望均匹配。`.i`中的dirty身份0与最新clean本地状态匹配；完整TU/生产构建/实板结论不得由函数抽取推断。模型I/O总成功，失败、掉电、RC动态和实际模拟量不在此测试范围。R/W 仅修改 owned bits 时，列出的完整byte以 RM V1.2复位保留位为前提；芯片实际版本/旧运行状态不一致时不能把完整byte当读回事实。动态字段另外列出。

配置来源链：生产显式 `D008_PRODUCT_PROFILE` → [bms/products/d008/d008_product_profile.h:15](<C:/Users/Administrator/Documents/CodexOutputs/telink-new-sdk-b85/afe-factory-audit-local-20261006-0aaa8429/source/bms/products/d008/d008_product_profile.h:15>) 的串数/chemistry → `bms_product_conf.h:80` CUV3默认 → [param.h:20](<C:/Users/Administrator/Documents/CodexOutputs/telink-new-sdk-b85/afe-factory-audit-local-20261006-0aaa8429/source/bms/core/param.h:20>)/`:104`软件表常量 → `bms_afe_hw_profile_build_default` ([bms_afe_hw_profile.c:144](<C:/Users/Administrator/Documents/CodexOutputs/telink-new-sdk-b85/afe-factory-audit-local-20261006-0aaa8429/source/bms/core/bms_afe_hw_profile.c:144>)) → 独立AFE profile → `dvc_apply_protection_from_params` ([dvc1124.c:857](<C:/Users/Administrator/Documents/CodexOutputs/telink-new-sdk-b85/afe-factory-audit-local-20261006-0aaa8429/source/bms/afe/dvc1124/dvc1124.c:857>))；固定引脚/ADC/watchdog政策来自 `dvc1124_project_config.h`，通过 basic config + boot operating defaults应用。已有设备最终profile由其有效持久化及OTA revision决定，不能宣称每台设备都是默认值。

| 配置 | 16S-LFP (profile 3) | 20S-NMC (profile 2) | 24S-LFP (profile 1) | 原厂依据/来源 |

|---|---|---|---|---|

| 有效cell | 16 | 20 | 24 | DS p15；product_profile.h |

| R6A..6C CM masks | FF 00 00 | F0 00 00 | 00 00 00 | RM p21；[dvc1124.c:674](<C:/Users/Administrator/Documents/CodexOutputs/telink-new-sdk-b85/afe-factory-audit-local-20261006-0aaa8429/source/bms/afe/dvc1124/dvc1124.c:674>) |

| 软件化学体系/SOC | generic LFP | generic NMC | generic LFP | product_profile.h；不改变AFE阈值 |

| 完整型号假设 | MODEL22/-2 | 相同 | 相同 | 源码假设，BOM未证实 |

| I2C | 0x40写/0x41读（7-bit=0x20），100kHz | 相同 | 相同 | DS p23；default固定地址 |

| Rsense | 200µΩ | 相同 | 相同 | 图10×2mΩ并联，实装待测 |

| GP1..4 | NTC；heater/battery1/battery2/MOS | 相同 | 相同 | R74=49；RM p24–25 |

| GP5/6 | low CHG/DSG output | 相同 | 相同 | R75=7F；RM p25；图外部低侧 |

| CADC | HSFM1,CAEW1,CAES0；R55=A8(保留bit5=1) | 相同 | 相同 | RM p17–18 |

| CC1/CC2 | 4ms/256ms；R56=FF | 相同 | 相同 | RM p18；DS p13 |

| VADC | VAE1,VASM1,VAMP0,VAO1；R6E=CD | 相同 | 相同 | RM p22–23；每CC2测一次 |

| 电荷泵/电压表示 | CPVS5=10V,COW0,CMM0,CVS0；R6D=28 | 相同 | 相同 | RM p22；低侧专用建议6V差异见findings |

| 高侧pulldown | DPC16；R52=90(含PDWM1) | 相同 | 相同 | RM p16；高侧驱动未使用 |

| 放/充屏蔽 | R53=50，R54=78 | 相同 | 相同 | RM p16–17；DWM/CWM0，DBDM/CBDM0 |

| 硬COV请求→effective | 3750mV/1000ms→相同；R70=CB,R71=28 | 相同 | 相同 | RM p23；COVT3250+500 |

| 硬CUV请求→effective | 2200mV/1000ms→相同；R72=89,R73=88 | 相同 | 相同 | RM p24 |

| OCD1/OCC1请求→effective | 10A/100ms→10A/96ms；R59/R5A=08，R5B/R5C=0B | 相同 | 相同 | RM p18–19；code8×0.25mV，(11+1)×8ms |

| OCD2/OCC2请求→effective | 15A/100ms→20A/100ms；R5E=40，R5F=C0(保留bit7=1)，R60/R61=18 | 相同 | 相同 | RM p19–20；(0+1)×4mV，(24+1)×4ms |

| 硬SCD | OFF；R62=00，R63=00 | 相同 | 相同 | RM p20；默认enable未设置 |

| 反向体二极管阈值 | 80µV nominal0.4A；R66=02 | 相同 | 相同 | RM p21；放电方向图文矛盾待确认 |

| current wake | CAES0，CWT0；R65=00 | 相同 | 相同 | RM p18/p20 |

| 芯片过温硬关机 | COTT0 disabled；R76=00（忽略动态COTF） | 相同 | 相同 | RM p26 |

| I2C watchdog | 4s；R77=C4（IWTS动态） | 相同 | 相同 | RM p26；超时CHG/DSG关断源未mask |

| timed wake / IRQ | OFF /全屏蔽；R78=00,R79=FF | 相同 | 相同 | RM p26–28 |

动态R51：启动safe=00；正常双方ON=0F；充电保护AUTO_DIODE=0E；放电保护AUTO_DIODE=0B；双保护AUTO_DIODE=0A。这些是驱动请求，不等于GP电平/MOS真实导通，DSGF/CHGF来自R6反馈仍须检查。R0 alarm为W0C，清选定位写~mask；R1 CST命令与ADC RC事件分开。CAMZ为触发自清；COW最长1s自清；R67..69 balance设定60s自动复位。均衡刷新代码 [dvc1124.c:1251](<C:/Users/Administrator/Documents/CodexOutputs/telink-new-sdk-b85/afe-factory-audit-local-20261006-0aaa8429/source/bms/afe/dvc1124/dvc1124.c:1251>)以后有真实读回；cell mask只保障软件访问/保护，不能替代未用脚硬短接。

采样换算核对：CC2为20-bit signed两补，0.3125µV/LSB（DS p13，RM p7），200µΩ约1.5625mA/code；代码整数有舍入、符号扩展。cell为unsigned100µV（RM p9–15），按低侧累计共模与RM p32表除K；原厂表/公式冲突尚未解决。PACK/LOAD/VTP为12.8mV/code（RM p8）；die T=N×0.24467-271.03℃，整数0.1℃有舍入误差；NTC用N_GP/(N_1P8-N_GP)×(FRT×25+6800Ω)（DS p15–16，RM p28–29），再查项目NTC温度表，后者曲线实装来源待补。启动校准强制命令及R6四路径OFF，CAMZ后两次270ms样本、±1500mA/200mA spread界限是项目政策而非厂商精度保证。

三profile之间真实的软件差异是串数mask、report有效槽、SOC chemistry；AFE默认保护完全相同。该设计不能自动证明20S NMC 的3750mV COV或各profile10/20A能力合乎产品要求。16S/20S使用同24S图的未用采样脚装配、NTC和Rsense实际一致性没有BOM证据，当前不能标为“硬件已确认”。

新当前运行配置入口：[dvc1124_boot.c:126](<C:/Users/Administrator/Documents/CodexOutputs/telink-new-sdk-b85/afe-factory-audit-local-20261006-0aaa8429/source/bms/afe/dvc1124/dvc1124_boot.c:126>)先调用采样，再由`:133`调用`DVC1124_ApplyProjectOperatingConfig`；实际固定运行字段在[dvc1124.c:1847](<C:/Users/Administrator/Documents/CodexOutputs/telink-new-sdk-b85/afe-factory-audit-local-20261006-0aaa8429/source/bms/afe/dvc1124/dvc1124.c:1847>)–`:1866`写入。`DVC1124_WriteRegisterSafe`在`:1717`执行保留位RMW与owned mask回读；初始basic和硬profile仍在`:729`与`:857`。配置集中没有消除现存采样资格、恢复状态混用或OC最小范围风险。

独立最小范围probe（3 profile相同）：OCD2请求1A，validator返回1，实际20A；OCD2请求延迟0ms，validator返回1，实际4ms。见`register_results.json`。108个byte编码匹配表示“源码形成的值符合文档编码”，不表示这些产品默认保护阈值已签核。

最终clean重核：相对中间dirty冻结仅8文件变化（`previous_dirty_vs_final_clean.json`）。DVC boot唯一差异为第121行注释从“返回样本资格”更正为“更新样本有效性”，与void接口一致；它没有增加ADC完成/新鲜资格。DVC驱动、恢复、profile与最终寄存器执行代码未变。三类独立probe在最终source重新抽取、重新编译、重新运行；恢复6反例/ADC2帧反例仍出现，三profile108个寄存器oracle仍0差异，1A→20A和0ms→4ms边界仍被接受。资料原件同hash，原PDF审查证据复用，不重复宣称新实板验证。

## SH 当前运行模式overlay

默认/static/wake/reinit=0x44（CGR_WK1/LD_WK00/CRLD01）。有SC/OCD或软件DSG_OC活动时，主采样切换为0x48（CRLD10）；同一帧检测changed或初次模式则不认旧状态，等待200ms后以LOADOFF=1且LOADON=0作为load_removed。故障解除后恢复0x44并等待后以新VADC+C+产生charger证据。两模式互斥，与原件p23/32一致，当前不再有“从不写LOAD”的错误。

OWD RMW保留当前CRLD：C+模式enable0x46/trigger0x47（TRG自清回0x46）；LOAD模式enable0x4A/trigger0x4B（自清0x4A）；disable分别0x44/0x48。sleep仅将CGR_WK置1，保持当时CRLD；wake重新0x44。LD_WK始终00，负载唤醒是未启用选择，是否符合产品需求需签核。出现整字节验证TRG競态见LSH-06。

SC十个200ms物理窗口+清标志成功+后续复读无SC且load_removed才清latch；OCD要求LOAD移除或CHGING、OCC要求charger移除或DSGING，且硬OC恢复只在本次VADC/CADC两ready的fresh帧推进。无release条件/通信失败会作废窗口；clear失败保持fail-safe直到init。200ms样本窗口/1.2s温度startup/400msADC年龄是软件策略，芯片可保证范围及芯片失效实测仍不能由host代替。

## SH 默认请求与最终寄存器

有效生产产品宏：D011 10S/250µΩ，heater/TS3/TS4/balance启用；D013 4S/100µΩ，heater/TS3/TS4/balance禁用；D014 8S/667µΩ，heater/TS3禁用、TS4 MOS NTC与balance启用。SH型号均为SH3673510；文件名SH3673520是共用芯片族低层，不能据文件名推型号。D013硬件Rsense/4S/功能删减仍需要实际原理图BOM；D014 TS4图纸/实装差异需主报告列缺口。

以下是**无用户持久化覆盖**的 default hardware profile经当前control TU形成的寄存器，非任意实机Flash当前值。有合法持久化profile时重新量化这些保护寄存器，恢复/重配用同一profile；源策略不应被称已签核产品参数。

|地址/名称|D011|D013|D014|解码/说明|

|---|---:|---:|---:|---|

|40 SCONF1|00|00|00|Normal|

|41 SCONF2|50|50|50|初始化CHG/DSG请求OFF；运行FET请求可变bits1:0|

|42 SCONF3|44|44|44|初始CGR_WK1、LD_WK0、CRLD01；运行CRLD恢复窗口见下文|

|43 SCONF4|6A|64|68|有效串数10/4/8，其他预放电/模式设置共用|

|44 SCONF5|3C|3C|3C|MOS_EN、OCC、CADC、WDT启用；WDT code0|

|45 SCONF6|3F|3F|3F|TS1/TS2/SC/OCD/UV/OV；TS3/TS4硬件温度位禁用|

|46 SCONF7|04|04|04|共同静态值|

|47 OWV/ALARMH|57|57|57|OWV码5=960mV；LOADOFF/VADC/CADC中断启用|

|48 ALARML|FF|FF|FF|WK/WDT/OWD/TEMP/OCC/OCD/UV/OV中断启用|

|49 OVH_OVT|42|42|42|3750mV、2030ms|

|4A OVL|EE|EE|EE|见上|

|4B UVH_UVT|72|72|72|3000mV、10010ms|

|4C UVL|58|58|58|见上|

|4D OCD1V_T|00|00|01|5/5/10mV、140ms|

|4E OCD2V_T|30|30|31|10/10/20mV、100ms|

|4F SCV_SCT|07|07|07|2×实际OCD2、256us|

|50 OCCV_T|01|00|04|2.75/1.375/6.875mV、140ms|

|51 OTC|85|85|85|NTC表55℃量化|

|52 OTD|76|76|76|NTC表60℃量化|

|53 UTC|77|77|77|NTC表0℃量化|

|54 UTD|C0|C0|C0|NTC表−20℃量化|

|55–57 CB1–3|00|00|00|初始化全关；运行按有效串/策略刷新|

|有效物理参数（未经器件误差/实测修正）|请求default|D011|D013|D014|

|---|---:|---:|---:|---:|

|OCD1|10A/100ms|20A/140ms|50A/140ms|14.9925A/140ms|

|OCD2|15A/100ms|40A/100ms|100A/100ms|29.985A/100ms|

|SC|30A/256us|80A/256us|200A/256us|59.970A/256us|

|OCC|10A/100ms|11A/140ms|13.75A/140ms|10.30735A/140ms|

|COV|3750mV/1000ms|3750mV/2030ms|同左|同左|

|CUV|3000mV/10000ms|3000mV/10010ms|同左|同左|

以上电流根据原页电压阈值/Rsense独立计算；D014整数667µΩ是模型，实际三只2mΩ并联理想值666.666…µΩ及电阻温漂/容差需测量。公共effective报告向0.1A上取整，分别为D01415/30/60/10.4A，不能替代这些物理值。尤其D013最小硬件阈值决定50/100/200A，与请求10/15/30A差距大；来源是Rsense和最小原厂阶梯，不应默认为软件请求被硬件实现。缺最终产品允许电流和保护能量签核，不能判这些default符合产品安全目标。

模式与运行：init先SPI reset再写静态/profile，初始FET OFF；sleep关heater/balance/FET再写SCONF1=AA，wake写Normal、等待10ms并重新静态/profile、FET OFF，然后重新三采样资格。均衡刷新100次200ms回调≈20s，小于原件30.38s自动清除，但主循环拖延的实际deadline仍需测量。电压5/32mV、电流100000000/(29127×RsenseµΩ)、包电压125/32mV和NTC10×TEMP/(32768−TEMP)量纲与原页一致；负电流编码原页没有明确写“二补码”，源码有该假设，列未知不判通过。
