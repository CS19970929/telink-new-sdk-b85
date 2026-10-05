# 本地最新 monorepo：SH3673510 三产品原厂依据审查

本报告最终身份：本地 `D:/telink/bms-monorepo` clean HEAD=`0aaa842938c37b0016a23d8a29ce2aa416ca2c4d`，tree=`0339c123dc9e0fb8192e1a625e826b6b5eeeb930`。已逐字节冻结到当前审查根，`worktree_content_sha256=05fb3147811468a347d3b27538b364d6ce92d56c533a17d713bcc8e1ac689613`，每文件哈希见`../local_snapshot.json`。所有源码行号相对最终审查根`source/`，原件两版是CV1.0A（V1.0A/2025-10/57页）和CV0.2C（Preliminary V0.2C/2024-12/57页），物理页等于印刷页。原件真实性未独立认证。

三组探针已针对最终clean快照重新执行，当前整backend/profile TU采用最终生产预处理，BMS_DIAG_BUILD_DIRTY=0与原工作区实际clean状态一致，不再使用此前dirty身份覆盖；控制TU直接包含最终原control.c和NTC表。远端6604d738及中间2fc2d203+dirty报告仅用来识别历史结论，不是最终当前验收依据。正常TC32生产构建结果由主报告提供，本文host成功不替代它或发布/实板验收。外围、公共门禁及其他owners替身明确列于脚本；不是芯片仿真/真实板。

原件提取/地图/access/reset完整审查来自同一轮前期资料审查，PDF哈希已确认与最终资料树一致；最终根复用该原件证据，没有声称最后切换提交后再次逐页重读所有PDF。版本和具体目视页见coverage.md。

## 当前确证的问题

### LSH-00 / P1：在线profile失败/回滚不一致后，普通健康采样可以重新开MOS

此项主证据由storage分项的当前本地22个实际生产TU事务探针给出，见`../storage_audit/probe_results.json`，本分项不重复跑或扩大该证据层级。场景：D014 candidate COV3750→3850并enable_mask=0，OVL一次读回错，commit错误5/rollback INCONSISTENT；requested回到before，硬件SCONF6仍00/COV仍3850，后续第5次健康采样DSG ON、apply_state3、effective_valid0。原件p26、34、36给出写命令验证及SCONF6保护开关/阈值独立写入；产品AGENTS要求重配失败fail-safe，不能靠普通SPI成功证明配置已恢复。

当前SH根因：control.c177/271把actual.valid置0/ok但不清s_control_ready或锁存配置失败；bms.c858 apply失败仅note_comm_error（149–167），它只作废采样/恢复窗口并raise错误，没有触发s_afe_reconfigure_required。note_comm_ok170–176在没有E2P_ERR时清AFE1；publish_hw_status的reconfiguration由E2P/RST触发，本场景没有这些芯片标志。outputs_healthy214–219没有actual.valid/apply_state，FET函数228只先检查control_ready，故健康样本重资格可再次授权。与已修好的专门clear_flag失败锁存是不同路径。

影响：事务报告失败但后续仍运行在部分阈值/保护开关未恢复的状态，可能硬件保护关闭。建议配置事务失败建立明确锁存禁用/要求完整验证重配，实际profile有效性进入最终输出门禁；只有寄存器/软件profile/有效配置都重新核验一致才解除。验证每个写/回读/rollback失败点、后续健康采样/MCU reset，并实板确认Gate和保护比较器，不把RAM寄存器模型当实芯片。

### LSH-01 / P1：SCT 原厂编码仍错误，移动 effective 所有者没有修正其物理意义

原件：两版p37–38：四位SCT 0..15=`[0,32,64,96,128,192,224,256,288,320,384,448,480,512,544,576] us`。当前`bms/afe/sh3673510/sh3673510_control.c:225`–236仍用八项`{2,4,8,16,32,64,128,256}`；actual.sc_delay_us也从此错误表赋值。`bms/core/bms_afe_hw_profile.c:417`现在读取actual，不再独立重复该表，但取到的值仍错；305拒绝原厂288..576us。

新证据：`control_probe_results.json`三产品整个真实control TU请求4us→寄存器码1→原厂32us；8→64、16→96、32→128、64→192、128→224；2→码0=0us。默认256→码7=256碰巧正确。`profile_probe_results.json`三产品576us拒绝。

影响：配置/持久化/effective显示与实际短路能量保护延时不符。建议以原厂16项表唯一编码/解码，明确无法精确表示请求时策略；不要把软件原指数表作独立oracle。验证每码/边界/非档位原厂期待，再用受控短路脉冲测Gate延时及原厂容差。

