# DVC1124-2 完整工程参考（数据手册 V1.1 + 参考手册 V1.2）

## 0. 适用范围、证据与阅读规则

- 本文只适用于 DVC1124-2 家族的 DVC1124-22 和 DVC1124-24。官方说明二者唯一差别是级联应用中的 I2C 硬线地址编码引脚；不能把后缀 -22/-24 解释为支持串数。器件覆盖 4～24 串。[DVC1124-2数据手册_V1.1.pdf PDF p.1](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=1) [DVC1124-2数据手册_V1.1.pdf PDF p.23](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=23) [DVC1124-2数据手册_V1.1.pdf PDF p.29](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=29)
- 数据手册《DVC1124-2数据手册_V1.1.pdf》共 29 页，末页版本日期为 2024/7/24，负责电气规格、引脚、功能时序及应用；参考手册《DVC1124-2参考手册_V1.2.pdf》共 33 页，末页版本日期为 2025/09/26，负责寄存器、驱动逻辑及 MCU 二次校准。它们是互补文档，不是同类文件可互相替代的新旧版。两份文件 PDF 页码与印刷页码相同。[DVC1124-2数据手册_V1.1.pdf PDF p.29](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=29) [DVC1124-2参考手册_V1.2.pdf PDF p.33](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=33)
- 【官方事实】表示原文件直接给出；【推导】表示依据所列公式或位图计算；【工程建议】是保守驱动/硬件策略；【项目假设】必须由实际电池、原理图、分流电阻、NTC 与测试确认。未给出的值保持“未说明”或 JSON null，不补成零。
- 交付结构：本文件是综合说明 + 全字节位图 + 完整字段编码字典；registers.json 包含每一字节、逐位访问属性、复位已知掩码及字段语义；coverage.json 是逐页覆盖索引；validation.md 是图像核验及未决问题清单。原文逐页附录仅作追溯，不替代综合说明。
- 快速风险：警报写零清除、转换标志读清；睡眠关闭电压/一级电流保护；复位时多数软件阈值关闭；高压总量存在 100V 应用说明、120V 工作条件和 132V 绝对极限三个不同层级；二次校准表、SCD 零编码、IWT 低编码、总压测量节点及放电续流图存在待确认处，见第 9 节。[DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6) [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7) [DVC1124-2数据手册_V1.1.pdf PDF p.12](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=12) [DVC1124-2数据手册_V1.1.pdf PDF p.1](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=1) [DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6) [DVC1124-2参考手册_V1.2.pdf PDF p.32](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=32)

## 1. 器件能力与系统架构

【官方事实】高压 BCD 监控 AFE，带两个独立 Σ-Δ ADC，可同时测量电压与电流；集成电荷泵、高边 CHG/DSG NFET 驱动、PCHG/PDSG PFET 预充/预放驱动、内部被动均衡及可驱动外部 NPN 的均衡结构。VADC 轮询单体、总压、PACK、LOAD、内核温度、V1P8 与最多 6 路 GP。CADC 输出 CC1 与 CC2；硬件二级过流与短路比较器不依赖正常 VADC 工作。面向电动自行车、轻型摩托、UPS、储能及多种锂/钠电池组。[DVC1124-2数据手册_V1.1.pdf PDF p.1](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=1) [DVC1124-2数据手册_V1.1.pdf PDF p.11](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=11) [DVC1124-2数据手册_V1.1.pdf PDF p.13](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=13) [DVC1124-2数据手册_V1.1.pdf PDF p.20](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=20)

【官方事实】正常/休眠/关机的典型总电流约为 270/60/1 μA；这是功能描述值。电气表分列 VTOP 和 VREG 电流，不能在有外部 MCU、LDO 负载或不同驱动配置时当作整个系统功耗上限。[DVC1124-2数据手册_V1.1.pdf PDF p.1](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=1) [DVC1124-2数据手册_V1.1.pdf PDF p.8](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=8) [DVC1124-2数据手册_V1.1.pdf PDF p.12](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=12)

【官方事实】首页举例的标称电池组为 18V、24V、36V、48V、60V、72V，化学体系示例为磷酸铁锂、三元锂、钛酸锂、钠离子。首页称使用车规级高压 BCD 工艺，这不等于本资料证明 AEC-Q100 或整车功能安全认证。原厂联系信息：南京集澈电子科技有限公司 / 宜矽源半导体南京有限公司，网址 http://www.easypowerinc.com，技术支持 fae@devechip.com；仅转录文件上的联系方式，未另行验证当前有效性。[DVC1124-2数据手册_V1.1.pdf PDF p.1](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=1)

### 1.1 引脚全表

|引脚号|名称|类型与连接要点|来源|
|---|---|---|---|
|1～24|C24～C1|I；编号 p 对应 C(25-p)，每路连接对应电池正极|[DVC1124-2数据手册_V1.1.pdf PDF p.3](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=3) [DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4)|
|25|C0|I；最低单体负极|[DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4)|
|26 / 27|SRN / SRP|I；分流电阻负端（靠近 VSS）/正端；放电时 SRP-SRN 为正|[DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4) [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)|
|28|VSS|电源地，最低单体负极|[DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4)|
|29 / 30|GP6 / GP5|I/O；热敏/模拟输入、低边 DSG/CHG、INT|[DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4)|
|31|GP4|I/O；热敏/模拟输入、DON 放电硬线控制|[DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4)|
|32 / 33|GP3 / GP2|I/O；热敏/模拟输入、低边 PCHG/PDSG、INT|[DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4)|
|34|GP1|I/O；热敏/模拟输入、CON 充电硬线控制|[DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4)|
|35|V1P8|O；1.8V，近引脚 1μF 到 VSS；内部数字电源，不能作为外部供电轨使用|[DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4) [DVC1124-2数据手册_V1.1.pdf PDF p.26](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=26)|
|36|V3P3|O；3.3V/50mA 外部供电，近引脚 1μF 到 VSS|[DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4) [DVC1124-2数据手册_V1.1.pdf PDF p.8](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=8)|
|37 / 38|SDA / SCL|I/O / I；I2C 数据/时钟，外部上拉|[DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4) [DVC1124-2数据手册_V1.1.pdf PDF p.23](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=23)|
|39|VREG|S；5V 电源输入，近引脚 1μF 到 VSS|[DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4)|
|40|VBASE|O；外接 NPN 预稳压器基极驱动，近引脚 1μF 到 VSS|[DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4)|
|41|LOAD|I/O；负载检测/上拉及关机唤醒输入|[DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4) [DVC1124-2数据手册_V1.1.pdf PDF p.12](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=12) [DVC1124-2数据手册_V1.1.pdf PDF p.21](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=21)|
|42 / 43|PDSG / PCHG|O；高边预放/预充 PFET 驱动|[DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4)|
|44|PACK|I；充电器检测|[DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4)|
|45 / 46|DSG / CHG|O；高边放电/充电 NFET 驱动|[DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4)|
|47|VCP|O；电荷泵，正常高边应用近引脚 2.2μF 到 VTOP|[DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4)|
|48|VTOP|S；电池组正极，近引脚 1μF 到 VSS|[DVC1124-2数据手册_V1.1.pdf PDF p.4](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=4)|

### 1.2 绝对极限与工作条件分开使用

【官方事实】下表绝对极限不是可长期工作的目标；所有差分都按所列参考节点，不能混用地参考与栅源/电荷泵差分限制。[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)

|对象/条件|绝对最大额定范围|推荐工作范围|来源|
|---|---|---|---|
|VTOP-VSS|-0.3～132V|8～120V；另有 VTOP-C24=-1～120V|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|
|C0-VSS|-0.3～6V|0～0.3V|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|
|Cn-VSS，n=1…24|-0.3～132V|单体工作条件见下一行，不能直接由绝对极限推导|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|
|Cn-C(n-1)|-0.3～132V（原表确如此）|0～5.0V|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|
|VCP-VSS|-0.3～132V|0～120V|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|
|VCP-VTOP|-0.3～15V|0～12V|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|
|SRP-VSS、SRN-VSS|-0.3～6V|各自及 SRP-SRN 均 -150～150mV|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|
|DSG-VSS|-0.3～132V|0～VCP|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|
|CHG-VSS|-0.3～132V|VTOP～VCP|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|
|PDSG、PCHG、PACK、LOAD 对 VSS|-0.3～132V|0～120V|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|
|VBASE-VSS|-0.3～15V|0～6.5V|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|
|VBASE-VTOP|-132～0.3V|未单独给出|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5)|
|VREG-VSS|-0.3～6V|0～5.5V|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|
|V3P3-VSS / V3P3-VREG|-0.3～6V / -6～0.3V|对 VSS 为 0～3.3V|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|
|V1P8-VSS / V1P8-VREG|-0.3～2V / -6～0.3V|对 VSS 为 0～1.8V|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|
|SCL、SDA、GP 对 VSS|-0.3～6V|数字 0～5V；GP 热敏/模拟 0～1.8V|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|
|引脚电流|C0～C24：0～25mA；VREG/V3P3：0～50mA；其他：0～1mA|均衡驱动 0～25mA|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|
|温度|额定结温 -40～85°C；储存 -65～150°C|工作 -40～85°C|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|
|ESD|HBM ±2000V；CDM ±250V|不是带电插拔系统级浪涌额定值|[DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5)|

【工程建议】同时约束 VTOP、VCP 对地及电荷泵差分；在开启升压时不能将 VTOP=120V 与 VCP-VTOP=12V 两个上限简单叠加为可用状态。首页“电池包总压不超过 100V”也应保留为应用范围声明。[DVC1124-2数据手册_V1.1.pdf PDF p.1](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=1) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)

### 1.3 精度、漏电、功耗、均衡与驱动电气值

