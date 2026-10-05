# D008 三个生产 profile 的有效 AFE 配置

固定 SHA：本地最新 clean 提交 `0aaa842938c37b0016a23d8a29ce2aa416ca2c4d`，冻结内容 SHA256 `05fb3147811468a347d3b27538b364d6ce92d56c533a17d713bcc8e1ac689613`。以下为**当前函数host寄存器模型实际写入**与原厂独立oracle比对的干净默认结果，非实装寄存器读回。脚本`register_host.py`从当前TC32条件展开`.i`抽取默认构造、basic、保护和新的`DVC1124_ApplyProjectOperatingConfig`原函数，用原PDF几何表复位值初始化bank，3 profile×36个寄存器（108项）独立编码期望均匹配。`.i`中的dirty身份0与最新clean本地状态匹配；完整TU/生产构建/实板结论不得由函数抽取推断。模型I/O总成功，失败、掉电、RC动态和实际模拟量不在此测试范围。R/W 仅修改 owned bits 时，列出的完整byte以 RM V1.2复位保留位为前提；芯片实际版本/旧运行状态不一致时不能把完整byte当读回事实。动态字段另外列出。

配置来源链：生产显式 `D008_PRODUCT_PROFILE` → `bms/products/d008/d008_product_profile.h:15` 的串数/chemistry → `bms_product_conf.h:80` CUV3默认 → `param.h:20`/`:104`软件表常量 → `bms_afe_hw_profile_build_default` (`bms_afe_hw_profile.c:144`) → 独立AFE profile → `dvc_apply_protection_from_params` (`dvc1124.c:857`)；固定引脚/ADC/watchdog政策来自 `dvc1124_project_config.h`，通过 basic config + boot operating defaults应用。已有设备最终profile由其有效持久化及OTA revision决定，不能宣称每台设备都是默认值。

| 配置 | 16S-LFP (profile 3) | 20S-NMC (profile 2) | 24S-LFP (profile 1) | 原厂依据/来源 |

|---|---|---|---|---|

| 有效cell | 16 | 20 | 24 | DS p15；product_profile.h |

| R6A..6C CM masks | FF 00 00 | F0 00 00 | 00 00 00 | RM p21；dvc1124.c:674 |

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

动态R51：启动safe=00；正常双方ON=0F；充电保护AUTO_DIODE=0E；放电保护AUTO_DIODE=0B；双保护AUTO_DIODE=0A。这些是驱动请求，不等于GP电平/MOS真实导通，DSGF/CHGF来自R6反馈仍须检查。R0 alarm为W0C，清选定位写~mask；R1 CST命令与ADC RC事件分开。CAMZ为触发自清；COW最长1s自清；R67..69 balance设定60s自动复位。均衡刷新代码 `dvc1124.c:1251`以后有真实读回；cell mask只保障软件访问/保护，不能替代未用脚硬短接。

采样换算核对：CC2为20-bit signed两补，0.3125µV/LSB（DS p13，RM p7），200µΩ约1.5625mA/code；代码整数有舍入、符号扩展。cell为unsigned100µV（RM p9–15），按低侧累计共模与RM p32表除K；原厂表/公式冲突尚未解决。PACK/LOAD/VTP为12.8mV/code（RM p8）；die T=N×0.24467-271.03℃，整数0.1℃有舍入误差；NTC用N_GP/(N_1P8-N_GP)×(FRT×25+6800Ω)（DS p15–16，RM p28–29），再查项目NTC温度表，后者曲线实装来源待补。启动校准强制命令及R6四路径OFF，CAMZ后两次270ms样本、±1500mA/200mA spread界限是项目政策而非厂商精度保证。

三profile之间真实的软件差异是串数mask、report有效槽、SOC chemistry；AFE默认保护完全相同。该设计不能自动证明20S NMC 的3750mV COV或各profile10/20A能力合乎产品要求。16S/20S使用同24S图的未用采样脚装配、NTC和Rsense实际一致性没有BOM证据，当前不能标为“硬件已确认”。

新当前运行配置入口：`dvc1124_boot.c:126`先调用采样，再由`:133`调用`DVC1124_ApplyProjectOperatingConfig`；实际固定运行字段在`dvc1124.c:1847`–`:1866`写入。`DVC1124_WriteRegisterSafe`在`:1717`执行保留位RMW与owned mask回读；初始basic和硬profile仍在`:729`与`:857`。配置集中没有消除现存采样资格、恢复状态混用或OC最小范围风险。

独立最小范围probe（3 profile相同）：OCD2请求1A，validator返回1，实际20A；OCD2请求延迟0ms，validator返回1，实际4ms。见`register_results.json`。108个byte编码匹配表示“源码形成的值符合文档编码”，不表示这些产品默认保护阈值已签核。

最终clean重核：相对中间dirty冻结仅8文件变化（`previous_dirty_vs_final_clean.json`）。DVC boot唯一差异为第121行注释从“返回样本资格”更正为“更新样本有效性”，与void接口一致；它没有增加ADC完成/新鲜资格。DVC驱动、恢复、profile与最终寄存器执行代码未变。三类独立probe在最终source重新抽取、重新编译、重新运行；恢复6反例/ADC2帧反例仍出现，三profile108个寄存器oracle仍0差异，1A→20A和0ms→4ms边界仍被接受。资料原件同hash，原PDF审查证据复用，不重复宣称新实板验证。