### LSH-02 / P1，产品策略待签核：MOS_EN=1 自主反向导通使双OFF请求不能证明双Gate关断

原件p17说明MOS_EN=1可在反方向条件下强制导通，独立于CHGMOS/DSGMOS；DSG侧OCD2/SC/WDT、CHG侧WDT有例外。p22 high-side由PUMP_EN与低侧控制共同决定。当前`sh3673510_control.c:366`–375只改SCONF2两FET位；本地整TU默认SCONF5=0x3C仍MOS_EN1。`sh3673510_bms.c:289`–291（具体低位见同函数）仍以低侧CHG_FET/DSG_FET表达状态。

影响：温度断线、软件输出禁用或通信失败中“双OFF”是否必须禁止反向通路尚未由产品需求给定；软件cache/请求不是真实Gate证据。建议分离请求/芯片high-side状态/真实Gate，明确紧急关断与允许反向策略；验证所有故障和反向电流/PUMP条件，测Gate、电流。源码写值和原文规则确证，实际反向导通与允许性未实板确认，不能声称所有故障均失效。

### LSH-03 / P2：profile仍把可编码范围当作器件保证范围，SC也没有可表达性校验

原件p36编码电压10bit×5mV；p49保证COV3.0..4.5V、CUV1.0..3.5V，CHGOT40..70℃、CHGUT−20..10℃、DSGOT45..80℃、DSGUT−40..10℃；p37SC=2/3/4/6×实际OCD2。`bms_afe_hw_profile.c:301`–312允许非零<=5115mV，温度仅通用范围/滞回，SC没有相对OCD2条件；control:223–235倍率到6停止。

新证据：实际生产条件profile整TU三产品接受COV5000、CUV4000/recover4100、CHGOT100/recover95、SC6553.5A，而最后者不可能由default OCD2×6得到。寄存器能编码不代表原厂保证准确。建议同时校验原厂保证范围、产品签核范围和量化后可表示值；原厂或供应商特批扩展范围必须有版本证据。验证边界、实际寄存器读回和比较器实测，不静默饱和。

### LSH-04 / P2：OCD1/OCD2独立enable接口与芯片共同OCD_EN不一致

原件p34 SCONF6 bit2共用，p37两个阈值无独立enable。`sh3673510_control.c:262` OR两个软件enable且203–215总写两阈值；profile validator允许OCD2only/disabled OCD1=0。新`profile_probe_results.json`全TU接受该候选，`control_probe_results.json`OCD2only使SCONF6=04，两个比较器共同工作；零值量化为最低5mV而非关闭。建议准确描述共同开关/受限能力，不能自行选择降低保护的策略。验证四种enable组合的读回/硬件触发；这两段独立证据未冒称OTA完整链。

### LSH-05 / P2：校准后SH电流报告u16仍回绕成零

原件p19/21给出16bit原始测量换算；0.1A报告是项目协议要求。`sh3673510_bms.c:670`–683先校准mA后`(uint16_t)((ma+50)/100)`。新全backend TU注入校准返回±6553600mA，三产品aux保留该值，而Ichg/IDischg均0（`bms_probe_results.json`CALIBRATED）。实际校准函数可达性由主报告新snapshot校准专项验证，不用旧结果充当当前全链通过；本探针校准owner是替身。

影响：报告和消费报告的软件保护/恢复低报，独立芯片硬保护仍可动作。建议限制校准有效工况、转u16饱和或标无效并保留诊断，验证65535档附近和极限gain/offset及正负；DVC后端已有饱和实现可作行为比较，但期望须来自范围要求。

### LSH-06 / P2，条件性反例：新CRLD切换把OWD_TRG自清视为验证失败

原件两版p16：OWD_TRG被芯片启动转换后清零；p32把该位列在SCONF3 bit0。新`sh3673510_control.c:399`–411 CRLD RMW保留bit0，写完以`verify==target`验证整个字节。

新全controlTU替身仅加入原厂明确的自清动作：初始0x47（C+与OWD触发），切LOAD写0x4B，自清后回读0x4A，CRLD实际=2正确，函数return0；三产品同样（`control_probe_results.json`CRLD_OWD_SELFCLEAR）。这是允许的异步寄存器情形，不是整个芯片仿真。实际正常主循环open-wire触发与故障交错频率及最短自清时间尚未测；不能写为每次切换必失败。

影响：良好传输/正确稳定配置可能被判采样通信失败，导致多余关断和重初始化；保留TRG也可能再次触发OWD，实际触发数量需测量。建议CRLD所有者只验证其稳定owned字段，协调OWD过程并显式处理command/selfclear位；以该寄存器特性写独立测试，实板记录OWD、CRLD、SPI和恢复交错。