|项目|官方数值与原列属性|来源|
|---|---|---|
|VADC 输入量程|单体 -0.3～5.0V；GP/V1P8 0～1.98V；C24/PACK/LOAD 0～120V。测量量程不扩大推荐工作电压|[DVC1124-2数据手册_V1.1.pdf PDF p.7](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=7)|
|VADC 分辨率|单体/GP/V1P8 100μV/bit；总压/PACK/LOAD 12.8mV/bit；有符号单体模式另见第 4 节|[DVC1124-2数据手册_V1.1.pdf PDF p.7](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=7) [DVC1124-2数据手册_V1.1.pdf PDF p.14](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=14)|
|VADC 典型偏移/增益误差|1mV / 0.1%；25°C 测量误差典型 ±5mV（单体/GP/V1P8）及 ±500mV（总压/PACK/LOAD）；不是表中最大保证值|[DVC1124-2数据手册_V1.1.pdf PDF p.7](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=7)|
|VADC 输入漏电|未测量：单体/GP/V1P8 典型 ±10nA，总压/PACK/LOAD 典型 10nA；测量：单体/GP/V1P8 典型 ±1μA，总压/PACK/LOAD 最大 31μA|[DVC1124-2数据手册_V1.1.pdf PDF p.7](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=7)|
|CADC|±150mV；CC1 5μV/bit，CC2 0.3125μV/bit；典型偏移 50μV、增益误差 0.1%、测量误差 ±200μV；SRP/SRN 未测量/测量漏电典型 ±10nA/±1μA|[DVC1124-2数据手册_V1.1.pdf PDF p.7](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=7) [DVC1124-2数据手册_V1.1.pdf PDF p.8](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=8)|
|VTOP 电流|正常/休眠/关机典型 15/8/1μA|[DVC1124-2数据手册_V1.1.pdf PDF p.8](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=8)|
|VREG 电流|正常/休眠/关机典型 255/52/0.1μA|[DVC1124-2数据手册_V1.1.pdf PDF p.8](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=8)|
|LDO|V3P3 典型 3.3V，输出电流 0～50mA|[DVC1124-2数据手册_V1.1.pdf PDF p.8](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=8)|
|均衡内阻 n=4…24|最小/典型/最大 173/290/441Ω|[DVC1124-2数据手册_V1.1.pdf PDF p.8](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=8)|
|均衡内阻 n=1…3|最小/典型/最大 123/170/204Ω；各路均衡电流 0～25mA|[DVC1124-2数据手册_V1.1.pdf PDF p.8](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=8)|
|电荷泵|升压 6～12V；2.2μF、10V 条件下启动时间典型 91ms（无最大值）|[DVC1124-2数据手册_V1.1.pdf PDF p.8](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=8)|
|NFET 上升/下降时间|CL=47.2nF、RGATE=51Ω、升压 10V；开启 VGS 从 0V 到 4V：CHG 44μs、DSG 26μs；关闭 CHG 199μs、DSG 6.5μs。注意这些数字原表位于“最小值”列，不是最大关断保证|[DVC1124-2数据手册_V1.1.pdf PDF p.8](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=8)|
|预充/预放输出|CL=860pF、RPU=270kΩ，开/关沿各 2.0ms（原表最小值列）；开启下拉电流最小/典型/最大 23.2/36.0/54.4μA|[DVC1124-2数据手册_V1.1.pdf PDF p.8](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=8)|
|采样输入 RC 推荐|220Ω、0.1μF 典型|[DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|

## 2. I2C 协议、地址、CRC 与访问副作用

### 2.1 总线与地址

【官方事实】从机 I2C，最高 100kHz，无 SCL clock stretching；SCL/SDA 无内部上拉，可用外部 5V 上拉。每字节 MSB 先传、随后 ACK；起止条件及 repeated START 遵守所列标准规则。[DVC1124-2数据手册_V1.1.pdf PDF p.23](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=23)

- 普通应用在线地址字节为写 0x40 / 读 0x41；【推导】采用 7-bit 地址 API 时应传 0x20，而非 0x40。[DVC1124-2数据手册_V1.1.pdf PDF p.23](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=23)
- 特殊级联模式以 SRN 接 VSS、SRP 接 VREG 激活；-22 的在线地址格式是 11000 GP2 GP1 R/W，-24 是 110 GP4 GP3 GP2 GP1 R/W。占用的 GP 固定为数字输入。【推导】对应 7-bit 地址分别为 0x60～0x63、0x60～0x6F。此接法改变 SRP 用途，不是保留电流采样的普通地址切换。[DVC1124-2数据手册_V1.1.pdf PDF p.23](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=23)
- 寄存器地址地图 0x00～0x90，共 145 字节，连续地址到边界后回卷；但读写起始 RA 合法条件文字只写 0x00～0x8F。不要把此矛盾隐藏成“任意地址都能直接访问”；生产驱动不应跨尾部回卷读写。[DVC1124-2数据手册_V1.1.pdf PDF p.23](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=23) [DVC1124-2参考手册_V1.2.pdf PDF p.5](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=5) [DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)

|时序/电平|要求|来源|
|---|---|---|
|fSCL|最大 100kHz，50% 占空比测试条件|[DVC1124-2数据手册_V1.1.pdf PDF p.9](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=9)|
|tHD;STA / tLOW / tHIGH|最小 4.0 / 4.7 / 4.0μs|[DVC1124-2数据手册_V1.1.pdf PDF p.9](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=9)|
|tHD;DAT / tSU;DAT|最小 0 / 250ns|[DVC1124-2数据手册_V1.1.pdf PDF p.9](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=9)|
|tr / tf|10→90% 上升最大 1000ns，90→10% 下降最大 300ns|[DVC1124-2数据手册_V1.1.pdf PDF p.9](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=9)|
|tSU;STO / tBUF|最小 4.0 / 4.7μs|[DVC1124-2数据手册_V1.1.pdf PDF p.10](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=10)|
|外部上拉电阻|原表最小 1.5kΩ；按总线电容和电平验证|[DVC1124-2数据手册_V1.1.pdf PDF p.10](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=10)|
|高/低输入|VIH 最小 1.25V；VIL 最大 0.9V；漏电最大 ±1μA|[DVC1124-2数据手册_V1.1.pdf PDF p.9](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=9)|
|SDA 输出低|下拉 1mA 时 VOL 最大 0.3V|[DVC1124-2数据手册_V1.1.pdf PDF p.9](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=9)|
|总线事务超时|64ms；从 START 开始，repeated START 不重启计时；STOP 结束。超时后只响应新的 START|[DVC1124-2数据手册_V1.1.pdf PDF p.10](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=10) [DVC1124-2数据手册_V1.1.pdf PDF p.24](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=24)|

### 2.2 CRC 帧定义

【官方事实】多项式 x^8+x^2+x+1，初始值 0x00。首字节的 CRC 覆盖地址/寄存器指针，后续每字节单独 CRC，不能将整个 burst 当作连续累计 CRC。[DVC1124-2数据手册_V1.1.pdf PDF p.23](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=23) [DVC1124-2数据手册_V1.1.pdf PDF p.24](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=24)

- 写：START，SA+W，ACK，RA，ACK，DATA0，ACK，CRC0，ACK，DATA1，ACK，CRC1，ACK…STOP。CRC0=CRC(SA+W,RA,DATA0)；CRC1=CRC(DATA1)，以此类推。只有对应 CRC 正确后该 DATA 才写入；CRC 错则该 DATA 丢弃、NACK、结束。此前已 ACK 写入的字节不保证回滚。[DVC1124-2数据手册_V1.1.pdf PDF p.23](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=23) [DVC1124-2数据手册_V1.1.pdf PDF p.24](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=24)
- 读：START，SA+W，ACK，RA，ACK，repeated START，SA+R，ACK，DATA0，主 ACK，CRC0，主 ACK，DATA1，主 ACK，CRC1…末尾 CRC 主 NACK，STOP。CRC0=CRC(SA+W,RA,SA+R,DATA0)；CRC1=CRC(DATA1)，以此类推。[DVC1124-2数据手册_V1.1.pdf PDF p.24](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=24)
- 【工程建议】先校验每个数据字节再发布到上层；任何 CRC/NACK 失败记为该读批次无效。对 RC 状态寄存器，即使主机 CRC 校验失败也不能假设硬件标志还在。两份文档未明确 CRC RefIn/RefOut/XorOut、硬件测试向量和 burst 原子快照保证；若用常规 MSB-first、poly=0x07、init=0 实现，需把其他 CRC 参数列为板级验证项，不能冒称文档已给出。[DVC1124-2数据手册_V1.1.pdf PDF p.23](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=23) [DVC1124-2数据手册_V1.1.pdf PDF p.24](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=24) [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)

### 2.3 状态、清除、命令与保留位

|地址/字段|副作用与安全规则|来源|
|---|---|---|
|0x00 IWTF/COV/CUV/OCD1/OCC1/OCD2/OCC2/SCD|全为写 0 清除，写 1 无效；不是 W1C。选择性清除可按【工程建议】发送“目标位 0、其余位 1”，避免读-改-写清掉期间新出现事件|[DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)|
|0x01 B6/B5/B4 VADF/CC1F/CC2F|读取该地址后自动清零；统一采集一份状态并分发，避免多个线程/调试轮询消费事件|[DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6) [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)|
|0x01 CST[3:0]|仅写 0xD/0xE/0xF 有效，依次为全部寄存器复位/休眠/关机；其他写值无效；不允许通用 RMW 偶然写成命令|[DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)|
|0x55 CAMZ|写 1 触发一次 CADC 校准后自清，写 0 无效；R/W1 不是清除故障的 W1C|[DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17) [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)|
|0x51 LDPU / 0x67～0x69 CB|分别负载上拉/均衡，60s 自动复位；均衡写入可重装定时器|[DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15) [DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21) [DVC1124-2数据手册_V1.1.pdf PDF p.20](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=20)|
|0x6D COW|写 1 开始断线检测，1s 自动回零|[DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)|
|0x76 COTF|读清内核过温关机历史标志；同一字节下 7 位为可写 COTT|[DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)|
|0x5D、0x8E 等未命名位|有已知非零默认和混合 R/RW；名字空白不表示可自由写 0。详见 JSON 逐位 reset_known_mask|[DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19) [DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)|

## 3. 电源状态、唤醒、复位与配置保存

|模式|测量与保护|保存与电源/驱动|进入与退出|来源|
|---|---|---|---|---|
|正常|支持全部测量/保护/均衡，但仍受各使能、阈值及屏蔽控制|I2C 可访问、MCU 可配置|从睡眠或关机唤醒进入|[DVC1124-2数据手册_V1.1.pdf PDF p.12](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=12)|
|休眠|VADC、CADC、COV/CUV、OCD1/OCC1、I2C 普通通信关闭；电流唤醒检测是独立休眠功能|寄存器保留；VBASE、可配置的 V3P3、电荷泵/FET、OCD2/OCC2/SCD 保持进入前状态|CST=0xE；可由 I2C 唤醒、定时、电流、二级过流、短路或 PACK 比 VTOP 高约 2V 退出|[DVC1124-2数据手册_V1.1.pdf PDF p.12](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=12) [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7) [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18) [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)|
|关机|除 I2C 唤醒及充电器检测等唤醒通路外关闭，功能表还列 LOAD 唤醒|寄存器复位初始状态；不能认为配置永久保存|CST=0xF 或内核过热；I2C 唤醒、PACK>VTOP+2V、LOAD>2V 可进入正常|[DVC1124-2数据手册_V1.1.pdf PDF p.12](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=12) [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)|

【官方事实】CST 读值：0=从关机唤醒；1=I2C；2=定时；3=放电电流；4=充电电流；5=OCD2；6=OCC2；7=SCD；8=充电器；9/A=N/A；B=等待关机；C=等待休眠。D 是复位命令值，不能当作普通模式。[DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)

【官方事实】CAES=1 且 CWT 非零时使用休眠电流唤醒；CWT×10μV，CWT=0 禁止；C1OS=0/1/2/3 的检测时间为 4/8/16/32ms。定时唤醒 TWSE：0 禁用，1…5 对应 10…50s，6 对应 1min，7…15 对应 2…10min。TIWK 为只读状态。[DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18) [DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20) [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26) [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)

【官方事实】V3P3EW、V3P3ES 分别控制正常和睡眠 LDO，复位均为 1。IWT 的 100/101/110/111 编码是 4/8/16/32s，其余 0XX 禁用；V3P3M=1 时 I2C 看门狗超时会关闭 V3P3 1s 再启动。IWTS 经下一次有效通信解除，IWTF 仍需主动写零清除。不要将 64ms 总线事务超时与秒级 I2C 看门狗混为一谈。[DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26) [DVC1124-2数据手册_V1.1.pdf PDF p.19](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=19) [DVC1124-2数据手册_V1.1.pdf PDF p.24](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=24)

【工程建议】未发现用户可写 NVM/EEPROM 保存机制或提交配置命令；FRT 明确是工厂 FUSE 只读。只把睡眠保留当作睡眠保留；上电、关机唤醒及 CST=0xD 后重新写入并读回项目配置。供应版本/实物 CV 读值应入日志，CV 的期望常数官方未给出。I2C 唤醒的具体脉冲宽度、上电可通信时间、模式命令完成最大时间均未说明，不编造固定延时。[DVC1124-2数据手册_V1.1.pdf PDF p.12](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=12) [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7) [DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)

## 4. 测量、单位、符号、校准与数据新鲜度

### 4.1 原始数据组装

【官方事实 + 位图推导】高位字节在低地址。下表 R[a] 表示剥离并校验 CRC 后的一个寄存器数据字节；sign16/sign20 是相应宽度二补码符号扩展。测量寄存器均只读，未列 DEFAULT，因此 JSON 的 reset=null。[DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7) [DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8) [DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9) [DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10) [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)

|测量量|地址与组装|物理量|来源|
|---|---|---|---|
|CC1|0x02～03；sign16((R02<<8)\|R03)|Vsense=code×5μV；I[A]=Vsense[V]/Rshunt[Ω]，正放电、负充电|[DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)|
|CC2|0x04～06；sign20((R04<<12)\|(R05<<4)\|(R06>>4))|Vsense=code×0.3125μV；不要将 0x06 低 4 位 FET 标志合入数值|[DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)|
|VTP|0x07～08，uint16|code×12.8mV；RM 称 VTOP，DS 图为 C24 总压，见冲突条目|[DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8) [DVC1124-2数据手册_V1.1.pdf PDF p.14](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=14)|
|VPK / VLD|0x09～0A / 0B～0C，uint16|PACK / LOAD 电压=code×12.8mV|[DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)|
|VCT|0x0D～0E，uint16|Tdie[°C]=code×0.24467-271.03|[DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9) [DVC1124-2数据手册_V1.1.pdf PDF p.15](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=15)|
|V1P8|0x0F～10，uint16|code×100μV|[DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9)|
|VGPn，n=1…6|起始地址=0x11+2(n-1)，uint16|code×100μV；模式须设热敏或模拟输入|[DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9) [DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10)|
|VCn，n=1…24|起始地址=0x1D+2(n-1)|CVS=0：uint16×100μV，负值舍弃为零；CVS=1：sign16×200μV，保留负值|[DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10) [DVC1124-2参考手册_V1.2.pdf PDF p.11](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=11) [DVC1124-2参考手册_V1.2.pdf PDF p.12](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=12) [DVC1124-2参考手册_V1.2.pdf PDF p.13](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=13) [DVC1124-2参考手册_V1.2.pdf PDF p.14](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=14) [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15) [DVC1124-2数据手册_V1.1.pdf PDF p.14](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=14)|
|VVOS|0x4D～4E，uint16|VADC 校准电压，100μV/LSB|[DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)|
|CVOS|0x4F～50，sign16|CADC 校准电压，5μV/LSB；手册未明确要求 MCU 从每次 CC 结果再次相减|[DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15) [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)|

【工程建议】CC1/CC2 虽被称为库仑计，所列寄存器是电流/分流电压测量结果，不是无限累计的库仑总量。SOC/电荷累计需要 MCU 按有效测量时刻积分、记录丢帧和休眠空档；分流阻值、温漂、方向及系统增益是项目参数。[DVC1124-2数据手册_V1.1.pdf PDF p.13](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=13) [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)

### 4.2 时间片与更新

【官方事实】CC1 C1OW=0/1/2/3 时周期 0.5/1/2/4ms；CC2 固定 256ms。每次完成分别置 CC1F/CC2F；CC2 可产生 INT。VADC 每轮由 36 个测量片和 6 个热敏建立延时片组成；热敏片前等待 1ms。VADC 完成置 VADF 并可发 INT。[DVC1124-2数据手册_V1.1.pdf PDF p.13](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=13)

|VAO|单片 tVM|最大整轮 tVADC|来源|
|---|---|---|---|
|0|0.791ms（RM 写 0.79ms）|34.5ms|[DVC1124-2数据手册_V1.1.pdf PDF p.13](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=13) [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)|
|1|1.54ms|61.4ms|[DVC1124-2数据手册_V1.1.pdf PDF p.13](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=13) [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)|
|2|3.03ms|115ms|[DVC1124-2数据手册_V1.1.pdf PDF p.13](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=13) [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)|
|3|6.02ms|223ms|[DVC1124-2数据手册_V1.1.pdf PDF p.13](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=13) [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)|

【官方事实】时间片次序：校准、C24 对地、PACK、LOAD、芯片温度、V1P8、GP1～GP6、VC1～VC24。校准/C24/VC1～VC4 六片不能屏蔽；VC5～VC24 在 CM 屏蔽且 CMM=0 时跳过，CMM=1 则仍测屏蔽通道；其他被屏蔽项跳过。CM 同时控制单体保护，不只是节省采样时间。[DVC1124-2数据手册_V1.1.pdf PDF p.13](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=13) [DVC1124-2数据手册_V1.1.pdf PDF p.14](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=14) [DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21) [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)

【官方事实】VASM=0 连续轮询；VASM=1 与 CADC CC2 同步开始，完成后进入低功耗等待。VAMP=0/1/2/3 表示每 1/2/4/8 个 CC2 周期测一次，即【推导】256/512/1024/2048ms 的起始间隔。默认 VASM=1、VAMP=0、VAO=1，不能因首页“35ms”宣传值而宣称默认每 35ms 有全通道新数据。[DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22) [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23) [DVC1124-2数据手册_V1.1.pdf PDF p.1](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=1) [DVC1124-2数据手册_V1.1.pdf PDF p.13](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=13)

【工程建议】将采样配置、CST、VADF/CC flags、校验结果、主机单调时间一起保存。寄存器连续读取并未获明确原子快照承诺，跨周期更新可能撕裂；先依据完成事件安排 burst、检查耗时/一致性，并在台架验证。被屏蔽、禁用、刚唤醒或未完成转换的通道值不能当作新鲜有效数据。[DVC1124-2数据手册_V1.1.pdf PDF p.13](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=13) [DVC1124-2数据手册_V1.1.pdf PDF p.24](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=24) [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)

### 4.3 热敏电阻与内核温度

【官方事实】GP 热敏输入使用约 10kΩ 内部上拉，文档只支持常温 10kΩ NTC，图示 103-AT。内部上拉实际值 RPU=6800Ω+25Ω×FRT（0x7E，8-bit unsigned）。RNTC=VGP_code/(V1P8_code-VGP_code)×RPU。测量前接入上拉并等 1ms，完成后断开。滤波公式 CF=1ms/(10×RPU)，一般推荐 10nF，线长时适当减小。[DVC1124-2数据手册_V1.1.pdf PDF p.15](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=15) [DVC1124-2数据手册_V1.1.pdf PDF p.16](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=16) [DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)

【工程建议】使用同一电压标度的 GP 与 V1P8 结果，检查分母≤0、开短路、过期 V1P8；不能直接将 GP code 当温度。阻值转摄氏需实际 NTC 的 R/T 表或 B/Steinhart-Hart 系数，文件没有提供通用 B 值。内核温度必须定期监测，不能用环境 NTC 代替芯片结温。[DVC1124-2数据手册_V1.1.pdf PDF p.15](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=15) [DVC1124-2数据手册_V1.1.pdf PDF p.16](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=16)

### 4.4 电池共模二次校准

【官方事实】RM 要求 MCU 二次校准：VCAC[n]=VCBC[n]/K[n]；K[n]=1+p1×VCM[n]+p2×VCM[n]^2；p1=0.35×10^-6，p2=-0.12×10^-6；VCM[1]=0，VCM[n]=VCM[n-1]+VCAC[n-1]（n≥2）。VCBC 为校准前单体读值、VCAC 为校准后值，共模单位按同页表是 V。应从最低有效单体顺序计算。[DVC1124-2参考手册_V1.2.pdf PDF p.32](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=32)

【官方事实】同页速查表：VCM[V] / K = 0～24/1.0000，24～40/0.9999，40～51/0.9998，51～60/0.9997，60～68/0.9996，68～75/0.9995，75～82/0.9994，82～88/0.9993，88～93/0.9992，93～99/0.9991，99～104/0.9990，104～109/0.9989，109～113/0.9988，113～117/0.9987，117～120/0.9986。边界相接，原表未规定开闭区间。[DVC1124-2参考手册_V1.2.pdf PDF p.32](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=32)

【推导/未决】例如 VCM=100V，公式给 K=0.998835，而表给 0.9990；VCM=120V 时公式给 0.998314，表给 0.9986。差异不应静默忽略或自动“修正”某一系数。量产前要求厂商确认采用公式还是速查表，并留存实测校准证据；本资料同时保留两者，不替工程项目作选择。[DVC1124-2参考手册_V1.2.pdf PDF p.32](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=32)

## 5. 全部保护阈值、延迟、锁存与恢复

### 5.1 编码表

|功能|寄存器/编码|单位、范围与使能条件|来源|
|---|---|---|---|
|COV|COVT=(R70<<4)\|(R71>>4)，COVD=R71&15|T=0 禁用，否则阈值=T×1mV+500mV，即 501～4595mV；正常模式且 VAE=1|[DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23) [DVC1124-2数据手册_V1.1.pdf PDF p.17](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=17)|
|CUV|CUVT=(R72<<4)\|(R73>>4)，CUVD=R73&15|T=0 禁用，否则 T×1mV，即 1～4095mV；正常模式且 VAE=1|[DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24) [DVC1124-2数据手册_V1.1.pdf PDF p.17](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=17)|
|COV/CUV 延迟|D=0…7→(200+100D)ms；D=8…15→(D-7)s|200ms～8s，非单一线性步进|[DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23) [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)|
|OCD1 / OCC1|T 在 0x59 / 0x5A；D 在 0x5B / 0x5C|T=0 禁用，否则 T×0.25mV，0.25～63.75mV；延迟=(D+1)×8ms，8～2048ms；正常且 CADC 开启|[DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18) [DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19) [DVC1124-2数据手册_V1.1.pdf PDF p.17](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=17) [DVC1124-2数据手册_V1.1.pdf PDF p.18](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=18)|
|OCD2 / OCC2|0x5E / 0x5F：B6=使能，B5:0=T；D 在 0x60 / 0x61|阈值=(T+1)×4mV，即 4～256mV；延迟=(D+1)×4ms，即 4～1024ms；正常或睡眠|[DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19) [DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20) [DVC1124-2数据手册_V1.1.pdf PDF p.18](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=18)|
|SCD|0x62 B6=SCDE、B5:0=T；D=0x63|RM 阈值=T×10mV，DS 规定有效范围 10～630mV；T=0 意义存在边界矛盾，不能当作禁用，禁用必须 SCDE=0；延迟 D×7.81μs，表列 0～1992μs|[DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20) [DVC1124-2数据手册_V1.1.pdf PDF p.9](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=9) [DVC1124-2数据手册_V1.1.pdf PDF p.18](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=18)|
|电流唤醒|CWT=0x65；CAES=0x55 B2|0 禁用，否则 CWT×10μV；正/负电流唤醒|[DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18) [DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)|
|体二极管续流|BDPT=0x66|0 禁用，否则 BDPT×40μV；须同时审查 DBDM/CBDM、DSGC/CHGC|[DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16) [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17) [DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)|
|内核过温|COTT=0x76 B6:0|0 禁用；其他阈值=(1466+2×COTT)×0.24467-271.03°C；COTF 读清|[DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)|
|I2C 看门狗|IWT=0x77 B2:0|0XX 禁用；100/101/110/111 为 4/8/16/32s；见 IWT 文档差异|[DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26) [DVC1124-2数据手册_V1.1.pdf PDF p.19](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=19)|

【工程建议】电流保护门限是分流电压，不是安培。用项目 Rshunt 算 Ithreshold=Vthreshold/Rshunt，并校核公差、热漂、RC 延迟和 FET SOA。二级/短路比较器门限可超过 CADC ±150mV 量程，不代表 ADC 在该范围仍可准确测量，也不自动扩大推荐 SRP/SRN 引脚输入范围。SCD 逻辑延迟不包含输入 RC 与外部 FET 负载延迟。[DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6) [DVC1124-2数据手册_V1.1.pdf PDF p.7](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=7) [DVC1124-2数据手册_V1.1.pdf PDF p.9](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=9) [DVC1124-2数据手册_V1.1.pdf PDF p.10](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=10)

### 5.2 比较、锁存及恢复

【官方事实】COV 基于单体 VADC 值大于门限启动对应定时器；等待期间任意读值小于门限会复位；CUV 方向相反。每路独立，共 24 路，CM5～CM24 可屏蔽，前 4 路不能屏蔽。达到延迟置对应警报并发中断（受 INT mask）。相等边界在文字中未明确，不能推定 ≥ 或 ≤。[DVC1124-2数据手册_V1.1.pdf PDF p.17](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=17)

【官方事实】OCD1/OCD2/SCD 按 SRP-SRN 的放电方向，OCC1/OCC2 按 SRN-SRP 的充电方向；超过门限持续至延迟触发，回到门限之下会复位/停止定时器。一级使用 CADC，二级/短路用硬件比较器。[DVC1124-2数据手册_V1.1.pdf PDF p.17](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=17) [DVC1124-2数据手册_V1.1.pdf PDF p.18](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=18)

|锁存警报|明确解除条件|不能误认为|
|---|---|---|
|COV/CUV|对应位写 0；VAE=0；进入睡眠|单体回到安全范围就会自动清标志 [DVC1124-2数据手册_V1.1.pdf PDF p.17](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=17)|
|OCD1/OCC1|对应位写 0；CADC 关闭；进入睡眠|电流下降就自动恢复所有输出 [DVC1124-2数据手册_V1.1.pdf PDF p.17](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=17) [DVC1124-2数据手册_V1.1.pdf PDF p.18](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=18)|
|OCD2/OCC2|对应位写 0；相应 OCD2E/OCC2E=0|进入睡眠会清除二级保护 [DVC1124-2数据手册_V1.1.pdf PDF p.18](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=18)|
|SCD|SCD 写 0；SCDE=0|延迟到期后会自动重合闸 [DVC1124-2数据手册_V1.1.pdf PDF p.19](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=19)|
|IWTS / IWTF|有效 I2C 读写解除 IWTS；IWTF 只主动清零|状态 IWTS 和历史 IWTF 是同一个标志 [DVC1124-2数据手册_V1.1.pdf PDF p.19](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=19)|

【工程建议】不要通过关闭 ADC/进入睡眠来充当正常故障恢复流程，因为会同时失去保护。恢复阈值、滞回、连续稳定时间、重试次数和负载去除判定并无完整内建编码，须由 MCU 项目策略定义。先记录原因、确认电池/负载安全、再选择性清锁存并重新授权驱动；若故障仍在可能再次触发。[DVC1124-2数据手册_V1.1.pdf PDF p.17](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=17) [DVC1124-2数据手册_V1.1.pdf PDF p.18](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=18) [DVC1124-2数据手册_V1.1.pdf PDF p.19](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=19)

【推导/风险】COTT 最小非零编码对应约 88.14556°C，已高于数据手册额定结温上限 85°C。不能把内核自动过温关机功能作为“保证芯片始终不超过 85°C”的唯一措施，应另设主机温控余量。[DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26) [DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5)

## 6. FET、预充/预放、电荷泵及硬线联锁

### 6.1 驱动电气行为

【官方事实】CPVS=0 关泵；1…7 对应升压 6…12V。升压既用于 CHG/DSG 也用于采样 MUX。DSG 默认电荷泵模式（DSGM=0）拉至 VCP，关闭时使放电 FET VGS 回到 0；DSGM=1 源随模式拉至 VTOP，可在睡眠使用并关闭泵节电，但该状态的外部导通/压降不能当作正常低阻完全增强。CHG 开启拉至 VCP，关闭拉至 VTOP。[DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22) [DVC1124-2数据手册_V1.1.pdf PDF p.21](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=21)

【官方事实】PDSG/PCHG 关闭时高阻，外部 270kΩ 把 PFET VGS 拉回 0；文字说明开启下拉约 32μA，约形成 8.6V 栅源压差，但电气表给典型 36μA，设计须覆盖 23.2～54.4μA 及器件公差。高边 LOAD 上拉约 150μA，目标为 VTOP/PACK 较高者；LDPU 开始后 60s 自动关闭。PACK 高于 VTOP 约 2V 认为有充电器。[DVC1124-2数据手册_V1.1.pdf PDF p.8](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=8) [DVC1124-2数据手册_V1.1.pdf PDF p.21](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=21) [DVC1124-2数据手册_V1.1.pdf PDF p.22](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=22) [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)

### 6.2 控制与屏蔽

【官方事实】0x51 的 DSGC/CHGC：00/01=关闭，10=通常关闭但允许超过体二极管续流阈值时开启，11=开启请求；并非 11 能越过所有故障联锁。PDSGC/PCHGC 是预驱动请求。HSFM=1 只屏蔽高边输出；GP6/5/2/3 可配置为低边 DSG/CHG/PDSG/PCHG 输出。[DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15) [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16) [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18) [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25) [DVC1124-2参考手册_V1.2.pdf PDF p.30](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=30) [DVC1124-2参考手册_V1.2.pdf PDF p.31](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=31)

- 放电主驱动同时涉及 IWTS/DWM、DON/DDM、SCD、DSGC、CUV/OCD1/OCD2，以及 PDSGF/DPDM 和 DBDM/BDPT 续流分支。SCD 在 RM 图中为独立关闭条件，续流不能视作屏蔽所有安全故障。[DVC1124-2参考手册_V1.2.pdf PDF p.30](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=30)
- 充电主驱动同时涉及 OCD1/CO1M、OCD2/CO2M、SCD/CSM、IWTS/CWM、DON/CDM、CON/CCM、CHGC、COV/OCC1/OCC2，以及 PCHGF/CPCM、CBDM/BDPT 续流分支。[DVC1124-2参考手册_V1.2.pdf PDF p.31](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=31)
- 预放只明确画出 PDSGC、IWTS/PDWM、DON/PDDM，再经 HSFM 控制高边；预充只明确画出 PCHGC、IWTS/PCWM、DON/PCDM、CON/PCCM，再经 HSFM。不能因主 FET 有过流/过欠压门控而自动假定预充/预放通路有同样门控。[DVC1124-2参考手册_V1.2.pdf PDF p.30](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=30) [DVC1124-2参考手册_V1.2.pdf PDF p.31](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=31)
- “屏蔽”位通常 1 表示该来源不再影响输出，0 才执行关闭/联锁；复位 DWM/CWM/PDWM/PCWM 为 1，不能假定 I2C 超时默认关断所有驱动。0x53 复位 0x59，0x54 复位 0xF9，0x52 复位 0x90。[DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16) [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- GP1M=3 时 GP1 是 CON（低有效关闭）；GP4M=3 时 GP4 是 DON；高电平只是“不影响驱动”，不是强制开启。GP1/4 关闭、热敏、模拟、硬线模式的编码是 0/1/2/3；GP2/3/5/6 的 0/1/2/6/7 分别为高阻、热敏、模拟、INT、相应低边驱动，3～5=N/A。[DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24) [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)

【未决】RM p30 图中的放电体二极管分支把 CC1>=BDPT 画成条件，然而 p7 定义充电为负，p17 文字规定充电电流触发放电 FET 续流；不能按图直接生成已认证逻辑。RM p30～31 的 DSGF/CHGF/PDSGF/PCHGF 节点在 HSFM 门控之前，所以状态标志可能反映内部逻辑而不是高边引脚的真实电平，必须实测。[DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7) [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17) [DVC1124-2参考手册_V1.2.pdf PDF p.30](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=30) [DVC1124-2参考手册_V1.2.pdf PDF p.31](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=31)

### 6.3 安全驱动顺序（工程建议，不是厂商给出的固定命令序列）

1. 上电先由独立硬件/系统措施保持负载安全，确认模式和实物版本；不要依赖默认阈值。把期望输出显式关断，核对高边/低边与 GP 功能路由。[DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15) [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18) [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
2. 配置电池映射、采样/分流标度、保护阈值/延迟、掩码、I2C 看门狗策略及 NTC；逐字段保留未知位、CRC 校验并读回。配置值、试验限流和恢复策略均由项目提供。[DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18) [DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19) [DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20) [DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21) [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23) [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24) [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
3. 确认电荷泵、电压/CADC 使能和新鲜测量；91ms 是特定条件典型启动时间，不是可照抄的全条件最大等待。先用仪器确认栅极与电荷泵稳定，再确定项目等待上限。[DVC1124-2数据手册_V1.1.pdf PDF p.8](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=8)
4. 若需预充/预放，启用对应请求并监督 LOAD/PACK、电流、时间和外部电阻温升；达到项目条件后再转主 FET。DPDM/CPCM 默认使预输出开启时阻断主输出，切换次序必须与是否允许重叠的实际电路一致。[DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16) [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17) [DVC1124-2参考手册_V1.2.pdf PDF p.30](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=30) [DVC1124-2参考手册_V1.2.pdf PDF p.31](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=31)
5. 任何故障先撤销危险输出请求、停止相关预驱动，留存警报和状态，再做受控恢复；读取输出标志并不替代 VGS/接触器/功率回路验证。不要把清警报命令与开驱动请求合成不可审查的大块写入。[DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6) [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7) [DVC1124-2参考手册_V1.2.pdf PDF p.30](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=30) [DVC1124-2参考手册_V1.2.pdf PDF p.31](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=31)

## 7. 均衡、断线检测、INT 与输入屏蔽

【官方事实】24 路被动均衡只有在正常模式且 VADC 处于低功耗阶段才开启；因此与同步采样工作窗口耦合。CB24…CB1 位于 0x67～0x69。内部 MOS 每路不超过 25mA，必须实时监测芯片内核温度；外接 NPN 可把主均衡热耗转到片外，仍需单独校核外部元件。[DVC1124-2数据手册_V1.1.pdf PDF p.20](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=20) [DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)

【官方事实】硬件对有效电池重新按序分奇偶组，交替均衡，不是简单看引脚编号的奇偶。被屏蔽单体的均衡驱动禁用。窗口 tCB=N×256ms-tVADC，N=1/2/4/8。任意均衡控制位置 1 启动 60s 定时器；后续再次置 1 重装 60s；超时清所有 CB。[DVC1124-2数据手册_V1.1.pdf PDF p.20](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=20) [DVC1124-2数据手册_V1.1.pdf PDF p.21](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=21)

【官方事实】13 串例子的有效采样通道为 C1,C2,C3,C4,C6,C9,C10,C14,C15,C16,C21,C22,C23；它们映射物理电池 1…13 并奇偶交替，C5,C7,C8,C11,C12,C13,C17,C18,C19,C20,C24 被屏蔽。这是原例子，不是所有 13 串板的固定接法。[DVC1124-2数据手册_V1.1.pdf PDF p.20](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=20)

【官方事实】断线检测 COW=1 同时开启 C1～C24 的 100μA 下拉电流源，1s 后自动关闭。MCU 根据受激励电压判断断线。资料没有给完整诊断阈值、滤波稳定时间和连续故障判据。[DVC1124-2数据手册_V1.1.pdf PDF p.15](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=15) [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)

【工程建议】断线测试必须考虑 RC、短接通道、物理串映射和测试对电压保护/测量值的扰动。不要将一次受激励后的离群数值立即当作真实过充/欠压而盲目解除保护。断线期间的驱动/均衡政策由项目定义并验证。[DVC1124-2数据手册_V1.1.pdf PDF p.15](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=15) [DVC1124-2数据手册_V1.1.pdf PDF p.17](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=17)

【官方事实】CM5～CM24 的 1 禁用该单体电压保护并默认跳过其测量，CMM=1 可使这些屏蔽通道仍测量；PKM、LDM、CTM、V1P8M 分别屏蔽 PACK、LOAD、芯片温度和参考电压测量。前 4 个单体没有 CM 位。[DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21) [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)

【官方事实】可在 GP2/GP3/GP5/GP6 输出 INT，低脉冲宽 1ms。0x79 B7…B0 依次 IWM、IVOM、ICCM、ICOM、ICUM、IOC1M、IOC2M、ISCDM；0 允许、1 屏蔽；复位全 0，但 GP 需先设置为 INT 模式才有输出路由。事件分别为唤醒、VADC 完成、CC2 完成、COV、CUV、一级过流、二级过流、短路；CC1 完成不在该中断表中。[DVC1124-2数据手册_V1.1.pdf PDF p.24](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=24) [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25) [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27) [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)

## 8. 原理图、未用引脚、布局、封装与采购

### 8.1 电池线与典型应用

【官方事实】少于 24 串时未用采样脚不能悬空。C0～C4 必须分别接最低 4 串的节点，不允许短接；其余可依串联顺序短接。图 9(a) 在母排 RBUS 两端分别采样再利用屏蔽通道，降低串联电阻对充放电测量影响；图 9(b) 为简化接法。具体线序由原图和板级物理串映射确认，不能从芯片通道号机械推断电池号。[DVC1124-2数据手册_V1.1.pdf PDF p.15](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=15)

【官方事实】典型应用图包含：每路单体 RC、VTOP 滤波与二极管、VTOP-VCP 电荷泵电容、高边背靠背 CHG/DSG、预充/预放支路、VBASE 驱动的外部 NPN 预稳压器和 VREG 去耦、V1P8/V3P3 去耦、I2C 上拉、GP2 中断、NTC 及差分分流采样滤波。该图未给所有器件数值/型号，不能据此直接生成可投产 BOM。[DVC1124-2数据手册_V1.1.pdf PDF p.27](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=27)

### 8.2 未使用引脚处理

|引脚|原手册处理|来源|
|---|---|---|
|C5～C24|按采样接线规则短接到相邻脚，不能悬空|[DVC1124-2数据手册_V1.1.pdf PDF p.25](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=25) [DVC1124-2数据手册_V1.1.pdf PDF p.15](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=15)|
|SRP/SRN|普通未用时短接 VSS；级联地址模式是另一个明确例外|[DVC1124-2数据手册_V1.1.pdf PDF p.25](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=25) [DVC1124-2数据手册_V1.1.pdf PDF p.23](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=23)|
|GP1～GP6|未用悬空|[DVC1124-2数据手册_V1.1.pdf PDF p.25](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=25)|
|V3P3|未用可悬空或 1μF 到 VSS，并将 V3P3EW/V3P3ES=0|[DVC1124-2数据手册_V1.1.pdf PDF p.25](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=25)|
|DSG/CHG|未用或低边应用悬空|[DVC1124-2数据手册_V1.1.pdf PDF p.25](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=25)|
|PDSG/PCHG|未用或低边应用短接 VSS|[DVC1124-2数据手册_V1.1.pdf PDF p.25](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=25)|
|LOAD|高边 DSG 未用时，经 10kΩ 到 PACK+ 或接 VSS；低边应用接 VSS|[DVC1124-2数据手册_V1.1.pdf PDF p.25](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=25)|
|PACK|高边 CHG 未用时，同口经 10kΩ 到 PACK+ 或接 VSS；分口经 10kΩ 到 CPACK+ 或接 VSS；低边接 VSS|[DVC1124-2数据手册_V1.1.pdf PDF p.25](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=25)|
|VCP|CHG 与 DSG 均未用，或低边应用：近引脚 100nF 到 VTOP，CPVS=0x01；不能直接把电荷泵删掉|[DVC1124-2数据手册_V1.1.pdf PDF p.25](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=25)|

### 8.3 版图图像中的指导文字

【官方事实，已直接检查 PDF 图 15】VTOP/VCP 电容尽可能靠近并就近布局；VREG 是内部 5V 模拟电源，滤波到 VSS 回路优先做到最短，尽量顶层连接，不得已时用多个过孔到主地。主地平面优先在芯片底层整面铺设，双层难实现时优先考虑多层板并设独立地层。V1P8 是内部 1.8V 数字电源，其滤波电容布局/走线优先级高于不在片内使用的 V3P3 外部输出。地节点多过孔接主地降低阻抗。SRP/SRN 滤波电阻电容靠近芯片，差分走线，周围包地。[DVC1124-2数据手册_V1.1.pdf PDF p.26](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=26)

【官方事实】因相邻引脚间隙和爬电距离，PCB 组装可能需敷形涂层，手册提及 IPC-2221B 或 IEC/UL 60950-1。【工程建议】这只是源文件的设计提示，不是本板通过当前法规/安规认证的结论；具体耐压、污染等级、涂覆工艺、外壳和法规应另行评估。[DVC1124-2数据手册_V1.1.pdf PDF p.25](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=25)

### 8.4 封装、订购与历史

【官方事实】LQFP48，芯体 7.00±0.10mm、含脚 9.00±0.20mm、引脚节距 0.50mm、引脚宽 0.15～0.27mm、两端参考跨距 5.50 BSC；最大高度 1.60mm，图示本体高度 1.30～1.50mm，离板 0.01～0.21mm、脚端长度 0.43～0.75mm、角度 0～10°。方向以顶视图 pin1 圆点及编号为准；尺寸全为 mm，正式 PCB footprint 仍需按原封装图审核。[DVC1124-2数据手册_V1.1.pdf PDF p.3](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=3) [DVC1124-2数据手册_V1.1.pdf PDF p.28](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=28)

【官方事实】-22/-24 都是 48-Lead Plastic LQFP，MSL=3，Reel 每盘 1500pcs，最小订货 1500pcs。[DVC1124-2数据手册_V1.1.pdf PDF p.29](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=29)

【官方事实】DS V1.0（2023/4/2）修改驱动绝对/工作电压、保护使能和解除条件、预驱动、地址及应用条目；V1.1（2024/7/24）修改硬线地址及最低串数。RM V1.0（2023/4/2）更新 CST、一级过流/电压阈值、V3P3ES、0x90、驱动图及 p2；V1.1（2025/5/7）把 CPVS 默认从 101 改 000、VAE 从 1 改 0；V1.2（2025/09/26）又恢复 CPVS=101、VAE=1。本包按提供的 V1.2 表记录默认值，不将历史中间值误套现版本；硬件批次是否对应需由采购/厂商确认。[DVC1124-2数据手册_V1.1.pdf PDF p.29](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=29) [DVC1124-2参考手册_V1.2.pdf PDF p.33](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=33) [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)

## 9. 安全驱动检查单与明确未决项

### 9.1 项目必须补齐（项目假设，源文件没有替你决定）

- 实际串数、每路芯片通道到物理单体映射、化学体系、工作/恢复电压、分流电阻与误差预算、最大充放电流、NTC 型号及曲线
- 高边/低边、同口/分口电路；预充/预放电阻、FET SOA、VGS/栅极钳位、峰值电流、外部硬线关断及电荷泵稳定判定
- CRC 实测向量、多字节一致性验证、唤醒波形与重试策略、失败时输出策略、恢复滞回/延时/次数、MCU 掉电与 I2C 看门狗/LDO 重启影响
- 不把工程建议或公式推导写成“厂商保证”；温度、保护时延、地址争议和批次复位值未确认前禁止量产安全承诺

### 9.2 必须保留的文档差异

|ID|矛盾/缺口|处理方式与来源|
|---|---|---|
|D1|首页应用总压≤100V；工作/量程120V；绝对132V|分层保留；同时检查 VCP 对地余量，绝对耐压不是系统额定 [DVC1124-2数据手册_V1.1.pdf PDF p.1](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=1) [DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5) [DVC1124-2数据手册_V1.1.pdf PDF p.6](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=6)|
|D2|0x90 在地图内，但初始 RA 合法上限写0x8F|避免直接/回卷依赖，厂商或台架验证；0x90未命名，不写 [DVC1124-2数据手册_V1.1.pdf PDF p.23](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=23) [DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)|
|D3|VTP 文字称 VTOP，但 DS ADC 路由称 C24-VSS|不得在 VTOP 与 C24 有压降时假定等同；需确认采样实际节点 [DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8) [DVC1124-2数据手册_V1.1.pdf PDF p.14](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=14)|
|D4|SCDT=0 公式0mV，DS门限从10mV起，复位又SCDE=1/T=0|不以T=0禁用；选有效非零阈值并做短路联锁试验 [DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20) [DVC1124-2数据手册_V1.1.pdf PDF p.9](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=9) [DVC1124-2数据手册_V1.1.pdf PDF p.18](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=18)|
|D5|DS称IWT非零启用；RM明确0XX关闭|驱动按显式100～111编码使用；001～011不使用，不静默统一 [DVC1124-2数据手册_V1.1.pdf PDF p.19](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=19) [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)|
|D6|RM p32公式与速查表不一致|同时保留，给出数值反例，要求校准策略确认 [DVC1124-2参考手册_V1.2.pdf PDF p.32](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=32)|
|D7|放电续流图使用CC1>=BDPT，而充电CC1应为负|不自动反转/取绝对值生成“官方”逻辑；厂商确认或台架验证 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7) [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17) [DVC1124-2参考手册_V1.2.pdf PDF p.30](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=30)|
|D8|FET flags位于HSFM之前，而文字称输出开启标志|仅作逻辑状态，实际高边电平需验证 [DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8) [DVC1124-2参考手册_V1.2.pdf PDF p.30](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=30) [DVC1124-2参考手册_V1.2.pdf PDF p.31](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=31)|
|D9|DS称CADC使能CAE；RM命名CAEW/CAES|按地址/正常与睡眠用途区分，CAE不是另一可写字段 [DVC1124-2数据手册_V1.1.pdf PDF p.17](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=17) [DVC1124-2数据手册_V1.1.pdf PDF p.18](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=18) [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)|
|D10|OCCD/OCC1D、FRT5]、OCD1T[5存在印刷别名/括号错误|保留source_name，规范字段仅作可追溯格式归一 [DVC1124-2参考手册_V1.2.pdf PDF p.4](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=4) [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18) [DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19) [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)|
|D11|预驱动正文约32μA，电气表典型36μA；开关时间列在最小列|保留两处条件和列位置，禁止充当最坏关断上限 [DVC1124-2数据手册_V1.1.pdf PDF p.8](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=8) [DVC1124-2数据手册_V1.1.pdf PDF p.21](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=21) [DVC1124-2数据手册_V1.1.pdf PDF p.22](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=22)|
|D12|COTT最小非零阈值超过额定结温85°C|MCU温控须有更低阈值/余量；不可靠此保证不超额定 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26) [DVC1124-2数据手册_V1.1.pdf PDF p.5](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=5)|
|D13|未明确多字节快照、CRC全部参数/向量、I2C唤醒波形、所有命令最大延时|列为驱动验证项；不编造原厂时序 [DVC1124-2数据手册_V1.1.pdf PDF p.12](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=12) [DVC1124-2数据手册_V1.1.pdf PDF p.23](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=23) [DVC1124-2数据手册_V1.1.pdf PDF p.24](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=24)|
|D14|断线诊断阈值/恢复策略、NTC温度系数、用户NVM保存未给出|由工程项目明确；未知值保持null/未说明 [DVC1124-2数据手册_V1.1.pdf PDF p.15](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=15) [DVC1124-2数据手册_V1.1.pdf PDF p.16](../../sources/DVC1124-2%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C_V1.1.pdf#page=16) [DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)|

### 9.3 最小可审查软件测试

【工程建议】分别测试：正常 7-bit 地址与级联地址；首/后字节 CRC 覆盖；错误 CRC/NACK 后部分写入；读清标志的单所有者；写零清故障；CC1 正负和 CC2 20-bit 符号扩展；0x06 状态低半字节隔离；CVS 切换后标度同步；COV/CUV 非线性延迟边界；SCD/OCD2 默认复位后仍保持负载禁止；所有保留位写前后保持；睡眠缺失保护与唤醒原因；CST复位重新配置；均衡映射/奇偶/60s超时；COW自清与误报；预驱动独立关闭；HSFM高低边区别；主机掉线时IWT、V3P3与外部MCU实际行为。验收应包含限流电源、模拟电池及真实 RC/FET 负载条件，而非只通过寄存器回读。


## 10. 完整逐字节寄存器图谱
所有地址均来自参考手册 V1.2 详细寄存器表。R/W0C=写零清除；RC=读清；W1触发=写一启动且自清。空白字段以 RES 表示，不能视为零。复位 ? 表示未给出，partial 表示只给出部分位。精确逐位 MODE/DEFAULT、保留位及出处见 registers.json。
|地址|B7 → B0|访问类型|复位|来源|
|---|---|---|---|---|
|0x00|IWTF / COV / CUV / OCD1 / OCC1 / OCD2 / OCC2 / SCD|R/W0C|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)|
|0x01|PD / VADF / CC1F / CC2F / CST[3] / CST[2] / CST[1] / CST[0]|R,RC,R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)|
|0x02|CC1[15] / CC1[14] / CC1[13] / CC1[12] / CC1[11] / CC1[10] / CC1[9] / CC1[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)|
|0x03|CC1[7] / CC1[6] / CC1[5] / CC1[4] / CC1[3] / CC1[2] / CC1[1] / CC1[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)|
|0x04|CC2[19] / CC2[18] / CC2[17] / CC2[16] / CC2[15] / CC2[14] / CC2[13] / CC2[12]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)|
|0x05|CC2[11] / CC2[10] / CC2[9] / CC2[8] / CC2[7] / CC2[6] / CC2[5] / CC2[4]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)|
|0x06|CC2[3] / CC2[2] / CC2[1] / CC2[0] / PDSGF / PCHGF / DSGF / CHGF|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)|
|0x07|VTP[15] / VTP[14] / VTP[13] / VTP[12] / VTP[11] / VTP[10] / VTP[9] / VTP[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)|
|0x08|VTP[7] / VTP[6] / VTP[5] / VTP[4] / VTP[3] / VTP[2] / VTP[1] / VTP[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)|
|0x09|VPK[15] / VPK[14] / VPK[13] / VPK[12] / VPK[11] / VPK[10] / VPK[9] / VPK[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)|
|0x0A|VPK[7] / VPK[6] / VPK[5] / VPK[4] / VPK[3] / VPK[2] / VPK[1] / VPK[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)|
|0x0B|VLD[15] / VLD[14] / VLD[13] / VLD[12] / VLD[11] / VLD[10] / VLD[9] / VLD[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)|
|0x0C|VLD[7] / VLD[6] / VLD[5] / VLD[4] / VLD[3] / VLD[2] / VLD[1] / VLD[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)|
|0x0D|VCT[15] / VCT[14] / VCT[13] / VCT[12] / VCT[11] / VCT[10] / VCT[9] / VCT[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)|
|0x0E|VCT[7] / VCT[6] / VCT[5] / VCT[4] / VCT[3] / VCT[2] / VCT[1] / VCT[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)|
|0x0F|V1P8[15] / V1P8[14] / V1P8[13] / V1P8[12] / V1P8[11] / V1P8[10] / V1P8[9] / V1P8[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9)|
|0x10|V1P8[7] / V1P8[6] / V1P8[5] / V1P8[4] / V1P8[3] / V1P8[2] / V1P8[1] / V1P8[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9)|
|0x11|VGP1[15] / VGP1[14] / VGP1[13] / VGP1[12] / VGP1[11] / VGP1[10] / VGP1[9] / VGP1[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9)|
|0x12|VGP1[7] / VGP1[6] / VGP1[5] / VGP1[4] / VGP1[3] / VGP1[2] / VGP1[1] / VGP1[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9)|
|0x13|VGP2[15] / VGP2[14] / VGP2[13] / VGP2[12] / VGP2[11] / VGP2[10] / VGP2[9] / VGP2[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9)|
|0x14|VGP2[7] / VGP2[6] / VGP2[5] / VGP2[4] / VGP2[3] / VGP2[2] / VGP2[1] / VGP2[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9)|
|0x15|VGP3[15] / VGP3[14] / VGP3[13] / VGP3[12] / VGP3[11] / VGP3[10] / VGP3[9] / VGP3[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9)|
|0x16|VGP3[7] / VGP3[6] / VGP3[5] / VGP3[4] / VGP3[3] / VGP3[2] / VGP3[1] / VGP3[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9)|
|0x17|VGP4[15] / VGP4[14] / VGP4[13] / VGP4[12] / VGP4[11] / VGP4[10] / VGP4[9] / VGP4[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9)|
|0x18|VGP4[7] / VGP4[6] / VGP4[5] / VGP4[4] / VGP4[3] / VGP4[2] / VGP4[1] / VGP4[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9)|
|0x19|VGP5[15] / VGP5[14] / VGP5[13] / VGP5[12] / VGP5[11] / VGP5[10] / VGP5[9] / VGP5[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10)|
|0x1A|VGP5[7] / VGP5[6] / VGP5[5] / VGP5[4] / VGP5[3] / VGP5[2] / VGP5[1] / VGP5[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10)|
|0x1B|VGP6[15] / VGP6[14] / VGP6[13] / VGP6[12] / VGP6[11] / VGP6[10] / VGP6[9] / VGP6[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10)|
|0x1C|VGP6[7] / VGP6[6] / VGP6[5] / VGP6[4] / VGP6[3] / VGP6[2] / VGP6[1] / VGP6[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10)|
|0x1D|VC1[15] / VC1[14] / VC1[13] / VC1[12] / VC1[11] / VC1[10] / VC1[9] / VC1[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10)|
|0x1E|VC1[7] / VC1[6] / VC1[5] / VC1[4] / VC1[3] / VC1[2] / VC1[1] / VC1[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10)|
|0x1F|VC2[15] / VC2[14] / VC2[13] / VC2[12] / VC2[11] / VC2[10] / VC2[9] / VC2[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10)|
|0x20|VC2[7] / VC2[6] / VC2[5] / VC2[4] / VC2[3] / VC2[2] / VC2[1] / VC2[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10)|
|0x21|VC3[15] / VC3[14] / VC3[13] / VC3[12] / VC3[11] / VC3[10] / VC3[9] / VC3[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10)|
|0x22|VC3[7] / VC3[6] / VC3[5] / VC3[4] / VC3[3] / VC3[2] / VC3[1] / VC3[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10)|
|0x23|VC4[15] / VC4[14] / VC4[13] / VC4[12] / VC4[11] / VC4[10] / VC4[9] / VC4[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.11](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=11)|
|0x24|VC4[7] / VC4[6] / VC4[5] / VC4[4] / VC4[3] / VC4[2] / VC4[1] / VC4[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.11](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=11)|
|0x25|VC5[15] / VC5[14] / VC5[13] / VC5[12] / VC5[11] / VC5[10] / VC5[9] / VC5[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.11](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=11)|
|0x26|VC5[7] / VC5[6] / VC5[5] / VC5[4] / VC5[3] / VC5[2] / VC5[1] / VC5[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.11](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=11)|
|0x27|VC6[15] / VC6[14] / VC6[13] / VC6[12] / VC6[11] / VC6[10] / VC6[9] / VC6[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.11](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=11)|
|0x28|VC6[7] / VC6[6] / VC6[5] / VC6[4] / VC6[3] / VC6[2] / VC6[1] / VC6[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.11](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=11)|
|0x29|VC7[15] / VC7[14] / VC7[13] / VC7[12] / VC7[11] / VC7[10] / VC7[9] / VC7[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.11](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=11)|
|0x2A|VC7[7] / VC7[6] / VC7[5] / VC7[4] / VC7[3] / VC7[2] / VC7[1] / VC7[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.11](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=11)|
|0x2B|VC8[15] / VC8[14] / VC8[13] / VC8[12] / VC8[11] / VC8[10] / VC8[9] / VC8[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.11](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=11)|
|0x2C|VC8[7] / VC8[6] / VC8[5] / VC8[4] / VC8[3] / VC8[2] / VC8[1] / VC8[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.11](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=11)|
|0x2D|VC9[15] / VC9[14] / VC9[13] / VC9[12] / VC9[11] / VC9[10] / VC9[9] / VC9[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.12](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=12)|
|0x2E|VC9[7] / VC9[6] / VC9[5] / VC9[4] / VC9[3] / VC9[2] / VC9[1] / VC9[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.12](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=12)|
|0x2F|VC10[15] / VC10[14] / VC10[13] / VC10[12] / VC10[11] / VC10[10] / VC10[9] / VC10[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.12](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=12)|
|0x30|VC10[7] / VC10[6] / VC10[5] / VC10[4] / VC10[3] / VC10[2] / VC10[1] / VC10[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.12](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=12)|
|0x31|VC11[15] / VC11[14] / VC11[13] / VC11[12] / VC11[11] / VC11[10] / VC11[9] / VC11[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.12](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=12)|
|0x32|VC11[7] / VC11[6] / VC11[5] / VC11[4] / VC11[3] / VC11[2] / VC11[1] / VC11[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.12](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=12)|
|0x33|VC12[15] / VC12[14] / VC12[13] / VC12[12] / VC12[11] / VC12[10] / VC12[9] / VC12[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.12](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=12)|
|0x34|VC12[7] / VC12[6] / VC12[5] / VC12[4] / VC12[3] / VC12[2] / VC12[1] / VC12[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.12](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=12)|
|0x35|VC13[15] / VC13[14] / VC13[13] / VC13[12] / VC13[11] / VC13[10] / VC13[9] / VC13[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.12](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=12)|
|0x36|VC13[7] / VC13[6] / VC13[5] / VC13[4] / VC13[3] / VC13[2] / VC13[1] / VC13[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.12](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=12)|
|0x37|VC14[15] / VC14[14] / VC14[13] / VC14[12] / VC14[11] / VC14[10] / VC14[9] / VC14[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.13](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=13)|
|0x38|VC14[7] / VC14[6] / VC14[5] / VC14[4] / VC14[3] / VC14[2] / VC14[1] / VC14[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.13](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=13)|
|0x39|VC15[15] / VC15[14] / VC15[13] / VC15[12] / VC15[11] / VC15[10] / VC15[9] / VC15[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.13](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=13)|
|0x3A|VC15[7] / VC15[6] / VC15[5] / VC15[4] / VC15[3] / VC15[2] / VC15[1] / VC15[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.13](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=13)|
|0x3B|VC16[15] / VC16[14] / VC16[13] / VC16[12] / VC16[11] / VC16[10] / VC16[9] / VC16[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.13](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=13)|
|0x3C|VC16[7] / VC16[6] / VC16[5] / VC16[4] / VC16[3] / VC16[2] / VC16[1] / VC16[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.13](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=13)|
|0x3D|VC17[15] / VC17[14] / VC17[13] / VC17[12] / VC17[11] / VC17[10] / VC17[9] / VC17[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.13](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=13)|
|0x3E|VC17[7] / VC17[6] / VC17[5] / VC17[4] / VC17[3] / VC17[2] / VC17[1] / VC17[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.13](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=13)|
|0x3F|VC18[15] / VC18[14] / VC18[13] / VC18[12] / VC18[11] / VC18[10] / VC18[9] / VC18[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.13](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=13)|
|0x40|VC18[7] / VC18[6] / VC18[5] / VC18[4] / VC18[3] / VC18[2] / VC18[1] / VC18[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.13](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=13)|
|0x41|VC19[15] / VC19[14] / VC19[13] / VC19[12] / VC19[11] / VC19[10] / VC19[9] / VC19[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.14](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=14)|
|0x42|VC19[7] / VC19[6] / VC19[5] / VC19[4] / VC19[3] / VC19[2] / VC19[1] / VC19[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.14](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=14)|
|0x43|VC20[15] / VC20[14] / VC20[13] / VC20[12] / VC20[11] / VC20[10] / VC20[9] / VC20[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.14](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=14)|
|0x44|VC20[7] / VC20[6] / VC20[5] / VC20[4] / VC20[3] / VC20[2] / VC20[1] / VC20[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.14](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=14)|
|0x45|VC21[15] / VC21[14] / VC21[13] / VC21[12] / VC21[11] / VC21[10] / VC21[9] / VC21[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.14](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=14)|
|0x46|VC21[7] / VC21[6] / VC21[5] / VC21[4] / VC21[3] / VC21[2] / VC21[1] / VC21[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.14](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=14)|
|0x47|VC22[15] / VC22[14] / VC22[13] / VC22[12] / VC22[11] / VC22[10] / VC22[9] / VC22[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.14](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=14)|
|0x48|VC22[7] / VC22[6] / VC22[5] / VC22[4] / VC22[3] / VC22[2] / VC22[1] / VC22[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.14](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=14)|
|0x49|VC23[15] / VC23[14] / VC23[13] / VC23[12] / VC23[11] / VC23[10] / VC23[9] / VC23[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.14](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=14)|
|0x4A|VC23[7] / VC23[6] / VC23[5] / VC23[4] / VC23[3] / VC23[2] / VC23[1] / VC23[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.14](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=14)|
|0x4B|VC24[15] / VC24[14] / VC24[13] / VC24[12] / VC24[11] / VC24[10] / VC24[9] / VC24[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)|
|0x4C|VC24[7] / VC24[6] / VC24[5] / VC24[4] / VC24[3] / VC24[2] / VC24[1] / VC24[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)|
|0x4D|VVOS[15] / VVOS[14] / VVOS[13] / VVOS[12] / VVOS[11] / VVOS[10] / VVOS[9] / VVOS[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)|
|0x4E|VVOS[7] / VVOS[6] / VVOS[5] / VVOS[4] / VVOS[3] / VVOS[2] / VVOS[1] / VVOS[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)|
|0x4F|CVOS[15] / CVOS[14] / CVOS[13] / CVOS[12] / CVOS[11] / CVOS[10] / CVOS[9] / CVOS[8]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)|
|0x50|CVOS[7] / CVOS[6] / CVOS[5] / CVOS[4] / CVOS[3] / CVOS[2] / CVOS[1] / CVOS[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)|
|0x51|LDPU / PDSGC / PCHGC / DSGM / DSGC[1] / DSGC[0] / CHGC[1] / CHGC[0]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)|
|0x52|PDWM / RES / RES / DPC[4] / DPC[3] / DPC[2] / DPC[1] / DPC[0]|R/W|0x90|[DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)|
|0x53|PDDM / PCWM / PCCM / PCDM / DWM / DDM / DPDM / DBDM|R/W|0x59|[DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)|
|0x54|CWM / CO1M / CO2M / CSM / CDM / CCM / CPCM / CBDM|R/W|0xF9|[DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)|
|0x55|HSFM / RES / RES / RES / CAEW / CAES / RES / CAMZ|R/W,R/W1-trigger-self-clear|0x2C|[DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)|
|0x56|RES / RES / RES / RES / C1OW[1] / C1OW[0] / C1OS[1] / C1OS[0]|R/W|0xFF|[DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)|
|0x57|RES / RES / RES / RES / RES / RES / RES / RES|R|0x28|[DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)|
|0x58|RES / RES / RES / RES / RES / RES / RES / RES|R|0x05|[DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)|
|0x59|OCD1T[7] / OCD1T[6] / OCD1T[5] / OCD1T[4] / OCD1T[3] / OCD1T[2] / OCD1T[1] / OCD1T[0]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)|
|0x5A|OCC1T[7] / OCC1T[6] / OCC1T[5] / OCC1T[4] / OCC1T[3] / OCC1T[2] / OCC1T[1] / OCC1T[0]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)|
|0x5B|OCD1D[7] / OCD1D[6] / OCD1D[5] / OCD1D[4] / OCD1D[3] / OCD1D[2] / OCD1D[1] / OCD1D[0]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)|
|0x5C|OCC1D[7] / OCC1D[6] / OCC1D[5] / OCC1D[4] / OCC1D[3] / OCC1D[2] / OCC1D[1] / OCC1D[0]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)|
|0x5D|RES / RES / RES / RES / RES / RES / RES / RES|R/W,R|partial 0xB0/0xF0|[DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)|
|0x5E|RES / OCD2E / OCD2T[5] / OCD2T[4] / OCD2T[3] / OCD2T[2] / OCD2T[1] / OCD2T[0]|R,R/W|0x40|[DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)|
|0x5F|RES / OCC2E / OCC2T[5] / OCC2T[4] / OCC2T[3] / OCC2T[2] / OCC2T[1] / OCC2T[0]|R/W|0xC0|[DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)|
|0x60|OCD2D[7] / OCD2D[6] / OCD2D[5] / OCD2D[4] / OCD2D[3] / OCD2D[2] / OCD2D[1] / OCD2D[0]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)|
|0x61|OCC2D[7] / OCC2D[6] / OCC2D[5] / OCC2D[4] / OCC2D[3] / OCC2D[2] / OCC2D[1] / OCC2D[0]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)|
|0x62|RES / SCDE / SCDT[5] / SCDT[4] / SCDT[3] / SCDT[2] / SCDT[1] / SCDT[0]|R,R/W|0x40|[DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)|
|0x63|SCDD[7] / SCDD[6] / SCDD[5] / SCDD[4] / SCDD[3] / SCDD[2] / SCDD[1] / SCDD[0]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)|
|0x64|RES / RES / RES / RES / RES / RES / RES / RES|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)|
|0x65|CWT[7] / CWT[6] / CWT[5] / CWT[4] / CWT[3] / CWT[2] / CWT[1] / CWT[0]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)|
|0x66|BDPT[7] / BDPT[6] / BDPT[5] / BDPT[4] / BDPT[3] / BDPT[2] / BDPT[1] / BDPT[0]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)|
|0x67|CB[24] / CB[23] / CB[22] / CB[21] / CB[20] / CB[19] / CB[18] / CB[17]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)|
|0x68|CB[16] / CB[15] / CB[14] / CB[13] / CB[12] / CB[11] / CB[10] / CB[9]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)|
|0x69|CB[8] / CB[7] / CB[6] / CB[5] / CB[4] / CB[3] / CB[2] / CB[1]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)|
|0x6A|CM[24] / CM[23] / CM[22] / CM[21] / CM[20] / CM[19] / CM[18] / CM[17]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)|
|0x6B|CM[16] / CM[15] / CM[14] / CM[13] / CM[12] / CM[11] / CM[10] / CM[9]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)|
|0x6C|CM[8] / CM[7] / CM[6] / CM[5] / PKM / LDM / CTM / V1P8M|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)|
|0x6D|RES / RES / CPVS[2] / CPVS[1] / CPVS[0] / COW / CMM / CVS|R/W|0x28|[DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)|
|0x6E|VAE / VASM / VAMP[1] / VAMP[0] / RES / RES / VAO[1] / VAO[0]|R/W|0xCD|[DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)|
|0x6F|RES / RES / RES / RES / RES / RES / RES / RES|R|0x28|[DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)|
|0x70|COVT[11] / COVT[10] / COVT[9] / COVT[8] / COVT[7] / COVT[6] / COVT[5] / COVT[4]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)|
|0x71|COVT[3] / COVT[2] / COVT[1] / COVT[0] / COVD[3] / COVD[2] / COVD[1] / COVD[0]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)|
|0x72|CUVT[11] / CUVT[10] / CUVT[9] / CUVT[8] / CUVT[7] / CUVT[6] / CUVT[5] / CUVT[4]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)|
|0x73|CUVT[3] / CUVT[2] / CUVT[1] / CUVT[0] / CUVD[3] / CUVD[2] / CUVD[1] / CUVD[0]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)|
|0x74|GP1M[1] / GP1M[0] / GP2M[2] / GP2M[1] / GP2M[0] / GP3M[2] / GP3M[1] / GP3M[0]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)|
|0x75|GP4M[1] / GP4M[0] / GP5M[2] / GP5M[1] / GP5M[0] / GP6M[2] / GP6M[1] / GP6M[0]|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)|
|0x76|COTF / COTT[6] / COTT[5] / COTT[4] / COTT[3] / COTT[2] / COTT[1] / COTT[0]|RC,R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)|
|0x77|V3P3ES / V3P3EW / V3P3M / RES / IWTS / IWT[2] / IWT[1] / IWT[0]|R/W,R|0xC0|[DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)|
|0x78|TIWK / RES / RES / RES / TWSE[3] / TWSE[2] / TWSE[1] / TWSE[0]|R,R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)|
|0x79|IWM / IVOM / ICCM / ICOM / ICUM / IOC1M / IOC2M / ISCDM|R/W|0x00|[DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)|
|0x7A|RES / RES / RES / RES / RES / RES / RES / RES|R|partial 0x00/0x80|[DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)|
|0x7B|RES / RES / RES / RES / RES / RES / RES / RES|R|partial 0x60/0xE0|[DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)|
|0x7C|RES / RES / RES / RES / RES / RES / RES / RES|R|0x48|[DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)|
|0x7D|RES / RES / RES / RES / RES / RES / RES / RES|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)|
|0x7E|FRT[7] / FRT[6] / FRT[5] / FRT[4] / FRT[3] / FRT[2] / FRT[1] / FRT[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)|
|0x7F|RES / RES / RES / RES / RES / RES / RES / RES|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)|
|0x80|RES / RES / RES / RES / RES / RES / RES / RES|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)|
|0x81|RES / RES / RES / RES / RES / RES / RES / RES|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)|
|0x82|RES / RES / RES / RES / RES / RES / RES / RES|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)|
|0x83|RES / RES / RES / RES / RES / RES / RES / RES|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)|
|0x84|RES / RES / RES / RES / RES / RES / RES / RES|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)|
|0x85|RES / RES / RES / RES / RES / RES / RES / RES|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)|
|0x86|RES / RES / RES / RES / RES / RES / RES / RES|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)|
|0x87|RES / RES / RES / RES / RES / RES / RES / RES|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)|
|0x88|RES / RES / RES / RES / RES / RES / RES / RES|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)|
|0x89|RES / RES / RES / RES / RES / RES / RES / RES|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)|
|0x8A|RES / RES / RES / RES / RES / RES / RES / RES|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)|
|0x8B|RES / RES / RES / RES / RES / RES / RES / RES|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)|
|0x8C|RES / RES / RES / RES / RES / RES / RES / RES|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)|
|0x8D|RES / RES / RES / RES / RES / RES / RES / RES|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)|
|0x8E|RES / RES / RES / RES / RES / RES / RES / RES|R/W,R|partial 0x80/0xC7|[DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)|
|0x8F|CV[7] / CV[6] / CV[5] / CV[4] / CV[3] / CV[2] / CV[1] / CV[0]|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)|
|0x90|RES / RES / RES / RES / RES / RES / RES / RES|R|?|[DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)|

