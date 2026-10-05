# 本地SH审查覆盖与证据层级

最终身份为clean HEAD `0aaa842938c37b0016a23d8a29ce2aa416ca2c4d`，tree `0339c123dc9e0fb8192e1a625e826b6b5eeeb930`，内容hash `05fb3147811468a347d3b27538b364d6ce92d56c533a17d713bcc8e1ac689613`。`identity.json`补充最终SH各源filehash，`../local_snapshot.json`完整文件身份。源码/资料库只读，所有脚本/日志在树外。

最终整backend/profile host来自最终生产preprocess，BMS_DIAG_BUILD_DIRTY=0与原local clean身份一致，不再作dirty身份覆盖。三组probe从最终根重新compile/run，正常TC32生产编译/link/resources及公共三样本门禁、参数存储/OTA由主报告提供。host成功不是发布镜像/实板验收。

|范围|当前完成|证据/限制|
|---|---|---|
|原件两版|本轮前期fitz重新提取114页及独立原件比较，最终根按相同PDF hash复用；版本metadata/逐页文本保留|extract.py、CV1.0A/CV0.2C manifest和001..057文本；不是使用包内提取文本作源，也不声称切换最终commit后再次逐页重读|
|寄存器地图与访问/复位|180地图行/1440位标签、180reset、1440逐bit access标签独立解析0差异|map_compare.json/detail_compare.json；副作用/复位范围/语义另读原文，不靠标签自动推断|
|原页回看|同一轮已实际view_image：V1.0A16/19/21/26/28–32/34–44/46–51；V0.2C16/19/21/26/37/38/48/51；文件原件相同可复用本轮目视证据|对应先前树外原PDF渲染PNG，关键表/公式/时序/SCT/charge pump已回看；非全114页逐字/每图签核|
|最终3产品实际宏/TU|核对D011/D013/D014最终production preprocessed、产品配置源和共享control/backend/feature/NTC/port/lowlevel/profile|原local clean，dirty macro0符合身份，代码字节原样；产品source membership/正常生产构建由主报告|
|默认最终寄存器及SCT/CRLD|重新执行整个当前原control.c+原ntc.c，三产品host compile/run0；13个SCT请求、OCD共同enable、CRLD切LOAD/C+、OWD selfclear条件|control_probe.py/results.json；GPIO/SPI/register RAM/profile getter替身，未模拟完整芯片|
|本地新恢复/Freshness/清失败|重新执行整个当前生产条件backend TU三产品host compile/run0；无ready拒绝、1.6s后有效、C+存在不清OCC/移除清、SC物理窗口及复读、清失败保持无效|bms_probe.py/results.json；公共qualification=1链接替身，其他owners/peripheral替身，不能视为公共门禁/整机证明|
|profile边界|整个当前生产条件profile TU三产品host compile/run0，原厂保证范围输入/SC可表达性/共同OCDenable|profile_probe.py/results.json；外部owner替身；原厂表独立期望|
|校准报告|本地全backend TU±6553600mA回绕0重新确证|calibration函数本探针替身；实际校准可达性由主报告新快照专项，分层不冒称全链|
|在线事务回滚不一致|当前SH源码解释、跨分项22 actual TU证据引用|storage_audit/probe_results.json；不重跑，不混clearflag失败旧问题|
|功能/模式/时间/硬件|源码对原文重点量纲、CN范围、TS角色、SCONF、MOS、balance、OWD、sleep/wake、SPI、C+/LOAD|LSH-00..06以及Unknown；实际Gate、电流、NTC、SPI、低功耗未验证|

已经关闭的旧远端反例有专表：无ready资格、OCC只零流、CRLD永不切LOAD、clear失败被好读掩盖。不保留为当前缺陷。SCT、MOS_EN双OFF边界、保证范围、共用OCDenable、u16报告回绕仍由本地新证据确认；OWD原文矛盾不猜。新事务fail-safe和selfclear验证条件另列。

未完成或未知：原件授权真实性/最新厂商勘误、全114页逐字目视签核、每字段完整描述自动语义审查、BOM/NTC R–T/实际Rsense、物理比较器保护能量/恢复、SPI/电泵/Gate电气、完整ADC模型/跨通道原子快照、Flash掉电与真实OTA/实机持久值、MCU reset跨重启故障策略、睡眠耗电与所有唤醒源。主机软件证据不代替实板验证，未验证不写通过。