## 原文冲突与当前未知

OWD奇偶：V1.0A p16 IND1奇数示例maskAAAAA与p30/44 bit0=OWD1矛盾；当前`sh3673510_feature_backend.c:126`–127仍照该示例。若详细表正确会过滤当前相位有效奇数故障，但两条原文不能自动裁定哪条对应实芯片。保留为原厂矛盾/风险，不以当前实现当正确oracle；须供应商勘误或分别断VC1/VC2等实测FLAG3/OWD原码，再确定mask。

NTC厂家受控R–T表/BOM缺失、signed16负数是否二补码、Normal C+2100/900mV产品阈值保证、high-side实际Gate、Charge Pump条件、SPI波形、sleep/wake耗电/MCU唤醒、ADC跨通道快照原子性等仍Unknown，详见后文。

## 本地重审已经撤销的旧远端结论

这些记录用于阻止把历史问题或修复当当前事实；只声明已验证到的软件层。

|远端旧问题|当前源码与新证据|当前结论|

|---|---|---|

|ADC FLAG2无ready仍3次放行|bms.c632–647记录完成时间、无ready启动5次valid0；ready两bit且1.6s后valid1|旧反例不再成立；实际ADC时间/公共门禁仍须专项与实板|

|OCC零流就清|bms.c485–490加入charger_removed或DSGING；C+28V+零流5周期clear0，C+0后clear1|旧反例撤销；产品C+阈值的物理保证尚未知|

|SC/OCD依赖LOADOFF但CRLD始终C+|control.c399–411切CRLD10；bms.c557–598等待200ms（大于tLOAD max65ms）并拒绝切换旧状态；SC LOAD窗口后clear1再复读latch0|旧配置死路已解决到软件层；新selfclear条件风险见LSH-06|

|清FLAG失败被后来成功读覆盖|bms.c522–526锁存s_flag_clear_failed；832前sample拒绝；探针失败后good状态输入future_status_reads0且valid0|旧清失败丢弃反例撤销；公共guard重init层由主报告|

|effective独立重复错误SCT表|profile.c417取actual|重复所有者已消除，但LSH-01物理映射仍错|

新host只替身公共`bms_afe_samples_qualified()`=1用于链接其他未执行入口，故本文valid0/1是backend测量资格，不是公共三样本门禁已通过的证据。实际公共门禁由主报告整TU验证。当前SC保留软件静态latch跨AFE重配，MCU冷重启仍不是持久化锁存证明。

## 当前SCONF3全部最终路径与恢复窗口

默认/static/wake/reinit=0x44（CGR_WK1/LD_WK00/CRLD01）。有SC/OCD或软件DSG_OC活动时，主采样切换为0x48（CRLD10）；同一帧检测changed或初次模式则不认旧状态，等待200ms后以LOADOFF=1且LOADON=0作为load_removed。故障解除后恢复0x44并等待后以新VADC+C+产生charger证据。两模式互斥，与原件p23/32一致，当前不再有“从不写LOAD”的错误。

OWD RMW保留当前CRLD：C+模式enable0x46/trigger0x47（TRG自清回0x46）；LOAD模式enable0x4A/trigger0x4B（自清0x4A）；disable分别0x44/0x48。sleep仅将CGR_WK置1，保持当时CRLD；wake重新0x44。LD_WK始终00，负载唤醒是未启用选择，是否符合产品需求需签核。出现整字节验证TRG競态见LSH-06。

SC十个200ms物理窗口+清标志成功+后续复读无SC且load_removed才清latch；OCD要求LOAD移除或CHGING、OCC要求charger移除或DSGING，且硬OC恢复只在本次VADC/CADC两ready的fresh帧推进。无release条件/通信失败会作废窗口；clear失败保持fail-safe直到init。200ms样本窗口/1.2s温度startup/400msADC年龄是软件策略，芯片可保证范围及芯片失效实测仍不能由host代替。

## 当前默认最终配置与三产品差异

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

## 资料包原件对照与应补清单

重新提取两个PDF全114页；独立解析p28–30原始寄存器总表，逐版本90地址×8位标签/名称（180行、1440位标签）与 registers.json比较，0差异，见`map_compare.json`。另`detail_compare.py`独立解析p31–44每个实际详细表的access/reset行，处理OVH/OVL各自复位行、共享多地址表、跨页表及“第7 位”空格；两版各90地址复位值（180）及各位access标签（1440）0差异，见`detail_compare.json`。字段说明中的副作用/复位范围及编码/语义不能从标签匹配自动推出，以下关键detail按原页核查。reference已经正确记录SCT16档、OCD/OCC单位及原文若干矛盾，当前驱动错误不能归因于reference写错SCT。