## 11. 完整字段语义与编码字典
以下按参考手册原字段定义组织；保留命名差异，不将 N/A 编码当作可用模式。测量换算、安全约束与冲突处理见前文。

### IWTF
I2C 看门狗溢出标识位，将该 bit 置 0 即可清除，置 1 无效 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 0：未发生 I2C 看门狗溢出 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 1：已发生 I2C 看门狗溢出 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)

### COV
电池过压标识位，将该 bit 置 0 即可清除，置 1 无效 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 0：未发生电池过压 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 1：已发生电池过压 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)

### CUV
电池欠压标识位，将该 bit 置 0 即可清除，置 1 无效 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 0：未发生电池欠压 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 1：已发生电池欠压 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)

### OCD1
1 级放电过流标识位，将该 bit 置 0 即可清除，置 1 无效 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 0：未发生 1 级放电过流 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 1：已发生 1 级放电过流 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)

### OCC1
1 级充电过流标识位，将该 bit 置 0 即可清除，置 1 无效 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 0：未发生 1 级充电过流 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 1：已发生 1 级充电过流 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)

### OCD2
2 级放电过流标识位，将该 bit 置 0 即可清除，置 1 无效 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 0：未发生 2 级放电过流 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 1：已发生 2 级放电过流 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)

### OCC2
2 级充电过流标识位，将该 bit 置 0 即可清除，置 1 无效 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 0：未发生 2 级充电过流 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 1：已发生 2 级充电过流 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)

