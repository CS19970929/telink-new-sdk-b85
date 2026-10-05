# DVC 原PDF与D008覆盖记录

实际审查 SHA 本地最新 clean 提交 `0aaa842938c37b0016a23d8a29ce2aa416ca2c4d`，冻结内容 SHA256 `05fb3147811468a347d3b27538b364d6ce92d56c533a17d713bcc8e1ac689613`；版本/hash见findings。本轮沿用已核hash相同的62页原PDF提取、渲染和已目视证据，并在新资料路径重新跑145×8位表和144语义比对。渲染不是目视：下表明确哪些页实际回看原页面；其余页按重新提取文本/原PDF几何表独立审核，不宣称逐页目视。物理页=印刷页。

## DS V1.1逐页主题

| 页 | 主题 | 实际检查 |

|---|---|---|

| 1 | 4..24S/100V能力、特点 | 原页提取文本；型号与总压范围 |

| 2 | 内容目录 | 原页提取文本；完整页结构 |

| 3 | LQFP48引脚图/1..12采样 | 原图目视及文本 |

| 4 | 引脚13..48、电源/GP/driver | 原表目视；与硬件图GP低侧/电源核对 |

| 5 | 绝对最大额定 | 原表目视；VTOP132/VCP120/Tj85 |

| 6 | 推荐工作条件 | 原表目视；电源/总压/电流 |

| 7 | 电气特性ADC | 原表目视；符号/单位/LSB/精度条件 |

| 8 | GP/driver/temperature特性 | 原表目视；typ32/36µA冲突 |

| 9 | 硬保护范围步进 | 原表目视；OC1/2/SC/延迟独立期望 |

| 10 | I2C时序参数/条件 | 原表目视；100k/电平/建立保持 |

| 11 | 模块框图 | 原图目视；SRP/SRN/VTOP/内部ADC和保护 |

| 12 | shutdown/sleep/normal功能与进入退出 | 原图/表目视；保留和关闭域/唤醒源 |

| 13 | CADC与VADC完成和时序 | 原表/波形目视；CC2=256ms/CC2F/VADF |

| 14 | ADC输入范围/测量节点 | 原图目视；PACK/LOAD/C24节点，不替原厂解歧义 |

| 15 | 同步波形/未用串短接/openwire/芯片温度 | 原图/公式目视；COW无官方0mV判据 |

| 16 | NTC/RPU/电容/R-T | 原图/公式目视；NTC转换公式 |

| 17 | COV/CUV/OCD1保护与锁存 | 原文目视；阈值/enable/清除方式 |

| 18 | OCC1/OC2/SCD保护与锁存 | 原文目视；条件/恢复ownership |

| 19 | SCD清除/I2C watchdog | 原文目视；IWT矛盾 |

| 20 | 内外均衡/奇偶/定时 | 原表/图目视；25mA、CMM及60s |

| 21 | 均衡时序/电荷泵/DSG/LOAD/GP6 | 原波形/文目视；串数按未mask排序 |

| 22 | CHG/PCHG/PACK/GP5/硬控制 | 原文目视；2V charger条件/低侧输出 |

| 23 | I2C/地址/CRC/RA限制 | 原表目视；-22/-24引脚差异，0x90歧义 |

| 24 | read/write CRC波形/超时/IRQ | 原波形目视；逐字节CRC、64ms |

| 25 | 未用引脚拓扑表 | 原表目视；低侧CPVS6V/VCP100nF要求 |

| 26 | PCB布局推荐 | 原图目视；敏感电源/差分采样分区 |

| 27 | 高侧应用框图 | 原图目视；不照搬为D008低侧硬件事实 |

| 28 | 封装毫米尺寸 | 原图目视；未做机械制造精度复核 |

| 29 | 订购/修订历史 | 原页提取文本；V1.1、-22/-24适用性 |

## RM V1.2逐页主题

| 页 | 主题 | 实际检查 |

|---|---|---|

| 1–5（逐页） | 寄存器汇总0..90及目录 | 重新提取文本；与详细表地址完整性对照，未单独目视总表 |

| 6 | R0 W0C与R1 RC/CST | 原表目视；全位NAME/MODE/default独立几何比对；恢复错寄存器定位 |

| 7 | CC1/CC2/低4位FET flags | 原表目视；20-bit signed/flags独立来源 |

| 8 | FET flags续文/VTP/PACK/LOAD | 原文/表提取；独立表比对、字段语义逐条精确匹配 |

| 9–10（逐页） | die/GP/V1P8/VC1..4测量 | 原表自动几何检查+字段描述逐项原文匹配；文本审核公式单位 |

| 11–14（逐页） | VC5..23测量 | 原表自动几何检查+每字段100µV原文匹配；无复杂图公式，未逐页目视 |

| 15 | VC24/VVOS/CVOS/R51 | 原表目视；signed格式/每位属性 |

| 16 | FET AUTO_DIODE/PDWM/DPC/DSG masks | 原表目视；0/1 mask语义及31N/A |

| 17 | DSG/CHG masks/CADC | 原表目视；独立表与语义比对 |

| 18 | CADC/CC1时间/OCD1 | 原表目视；reserved默认非零、OC1min |