原件冲突/未明确规定，不能合成一个“正确值”：

|项目|页（两版相同页号，差异另写）|证据/处理|

|---|---|---|

|SPI write CRC覆盖|26|正文有length，写时序图无length；源码采用图。需供应商/实芯片确定，不能说正文与代码一致|

|负电流符号编码|19、42–43|只有有符号16位，没有明确负数编码；保留signed_binary_encoding=null|

|OWD奇偶示例|V1.0A16与30/44|见本报告原文冲突；reference已标冲突|

|CADCDL bit1/数据高字bit3|30与42–43|总表CADCDL bit1重复.0；CELL6H/CELL16H detail bit3重复.12，对应另处.1/.11；source定义不可照错字|

|OCD2延时上限|37与49|公式最大400ms，电气表425ms；编码值与保证/容差分开|

|UV最大延时|36–37与49|10.01s vs10.1s；作为原文精度/矛盾保存|

|预放电最大|32与48|3.01s vs3s；不能静默统一|

|WDT手动预放电|17与22|例外表述不同；具体安全策略需答复|

|RST1重置范围|11、26、39|复位表默认1与说明不含软件复位、Powerdown/SHIP表述差异；初始化不可从一次读取推所有reset原因|

|模式表与defaults|9、44–45|√(O)表示可选，不是默认关闭；对照寄存器默认启用|

|目录/应用图顺序|6–7与56|低侧/高侧引用顺序不一致；按实际电路页标题|

|VVCPTH基准节点|V1.0A48|门限参考节点未明确；不猜该门限是MCU测得哪个节点|

|多byte/跨通道快照原子性|19–21、26|未明确冻结/原子承诺；合成快照需软件时间边界与实测|

版本差异必须保留：V0.2C为Preliminary；新旧p48 charge pump建立条件由1µF/9V最大250ms变为0.47µF/6、7、8V典型100ms最大125ms；p49 SC延时误差最大旧32us新64us。引脚图/电气规格及修订条目按版本存档，不能把新误差与旧测试条件拼在一张表。独立逐页文本差异清单`independent_text_diff_pages.json`有27页；库version差异26页未列p26“Write(0x01)CMD”到“Write CMD (0x01)”的图标签排列变化，此项是低影响完整性补充，不是实质协议变更。

应补：每个关键字段加具体物理页/印刷页、table/cell与原文截图定位；保证范围与寄存器可编码范围分栏；70ms更新时间注释显式指向均衡/open-wire暂停及IDLE四倍；read-clear寄存器重试/多消费者风险；将原文未决项目与供应商答复版本关联；用型号清单明确SH3510适用数据。不能把整个extract_text或元数据创建时间当成厂家版本签核。

## 硬件与规格缺口

1. 受控原理图、BOM、Rsense实装与误差、NTC厂家R–T表及器件版本。产品文件写10K-3435，`sh3673510_ntc.c`注释只有existing calibration；表和ADC/阈值计算自洽不等于真实传感器正确。恒β3435估算不应充当厂家R–T期望判表错，需温箱/电阻箱逐点测。

2. 电流零偏、增益、二补码、CADC/VADC跨读快照、新鲜度、ADC失效/通信失败真实Gate行为。

3. SPI Mode3/500kHz与5µs CS等待源码符合p26/51最低要求的方向；真实SCLK/CS/SDO setup/hold、reset脉冲和板级信号质量未经逻辑分析仪验证。

4. charge pump电容/电压/建立时间与每版规格测试条件，实际gate及PUMP状态；读取CHG_FET低位不是高侧MOS已导通的电气证明。

5. C+运行时2100/900mV判定是软件工程阈值；p50睡眠唤醒0.9..2.1V范围在不同内部下拉/状态条件，不能移用为Normal测量的保证阈值。需charger/load板级测试。

6. LD_WK=00的选择、wake源/MCU deep sleep/IRQ、WDT默认32.34s刷新节奏和均衡20s刷新实际最坏延时，需完整主循环与实板耗电/唤醒测量；本文未签核其产品需求。

7. 持久化Flash内容、出货老版本迁移、OTA掉电与reset故障历史恢复由主报告专项审查；不能从default最终表推每台设备当前寄存器。本文明确当前SC静态latch保留AFE reinit，但不是跨MCU reset持久化证明。