### SCD
放电短路标识位，将该 bit 置 0 即可清除，置 1 无效 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 0：未发生放电短路 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 1：已发生放电短路 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)

### PD
充电器检测标识位 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 0：未检测到充电器 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 1：已检测到充电器 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)

### VADF
VADC 转换完成标识位，I2C 读取该地址后会自动清零 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 0：VADC 未完成转换 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)
- 1：VADC 已完成转换 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6)

### CC1F
CADC CC1 转换完成标识位，I2C 读取该地址后会自动清零 [DVC1124-2参考手册_V1.2.pdf PDF p.6](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=6) [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 0：CADC CC1 未完成转换 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 1：CADC CC1 已完成转换 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)

### CC2F
CADC CC2 转换完成标识位，I2C 读取该地址后会自动清零 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 0：CADC CC2 未完成转换 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 1：CADC CC2 已完成转换 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)

### CST[3:0]
芯片状态标识位，写入 1101，1110，1111 有效，写入其他值无效 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 0000：芯片从关机状态被唤醒 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 0001：芯片从休眠状态被 I2C 通信唤醒 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 0010：芯片从休眠状态被定时唤醒 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 0011：芯片从休眠状态被放电电流唤醒 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 0100：芯片从休眠状态被充电电流唤醒 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 0101：芯片从休眠状态被 2 级放电过流唤醒 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 0110：芯片从休眠状态被 2 级充电过流唤醒 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 0111：芯片从休眠状态被放电短路唤醒 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 1000：芯片从休眠状态被充电器唤醒 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 1001：N/A [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 1010：N/A [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 1011：芯片正在等待关机 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 1100：芯片正在等待休眠 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 1101：使所有寄存器复位为默认值 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 1110：使芯片进入休眠状态 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 1111：使芯片进入关断状态 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)

### CC1[15:0]
CADC CC1 电流值，有符号二进制补码，正值为放电，负值为充电，LSB=5μV [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)

### CC2[19:0]
CADC CC2 电流值，有符号二进制补码，正值为放电，负值为充电，LSB=5/16μV=0.3125μV [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)

### PDSGF
PDSG 驱动输出标识位 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7) [DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)
- 0：PDSG 驱动输出已关闭 [DVC1124-2参考手册_V1.2.pdf PDF p.7](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=7)
- 1：PDSG 驱动输出已开启 [DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)