| 19 | OCC1/OC1延迟/OC2 | 原表目视；(code+1)与enable |

| 20 | OC2延迟/SCD/current wake | 原表/公式目视；SC reset冲突 |

| 21 | body diode/balance/masks | 原表目视；60s复位、有效mask |

| 22 | CPVS/COW/CVS/VADC | 原表目视；CPVS5和CVS双单位 |

| 23 | VADC编码/COV | 原表目视；非线性delay表 |

| 24 | CUV/GP123 | 原表目视；GP1两位与GP2/3三位区别 |

| 25 | GP456全部模式 | 原表目视；6/7编码，未用值不猜 |

| 26 | COTF/COTT/watchdog/timed wake | 原表/公式目视；RC、IWT0XX、OT下限 |

| 27 | timed wake/IRQ masks/reserved | 原表目视；不可将保留default '-'补0 |

| 28 | IRQ/FRT/只读保留 | 原表目视；FRT原文括号错误保留 |

| 29 | reserved/CV/0x90 | 原表目视；空default未知而非0 |

| 30 | 放电/预放逻辑图 | 原图目视；body-diode比较方向歧义 |

| 31 | 充电/预充逻辑图/符号 | 原图目视；不可由源码替代图的厂商定义 |

| 32 | cell共模校正公式和K表 | 原公式/表目视；公式与表数字冲突独立算术核对 |

| 33 | 修订历史 | 原页文本；V1.1/V1.2 CPVS/VAE回改，不能跨批次推default |

## 资料库实际覆盖与边界

`chips/dvc1124_2/registers.json`：145地址、1160bits NAME/MODE/default全量独立从原PDF几何比对；144 field_semantics描述和全部encoding逐项匹配原页，0差异。`reference.md`、`validation.md`、`coverage.json`检查版本/原页链接/重点公式模式保护通信章节以及known_issues；未逐字符比对reference.md全部181KB prose，不能把结构/重点核对说成全文无错。两PDF原厂图、表和关键公式均已回看；原始版面中已有括号缺损、缺省值缺失保持未知。

## D008代码实际覆盖

按`bms/products/d008/sources.txt`成员追溯生产后端 `dvc1124.c`、`dvc1124_bms.c`、`dvc1124_boot.c`、`dvc1124_special.c`、`dvc1124_config_service.c`与公共AFE profile入口；核对3 profiles、固定参数、read/write CRC与timeout/retry、registersafe mask/RC/W0C/selfclear、基本/保护/boot operating配置、20bit电流/100µV电压/K校正/NTC/die换算、boot-zero、MOS flags/请求/AUTO_DIODE、balance60s、COW状态机、sleep命令/wake/重配置、硬件故障恢复。公共storage/OTA/guard/SW protection深审和全产品生产编译由父代理主报告覆盖，勿声称本文件独立复核了这些全部路径。

主机验证：原样恢复函数抽取2构建（HW0/HW1），每构建3个规范期望反例，共6；原样采样函数抽取2帧未完成ADC反例。helper/I/O以stubs隔离；未跑完整DVC TU执行、MCU instruction仿真、HIL、真实I2C或实板。所有反例可重跑 `python independent_host.py` 与 `python freshness_host.py`。编译属于host harness compile；不是TC32固件编译或BIN。

没有确认的内容：实装完整型号/批次、16S20S短接、shunt精度/温漂、NTC R-T装配、ADC原子性/完成flag清除细则、COW实际响应、低侧CPVS应用例外、MOS/熔丝SOA、各profile保护签核、wake/reset真实波形、关机/掉电/reset后的负载和FET行为。未开展设备readback、通道/温度/电流精度量测，无法把配置推导等同有效实装寄存器值。

## 本地新基线追加验证

本轮所有代码位置按最新clean冻结内容重新读取。`old_vs_current.json`仅为旧基线→当前差异导航；恢复/bms模块token相同的事实不替代重跑测试。安全写与固定配置集中后的新函数原样提取执行，3 profile×36个寄存器独立原厂oracle结果0差异（`register_host.py`/`register_results.json`），另确认1A→20A、0ms→4ms请求被接受。当前两类恢复/采样反例重新生成，不复用旧exe或旧输出。

最终生产分析预处理使用的`BMS_DIAG_BUILD_DIRTY=0`与最新原本地clean身份一致；克隆Git换行误判不作为产品门禁问题。本子任务未开展完整生产固件链接/board验证，父代理原本地只读生产构建与外置输出结果由主报告独立记录。文档62页主题表中的原厂资料事实没有因代码基线切换而改变。

最终clean重核：相对中间dirty冻结仅8文件变化（`previous_dirty_vs_final_clean.json`）。DVC boot唯一差异为第121行注释从“返回样本资格”更正为“更新样本有效性”，与void接口一致；它没有增加ADC完成/新鲜资格。DVC驱动、恢复、profile与最终寄存器执行代码未变。三类独立probe在最终source重新抽取、重新编译、重新运行；恢复6反例/ADC2帧反例仍出现，三profile108个寄存器oracle仍0差异，1A→20A和0ms→4ms边界仍被接受。资料原件同hash，原PDF审查证据复用，不重复宣称新实板验证。