### PCHGF
PCHG 驱动输出标识位 [DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)
- 0：PCHG 驱动输出已关闭 [DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)
- 1：PCHG 驱动输出已开启 [DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)

### DSGF
DSG 驱动输出标识位 [DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)
- 0：DSG 驱动输出已关闭 [DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)
- 1：DSG 驱动输出已开启 [DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)

### CHGF
CHG 驱动输出标识位 [DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)
- 0：CHG 驱动输出已关闭 [DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)
- 1：CHG 驱动输出已开启 [DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)

### VTP[15:0]
VTOP 电压值，无符号二进制，LSB=12.8mV [DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)

### VPK[15:0]
PACK 电压值，无符号二进制，LSB=12.8mV [DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)

### VLD[15:0]
LOAD 电压值，无符号二进制，LSB=12.8mV [DVC1124-2参考手册_V1.2.pdf PDF p.8](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=8)

### VCT[15:0]
芯片内核温度值，无符号二进制，芯片内核温度=VCT×0.24467°C-271.03°C [DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9)

### V1P8[15:0]
V1P8 电压值，无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9)

### VGP1[15:0]
GP1 电压值，无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9)

### VGP2[15:0]
GP2 电压值，无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9)

### VGP3[15:0]
GP3 电压值，无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.9](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=9)

### VGP4[15:0]
GP4 电压值，无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10)

### VGP5[15:0]
GP5 电压值，无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10)

### VGP6[15:0]
GP6 电压值，无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10)

### VC1[15:0]
C1 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10)

### VC2[15:0]
C2 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.10](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=10)

### VC3[15:0]
C3 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.11](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=11)

### VC4[15:0]
C4 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.11](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=11)

### VC5[15:0]
C5 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.11](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=11)

### VC6[15:0]
C6 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.11](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=11)

### VC7[15:0]
C7 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.11](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=11)

### VC8[15:0]
C8 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.12](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=12)

### VC9[15:0]
C9 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.12](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=12)

### VC10[15:0]
C10 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.12](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=12)

### VC11[15:0]
C11 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.12](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=12)

### VC12[15:0]
C12 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.12](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=12)

### VC13[15:0]
C13 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.13](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=13)

### VC14[15:0]
C14 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.13](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=13)

### VC15[15:0]
C15 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.13](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=13)

### VC16[15:0]
C16 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.13](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=13)

### VC17[15:0]
C17 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.13](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=13)

### VC18[15:0]
C18 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.14](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=14)

### VC19[15:0]
C19 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.14](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=14)

### VC20[15:0]
C20 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.14](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=14)

### VC21[15:0]
C21 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.14](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=14)

### VC22[15:0]
C22 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.14](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=14)

### VC23[15:0]
C23 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)

### VC24[15:0]
C24 电压值，默认无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)

### VVOS[15:0]
VADC 校准电压值，无符号二进制，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)

### CVOS[15:0]
CADC 校准电压值，有符号二进制补码，LSB=5μV [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)

### LDPU
LOAD 上拉控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)
- 0：关闭 LOAD 上拉 [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)
- 1：开启 LOAD 上拉，该 bit 会在 60s 后自动复位为 0 [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)

### PDSGC
PDSG 驱动输出控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)
- 0：关闭 PDSG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)
- 1：开启 PDSG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)

### PCHGC
PCHG 驱动输出控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)
- 0：关闭 PCHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)
- 1：开启 PCHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)

### DSGM
DSG 驱动输出模式控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15) [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 0：DSG 为电荷泵驱动输出模式 [DVC1124-2参考手册_V1.2.pdf PDF p.15](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=15)
- 1：DSG 为源随驱动输出模式 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)

### DSGC[1:0]
DSG 驱动输出控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 00/01：关闭 DSG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 10：关闭 DSG 驱动输出，但允许在充电电流大于 FET 体二极管续流阈值时开启 DSG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 11：开启 DSG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)

### CHGC[1:0]
CHG 驱动输出控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 00/01：关闭 CHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 10：关闭 CHG 驱动输出，但允许在放电电流大于 FET 体二极管续流阈值时开启 CHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 11：开启 CHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)

### PDWM
I2C 超时关闭 PDSG 驱动输出屏蔽位 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 0：I2C 超时关闭 PDSG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 1：I2C 超时不影响 PDSG 驱动输出状态 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)

### DPC[4:0]
DSG 驱动输出下拉强度控制位， [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 00000：下拉强度为 0 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 00001：下拉强度为 1 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 11101：下拉强度为 29 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 11110：下拉强度为 30 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 11111：N/A [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)

### PDDM
关闭 PDSG 驱动输出屏蔽位 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 0：DON 输入为 0 时关闭 PDSG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 1：DON 输入不影响 PDSG 驱动输出状态 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)

### PCWM
I2C 超时关闭 PCHG 驱动输出屏蔽位 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 0：I2C 超时关闭 PCHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 1：I2C 超时不影响 PCHG 驱动输出状态 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)

### PCCM
CON(CHG_OFF_N)关闭 PCHG 驱动输出屏蔽位 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 0：CON 输入为 0 时关闭 PCHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 1：CON 输入不影响 PCHG 驱动输出状态 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)

### PCDM
DON(DSG_OFF_N)关闭 PCHG 驱动输出屏蔽位 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 0：DON 输入为 0 时关闭 PCHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 1：DON 输入不影响 PCHG 驱动输出状态 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)

### DWM
I2C 超时关闭 DSG 驱动输出屏蔽位 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 0：I2C 超时关闭 DSG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)
- 1：I2C 超时不影响 DSG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.16](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=16)

### DDM
DON(DSG_OFF_N)关闭 DSG 驱动输出屏蔽位 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 0：DON 输入为 0 时关闭 DSG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 1：DON 输入不影响 DSG 驱动输出状态 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)

### DPDM
PDSG 开启时关闭 DSG 驱动输出屏蔽位 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 0：PDSG 开启时关闭 DSG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 1：PDSG 开启时不影响 DSG 驱动输出状态 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)

### DBDM
放电 NFET 体二极管保护屏蔽位 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 0：充电电流大于放电 NFET 体二极管续流阈值时自动开启 DSG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 1：充电电流大于放电 NFET 体二极管续流阈值时不影响 DSG 驱动输出状态 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)

### CWM
I2C 超时关闭 CHG 驱动输出屏蔽位 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 0：I2C 超时关闭 CHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 1：I2C 超时不影响 CHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)

### CO1M
1 级放电过流时关闭 CHG 驱动输出屏蔽位 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 0：1 级放电过流时关闭 CHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 1：1 级放电过流时不影响 CHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)

### CO2M
2 级放电过流时关闭 CHG 驱动输出屏蔽位 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 0：2 级放电过流时关闭 CHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 1：2 级放电过流时不影响 CHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)

### CSM
放电短路时关闭 CHG 驱动输出屏蔽位 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 0：放电短路时关闭 CHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 1：放电短路时不影响 CHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)

### CDM
DON(DSG_OFF_N)关闭 CHG 驱动输出屏蔽位 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 0：DON 输入为 0 时关闭 CHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 1：DON 输入不影响 CHG 驱动输出状态 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)

### CCM
CON(CHG_OFF_N)关闭 CHG 驱动输出屏蔽位 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 0：CON 输入为 0 时关闭 CHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 1：CON 输入不影响 CHG 驱动输出状态 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)

### CPCM
PCHG 开启时关闭 CHG 驱动输出屏蔽位 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 0：PCHG 开启时关闭 CHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 1：PCHG 开启时不影响 CHG 驱动输出状态 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)

### CBDM
充电 NFET 体二极管保护屏蔽位 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 0：放电电流大于充电 NFET 体二极管续流阈值时自动开启 CHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)
- 1：放电电流大于充电 NFET 体二极管续流阈值时不影响 CHG 驱动输出状态 [DVC1124-2参考手册_V1.2.pdf PDF p.17](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=17)

### HSFM
高边 FET 驱动输出屏蔽位 [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)
- 0：允许高边 FET 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)
- 1：屏蔽高边 FET 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)

### CAEW
芯片工作状态下 CADC 使能控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)
- 0：芯片工作状态下关闭 CADC [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)
- 1：芯片工作状态下开启 CADC [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)

### CAES
芯片休眠状态下电流唤醒使能控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)
- 0：芯片休眠状态下关闭电流唤醒 [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)
- 1：芯片休眠状态下开启电流唤醒 [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)

### CAMZ
CADC 手动校准控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)
- 0：无效 [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)
- 1：开启一次 CADC 校准，该 bit 会自动复位为 0 [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)

### C1OW[1:0]
CADC CC1 测量时间控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)
- 00：0.5ms [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)
- 01：1.0ms [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)
- 10：2.0ms [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)
- 11：4.0ms [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)

### C1OS[1:0]
芯片休眠状态下电流唤醒时间控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)
- 00：4ms [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)
- 01：8ms [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)
- 10：16ms [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)
- 11：32ms [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)

### OCD1T[7:0]
1 级放电过流保护阈值控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)
- 0x00：关闭 [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)
- 其他：阈值电压=OCD1T×0.25mV [DVC1124-2参考手册_V1.2.pdf PDF p.18](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=18)

### OCC1T[7:0]
1 级充电过流保护阈值控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)
- 0x00：关闭 [DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)
- 其他：阈值电压=OCC1T×0.25mV [DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)

### OCD1D[7:0]
1 级放电过流保护延迟控制位，延迟时间=(OCD1D+1)×8ms [DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)

### OCCD[7:0]
1 级充电过流保护延迟控制位，延迟时间=(OCC1D+1)×8ms [DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)

### OCD2E
2 级放电过流保护使能控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)
- 0：关闭 [DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)
- 1：开启 [DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)

### OCD2T[5:0]
2 级放电过流保护阈值控制位，阈值电压=(OCD2T+1)×4mV [DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)

### OCC2E
2 级充电过流保护使能控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)
- 0：关闭 [DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)
- 1：开启 [DVC1124-2参考手册_V1.2.pdf PDF p.19](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=19)

### OCC2T[5:0]
2 级充电过流保护阈值控制位，阈值电压=(OCC2T+1)×4mV [DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)

### OCD2D[7:0]
2 级放电过流保护延迟控制位，延迟时间=(OCD2D+1)×4ms [DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)

### OCC2D[7:0]
2 级充电过流保护延迟控制位，延迟时间=(OCC2D+1)×4ms [DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)

### SCDE
放电短路保护使能控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)
- 0：关闭 [DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)
- 1：开启 [DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)

### SCDT[5:0]
放电短路保护阈值控制位，阈值电压=SCDT×10mV [DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)

### SCDD[7:0]
放电短路保护延迟控制位，延迟时间=SCDD×7.81μs [DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)

### CWT[7:0]
芯片休眠状态下电流唤醒阈值控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)
- 0x00：关闭休眠状态下电流唤醒 [DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)
- 其他：阈值电压=CWT×10μV [DVC1124-2参考手册_V1.2.pdf PDF p.20](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=20)

### BDPT[7:0]
充/放电 NFET 体二极管续流保护阈值控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)
- 0x00：关闭充/放电 NFET 体二极管续流保护 [DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)
- 其他：阈值电压=BDPT×40μV [DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)

### CB[n]
第 n 节电池被动均衡控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)
- 0：关闭第 n 节电池被动均衡 [DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)
- 1：开启第 n 节电池被动均衡，该组寄存器会在 60s 后自动复位为 0 [DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)

### CM[n]
第 n 节电池保护屏蔽控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)
- 0：开启第 n 节电池保护 [DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)
- 1：关闭第 n 节电池保护，同时默认关闭第 n 节电池电压测量 [DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)

### PKM
PACK 电压测量屏蔽控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21) [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 0：开启 PACK 电压测量 [DVC1124-2参考手册_V1.2.pdf PDF p.21](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=21)
- 1：关闭 PACK 电压测量 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)

### LDM
LOAD 电压测量屏蔽控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 0：开启 LOAD 电压测量 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 1：关闭 LOAD 电压测量 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)

### CTM
芯片核心温度测量屏蔽控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 0：开启芯片核心温度测量 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 1：关闭芯片核心温度测量 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)

### V1P8M
V1P8 电压测量屏蔽控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 0：开启 V1P8 电压测量 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 1：关闭 V1P8 电压测量 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)

### CPVS[2:0]
电荷泵输出电压控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 000：关闭 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 001：6V [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 010：7V [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 011：8V [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 100：9V [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 101：10V [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 110：11V [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 111：12V [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)

### COW
电池采集线断线检测控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 0：关闭 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 1：开启断线检测，该 bit 会在 1s 后自动复位为 0 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)

### CMM
屏蔽电池电压测量控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 0：关闭屏蔽电池电压测量 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 1：开启屏蔽电池电压测量 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)

### CVS
电池电压有符号数显示控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 0：电池电压以无符号数显示，LSB=100μV [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)
- 1：电池电压以有符号数显示，LSB=200μV [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22)

### VAE
VADC 使能控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.22](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=22) [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 0：关闭 [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 1：开启 [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)

### VASM
VADC 与 CADC CC2 同步测量控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 0：VADC 连续测量 [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 1：VADC 与 CADC CC2 同步测量 [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)

### VAMP[1:0]
VADC 同步测量周期控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 00：每 1 个 CC2 周期 VADC 测量 1 次 [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 01：每 2 个 CC2 周期 VADC 测量 1 次 [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 10：每 4 个 CC2 周期 VADC 测量 1 次 [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 11：每 8 个 CC2 周期 VADC 测量 1 次 [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)

### VAO[1:0]
VADC 测量时间控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 00：0.79ms [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 01：1.54ms [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 10：3.03ms [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 11：6.02ms [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)

### COVT[11:0]
电池过压保护阈值控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 0x00：关闭电池过压保护 [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 其他：阈值电压=COVT×1mV+500mV [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)

### COVD[3:0]
电池过压保护延迟控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23) [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 0000：200ms [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 0001：300ms [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 0010：400ms [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 0011：500ms [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 0100：600ms [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 0101：700ms [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 0110：800ms [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 0111：900ms [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 1000：1s [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 1001：2s [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 1010：3s [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 1011：4s [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 1100：5s [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 1101：6s [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 1110：7s [DVC1124-2参考手册_V1.2.pdf PDF p.23](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=23)
- 1111：8s [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)

### CUVT[11:0]
电池欠压保护阈值控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 0x00：关闭电池欠压保护 [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 其他：阈值电压=CUVT×1mV [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)

### CUVD[3:0]
电池欠压保护延迟控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 0000：200ms [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 0001：300ms [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 0010：400ms [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 0011：500ms [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 0100：600ms [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 0101：700ms [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 0110：800ms [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 0111：900ms [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 1000：1s [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 1001：2s [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 1010：3s [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 1011：4s [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 1100：5s [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 1101：6s [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 1110：7s [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 1111：8s [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)

### GP1M[1:0]
GP1 模式控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 00：关闭，GP1 为高阻态 [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 01：热敏电阻检测输入 [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 10：模拟电压检测输入 [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 11：CON(CHG_OFF_N)，CHG 驱动硬线控制，低电平关闭 CHG 驱动输出，高电平不影响 CHG 驱动输出状态 [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)

### GP2M[2:0]
GP2 模式控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24) [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 000：关闭，GP2 为高阻态 [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 001：热敏电阻检测输入 [DVC1124-2参考手册_V1.2.pdf PDF p.24](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=24)
- 010：模拟电压检测输入 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 011：N/A [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 100：N/A [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 101：N/A [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 110：中断输出 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 111：低边 PDSG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)

### GP3M[2:0]
GP3 模式控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 000：关闭，GP3 为高阻态 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 001：热敏电阻检测输入 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 010：模拟电压检测输入 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 011：N/A [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 100：N/A [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 101：N/A [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 110：中断输出 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 111：低边 PCHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)

### GP4M[1:0]
GP4 模式控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 00：关闭，GP4 为高阻态 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 01：热敏电阻检测输入 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 10：模拟电压检测输入 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 11：DON(DSG_OFF_N)，DSG 驱动硬线控制，低电平关闭 DSG 驱动输出，高电平不影响 DSG 驱动输出状态 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)

### GP5M[2:0]
GP5 模式控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 000：关闭，GP5 为高阻态 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 001：热敏电阻检测输入 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 010：模拟电压检测输入 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 011：N/A [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 100：N/A [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 101：N/A [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 110：中断输出 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 111：低边 CHG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)

### GP6M[2:0]
GP6 模式控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 000：关闭，GP6 为高阻态 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 001：热敏电阻检测输入 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 010：模拟电压检测输入 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 011：N/A [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 100：N/A [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 101：N/A [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 110：中断输出 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)
- 111：低边 DSG 驱动输出 [DVC1124-2参考手册_V1.2.pdf PDF p.25](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=25)

### COTF
芯片内核过温关机标识位，该 bit 会在读取后自动复位为 0 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 0：未发生过芯片内核过温 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 1：已发生过芯片内核过温 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)

### COTT[6:0]
芯片内核过温保护阈值控制位, [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 0x00：关闭芯片内核过温保护 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 其他：阈值温度=(1466+COTT×2)×0.24467°C-271.03°C [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)

### V3P3ES
芯片休眠状态下 V3P3 输出控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 0：芯片休眠状态下关闭 V3P3 输出 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 1：芯片休眠状态下开启 V3P3 输出 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)

### V3P3EW
芯片工作状态下 V3P3 输出控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 0：芯片工作状态下关闭 V3P3 输出 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 1：芯片工作状态下开启 V3P3 输出 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)

### V3P3M
I2C 超时重启 V3P3 控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 0：I2C 超时不影响 V3P3 输出 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 1：I2C 超时后 V3P3 输出关闭 1s 后重启 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)

### IWTS
I2C 看门狗超时状态位 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 0：未超时 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 1：已超时 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)

### IWT[2:0]
I2C 看门狗定时器控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 0XX：关闭定时器 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 100：定时器设为 4s [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 101：定时器设为 8s [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 110：定时器设为 16s [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 111：定时器设为 32s [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)

### TIWK
定时唤醒状态位 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 0：关闭 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)
- 1：开启 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26)

### TWSE[3:0]
定时唤醒定时器控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.26](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=26) [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 0000：关闭 [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 0001：10s [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 0010：20s [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 0011：30s [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 0100：40s [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 0101：50s [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 0110：1min [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 0111：2min [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 1000：3min [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 1001：4min [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 1010：5min [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 1011：6min [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 1100：7min [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 1101：8min [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 1110：9min [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 1111：10min [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)

### IWM
唤醒中断输出屏蔽控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 0：芯片唤醒后中断输出低电平 1ms [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 1：芯片唤醒后中断无输出 [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)

### IVOM
VADC 转换结束中断输出屏蔽控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 0：VADC 转换结束后中断输出低电平 1ms [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)
- 1：VADC 转换结束后中断无输出 [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27)

### ICCM
CADC CC2 转换结束中断输出屏蔽控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.27](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=27) [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)
- 0：CADC CC2 转换结束后中断输出低电平 1ms [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)
- 1：CADC CC2 转换结束后中断无输出 [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)

### ICOM
电池过压中断输出屏蔽控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)
- 0：电池过压后中断输出低电平 1ms [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)
- 1：电池过压后中断无输出 [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)

### ICUM
电池欠压中断输出屏蔽控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)
- 0：电池欠压后中断输出低电平 1ms [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)
- 1：电池欠压后中断无输出 [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)

### IOC1M
1 级过流(包含 1 级放电过流和 1 级充电过流)中断输出屏蔽控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)
- 0：1 级过流后中断输出低电平 1ms [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)
- 1：1 级过流后中断无输出 [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)

### IOC2M
2 级过流(包含 2 级放电过流和 2 级充电过流)中断输出屏蔽控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)
- 0：2 级过流后中断输出低电平 1ms [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)
- 1：2 级过流后中断无输出 [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)

### ISCDM
放电短路中断输出屏蔽控制位 [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)
- 0：放电短路后中断输出低电平 1ms [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)
- 1：放电短路后中断无输出 [DVC1124-2参考手册_V1.2.pdf PDF p.28](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=28)

### FRT[7:0]
存储在 FUSE 中电阻修调值；电阻值=6800Ω+FRT×25Ω [DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)

### CV[7:0]
芯片版本 [DVC1124-2参考手册_V1.2.pdf PDF p.29](../../sources/DVC1124-2%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C_V1.2.pdf#page=29)

## 12. 原文与覆盖附录

- [数据手册逐页完整提取](../../extracted/DVC1124_2_DS_V1_1/full_by_page.md)：保留全部文字；图像内容须回看 PDF
- [参考手册逐页完整提取](../../extracted/DVC1124_2_RM_V1_2/full_by_page.md)：保留全部文字；门级逻辑/图像以 PDF 为准
- [机器可读寄存器](registers.json)、[逐页覆盖](coverage.json)、[验证与未决事项](validation.md)

完整提取仅用于追溯；本参考的第 1～9 节已综合电气、通信、转换、保护、控制及实施，第 10～11 节完整展开寄存器位图与字段编码。没有提供安全认证、量产固件或任何实物测试结论。
