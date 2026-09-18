# C037 Sci_Upper 与 D008 参数、协议及持久化对照（2026-09-18）

## 核对范围与结论

参考：C037 `2e6a5cb`，用户指定目录下 `Code/Source/Sci_Upper.c`；Keil 工程实际包含该文件、`SH367309_DataDeal.c`、`EEPROM.c`、`LogRecord.c`。当前：D008 `refactor/d008-common-bms-features`，审核基线 `d84d34b`，实际入口为 `modbus_rtu.c`（固定 source_order 已包含），不是历史 Sci_Upper 副本。本轮只审查，不修改固件、客户端、保护参数或 Flash。

C037 保存参数主要走外部 I2C EEPROM（`EEPROM.c:299,395,864`），不能称为和 D008 相同的 MCU Flash 实现。D008 通过 Config/State/Event 与 `storage_record` 落到 Telink Flash；以下按“掉电保留能力”对比。

D008 是部分语义兼容，**不支持的普通地址可能读零、写成功却不做任何操作**：`modbus_rtu.c:396` 的读兜底为 0，`:475` 写兜底为 0（无异常）；仅判断应答成功不能证明实现/生效/持久化。

## 1. 参数与 Flash：逐组对照

表中的“未接入”表示 D008 当前协议没有处理该范围，不等于业务完全不存在；例如睡眠有逻辑但参数由宏提供。参考侧“可写保存”表示源码存在挂接，不代表其每次 ACK 都等待 EEPROM 完成。

| 地址 | C037 内容与实际实现 | 当前 D008 | D008 掉电保留结论 |
|---|---|---|---|
| `2000..205D` | 47 组测量 K/B：32 路单体、AFE1/2 总压、VBUS、充放电流、10 路温度；`Sci_Upper.c:668,1458`，按 K 起点写两个字并置 EEPROM 写标志 | 读写未接入，未发现该校准表的 Config owner | 不能经此协议修改/保存校准 |
| `2100..213B` | 12 组软件保护，每组 First/Second/Third/Recover/Filter | 已读写；候选完整校验，`SaveParam` 失败回滚 RAM 并报异常 | Config Flash，可持久化；SW/TEMP 编译开关仍决定哪些算法执行 |
| `213C..2140` | 最后 5 字 SOC 保护组 | 字段可读写、可保存；`bms_sw_protection.c:19` 明确未纳入算法 | **可保存但不产生 SOC 保护动作** |
| `2200..2229` | 21 个电压/SOC 点；可读，写函数 `Sci_Upper.c:1556` 整体注释 | 未接入；当前 SOC 用自己的 profile | 两边都不能据该写函数认定已支持在线 OCV 表更新 |
| `222A..2249` | 16 个铜损值及16个通道号；可读，写函数 `:1575` 注释 | 未接入 | 当前不能修改/保存；参考同样无有效写实现 |
| `224A..2255` | RTC 当前时间/闹钟 12 字；读返回0，写函数 `:1595` 注释 | 未接入 | 两边都不是完整 RTC 参数协议 |
| `2300..2307` | 均衡开启电压/压差/关闭条件及奇偶/MOS时间等 8 字；`:1614` 更新 OtherElement 并置保存标志 | 未接入旧均衡参数块；公共均衡目前使用软件压差 First 等既有配置与芯片策略 | 旧块不能修改/保存；不代表 D008 没有均衡 |
| `2308..230F` | 电流限制/短路及旧冷却参数；`:1641` RAM 更新8字，保存标志只置前4字，刷新 PWM/SH 短路 | 未接入；DVC 短路走 `2500` 独立硬件 profile | 不能经旧块保存；参考后4字也不能当作正常持久化 |
| `2310..2317` | 正常/低压睡眠电压、时间、充放电流等8字；`:1676` 保存并刷新状态 | 未接入；当前 `conf.h` 和 `app.c::blt_pm_proc` 固定宏/策略 | 不能通过协议调休眠参数并保存 |
| `2318` | 额定容量，参考按 `2318` 起点4字写入 | 未接入写；但 Config 中已有 `capacity_factory`，SOC 会读取 | **有存储能力，但无这个协议入口**；不能只补 UI |
| `2319` | 循环次数（参考为4字 SOC 参数块的第二字） | 特殊写处理，更新 SOC RAM；该地址读没有映射，返回0 | 后续主循环 State 保存，正常60s、失败5s退避；ACK 不是本次落盘保证 |
| `231A..231B` | SOC 保留字段 | 未接入 | 不应擅自赋予业务 |
| `231C..231F` | 串数、采样电阻、并联数量、预充/保留字；`:1731` 保存，刷新 SeriesNum/AFE 参数 | 未接入旧块；DVC cell count/Rsense 为编译固定值 | **属于当前产品明确固定所有权**；不能直接照搬动态修改 |
| `2320..2337` | 加热/冷却24字；`:1759` RAM及EEPROM挂接 | 未接入旧参数块；当前有公共加热策略，但没有这24字配置事务 | 无该块在线修改/Flash保存 |
| `2400..2417` | SH367309 24字硬件参数，`SH367309_DataDeal.c:328` 写 EEPROM、置 AFE apply 标志 | **当前已没有旧 2400 映射**，读0/普通写落空 | 硬件保护能力由新 `2500` profile 提供，不兼容原地址/字段 |
| `FFF0/FFF1/FFF2` | SN/硬件版本/软件版本字符串写入口，`:1802` 后按命令选择并置保存标志 | 无写入口；`WriteProID_Default` 每次启动从编译字符串生成 | 生产信息只读默认值，无法经旧协议修改持久化 |

软件保护 `2100..2140` 为65字，支持 `0x06` 单写和 `0x10` 候选整体校验/一次保存。跨帧批量不是全局原子事务；无关地址的默认 ACK 仍不代表支持。

## 2. 当前真正可配置/持久化的扩展能力

| 接口/数据 | 实际状态 | 保存与错误边界 |
|---|---|---|
| `2500..2522` AFE Requested 35字 | 完整授权事务；COV/CUV/OCD/OCC/SCD、延时、恢复、enable mask | Config 保存 -> AFE apply -> readback；失败尝试 rollback，记录 apply_state/last_error |
| `2523..252B` 元数据，`2540..2562` Effective | 只读 | Effective 是量化/实际配置，不是第二份可写参数 |
| 自定义功能 `0x42` | AFE 授权与 MTU23 分片提交已实现 | 分片未完成不应用；最终仍走完整 profile 事务 |
| `0100..010F` BLE 名称窗口 | `0x10` 写后缀，`0x03` 读名称；`0x06` 没有名字修改处理 | Config Flash 已接入；但名称保存失败仅记存储错误，外层仍返回成功，必须读回核对 |
| `1005` 单次 SOC | 修改当前 SOC RAM、显示/积分状态 | 由 State 周期保存/关机保存；不是同步落盘；旧地址读没有值 |
| `1102=3` 工厂模式重入 | State runtime reset | 返回保存失败异常；与 C037 命令含义冲突，见下 |
| `1007=1` 清事件日志 | 独立 Event reset 事务 | 保存失败恢复旧快照并返回异常 |
| SOC 算法 profile/化学体系 | 有 `bms_soc_set_product_config`、Config 存储 API | 在现有 Modbus 分发中未找到调用入口，不等于上位机已可配置 |
| SOC/容量学习/循环/工厂分钟数 | State owner 自动管理 | 自动持久化和在线可编辑是两件事 |
| `2800` DVC semantic / `2900` raw | 固定配置与诊断只读 | 不是待补写缺陷；禁止旧 operating-config Flash 再覆盖固件固定配置 |
| `2A00..2DFF` 诊断和 RAM Trace | 只读，写拒绝 | Trace 掉电清空；不得当作 Flash 历史日志 |

固定参数包括 DVC cell count/Rsense、GP模式、低边拓扑、高边mask、Charge Pump、CC1/VADC、V3P3、WDT、I2C timeout-close、Body-Diode 等。修改这些需要编译固件；不是所有 `2800` 字段都应变成 Flash 参数。外部温度保护应走软件保护组，不能复制 SH367309 的硬件温度字段到 DVC。

## 3. 命令对照：相同地址不一定同语义

| 地址 | C037 `0x06` | D008 当前行为 |
|---|---|---|
| `1000` | 校准 K/B 恢复（含 `55AA..55B0` 子命令） | 未实现，可能 ACK 成功 |
| `1001` | 清保护记录 | 未实现；D008故障历史在RAM，Event是独立模块 |
| `1002` | 恢复软件保护默认值 | 未实现这个外部命令；启动 revision 机制不是此命令 |
| `1003` | 恢复 OtherElement 默认值 | 未实现 |
| `1004` | 恢复 Heat/Cool 默认值 | 未实现 |
| `1005` | SOC ≤100 校验后设置 | 已设置，但直接将16位 val传给8位 SOC API，缺少同等入口范围校验 |
| `1006` | 恢复 SH AFE 参数默认值 | 未实现此命令；不能用SH默认覆盖DVC |
| `1007=1` | 清事件记录 | 已实现且单独持久化 |
| `1100/1101` | 开关命令，但参考函数体也为空 | D008未实现；不计为参考已完成功能 |
| `1102` | 1..32功能位开启，多数保存 System_OnOFF_Func；3为MOS/接触器；A/B睡眠 | D008仅3=工厂模式重入、A=断电请求；宏 `__TEST_SOC__` 下1为SOC测试。其余值仍可能成功无动作 |
| `1103` | 1..32功能位关闭，多数持久化 | 只有 `__TEST_SOC__` 下1的测试处理；无通用功能位关闭/Flash保存 |
| `FFFD`，`0x10` | 写 MCU IAP 标志并准备升级 | 旧 IAP 命令未实现；D008 已有 Telink BLE OTA 独立通道，不能据此说完全不能升级 |

**旧上位机不可直接把 `1102=3` 当成开启 MOS 来发送给 D008。** 这是同地址行为冲突，优先级高于单纯少了一个读取字段。

## 4. 读协议/地址兼容范围

| 范围/请求起点 | D008 对比结果 |
|---|---|
| `D000..D03E` | 已提供63字实时报告；仅可确认布局读取入口，错误/状态bit的芯片业务含义不能仅凭地址相同保证完全等价 |
| `D100..D102` | RTC返回0；参考源码同样为0占位 |
| `D103..D108` | 已提供三级最近故障历史；D008由RAM环形保存，不保证MCU复位后仍有记录 |
| `D109..D114` | 已提供错误状态打包；错误枚举仍须按D008解析 |
| `D115..D116` | 已提供系统状态 |
| `D117..D118` | **缺少 C037 System_OnOFF_Func 读值**，目前为0 |
| `D119..D11F` | 普通兜底0；参考对应保留0 |
| `D120` | C03733字状态块最后一个保留字为0；D008复用为扩展实时 magic `4253`，旧整块读取最后一字语义改变 |
| `D120..D12A` | D008新增版本化实时窗口，不能套用C037旧表 |
| `D200` | 返回0；参考也只是保留0，不算业务缺失 |
| `C000` | C037 LCD页面；D008无对应页面 |
| `C001` | C037完整实时/记录页面；D008无对应页面 |
| `C002` | 已有生产信息读取：SN/HW/SW各32字节；需从此起点按页面约定读 |
| `C003` | C037系统全状态页面；D008没有该页面，落在SN字符串窗口里，不是系统状态 |
| `C004..C007` | 参考未实现独立页面；D008属于生产信息连续窗口，不能混称新增页面 |
| `C008` | 已实现100条事件，按新到旧每条2字节；清除命令`1007`也有 |
| `C008+offset` 分页 | **未实现事件offset分页**。特殊分发只在起点等于C008时生效；改起点可能读到生产字符串或0。只能按现有完整事件页面方式请求 |

功能码双方均有 `0x03/0x06/0x10`，缺的是地址、语义和保存链路，不是缺这三个功能码。D008另有`0x42`和`0x7F`调试回显。参考的私有错误编号与D008的标准异常`01/02/03/04`也不能直接等同。

## 5. 实现优先级建议（本次未实施）

1. 先建立读写白名单和明确“不支持”异常，补齐 `1102/1103` 的 capability/兼容性区分；不能为了检测不支持而向实板盲写命令。
2. 补有真实产品用途的“参数 owner + 校验 + 生效 + 持久化 + 读回”闭环：额定容量、校准、生产信息、休眠参数、均衡/加热配置。不能仅在 `write_reg` 加 RAM 赋值就宣称完成。
3. 统一名称/SOC/循环次数的保存成功语义：明确同步落盘还是等待周期保存，失败可被上位机识别。
4. 老 `2400` 需要先制定 SH 到 DVC 的语义映射与不支持字段策略；优先使用已存在的`2500`，不恢复第二参数 owner。
5. 保留串数/Rsense/GP/WDT等当前固定所有权；若将来要求在线修改，属于独立产品设计变更，不在此次差异补齐中默认开放。

## 6. 源码定位

当前关键位置：

- `modbus_rtu.c:396` 读取分发；`:475` 写分发；`:543` 软件保护保存；`:587` 功能码分发；`:781`事件页面；`:874`生产信息默认值。
- `param.c:72` `SaveParam` -> `bms_config_store_set_protect` -> `storage_record_save` -> Telink Flash。
- `modbus_rtu.c:184` AFE完整事务，`bms_afe_hw_access.c`分片入口。
- `SocEnhance.c:1129` SOC仅RAM更新；`app.c`末尾State周期保存；`bms_state_store.c:200`保存节流。
- `btname_modbus.c::btname_modbus_on_write_holding` 写名/失败路径。
- 参考 `Sci_Upper.c:138/201`写入口，`:655/668/684/737`读参数组，`:1458..1868`写参数组，`:1870..2172`命令处理。

纯源码核对，未编译/烧录参考工程，未向设备写参数，不声称已做掉电实测。

参考入口：[Sci_Upper.c](<E:/sync/work project/git/+++A002/C037/新建文件夹/c037- - 电流没问题 - bug - 副本/Code/Source/Sci_Upper.c:138>)；当前入口：[modbus_rtu.c](<D:/telink/tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001 (1)/tc_ble_single_sdk-V3.4.2.8_Patch_0001/tc_ble_single_sdk/vendor/ble_sample/modbus_rtu.c:396>)。

### 审核文件 SHA-256

- `Sci_Upper.c`：`7f968091e41da9f5bc1e9220bbb2af4e939637478560945fde6b1ee165c4fdf7`
- `Sci_Upper.h`：`4b8188048d9446d306827ede9b78737c98f86f9ed990664fd9a78c2d1f4e769b`
- `modbus_rtu.c`：`a10df78dba3efea810817bf0178072e300526b90981bc1f300639b9790a2fa5a`

### 参考枚举逐地址索引

以下由活动 `RS485_CMD_RW_E` 枚举顺序解析，排除 `#if 0` 与注释；**只是地址索引，不表示参考每项写函数均有效**。能力以以上表及函数体为准。

| 地址 | 参考符号 |
|---|---|
| `0x1000` | `RS485_CMD_ADDR_RESET_CALIB_COEF` |
| `0x1001` | `RS485_CMD_ADDR_RESET_PROTECT_RECORD` |
| `0x1002` | `RS485_CMD_ADDR_RESET_PROTECT_ELEMENT` |
| `0x1003` | `RS485_CMD_ADDR_RESET_OTHER_CANADD` |
| `0x1004` | `RS485_CMD_ADDR_RESET_HEAT_COOL` |
| `0x1005` | `RS485_CMD_ADDR_SET_ONCE_SOC` |
| `0x1006` | `RS485_CMD_ADDR_RESET_AFE_PARAMETERS` |
| `0x1007` | `RS485_CMD_ADDR_RESET_EVENT_RECORD` |
| `0x1100` | `RS485_CMD_ADDR_SWITCH_ON` |
| `0x1101` | `RS485_CMD_ADDR_SWITCH_OFF` |
| `0x1102` | `RS485_CMD_ADDR_SYSTEM_FUNCTION_ON` |
| `0x1103` | `RS485_CMD_ADDR_SYSTEM_FUNCTION_OFF` |
| `0x2000` | `RS485_CMD_ADDR_VC1CALIB_K` |
| `0x2001` | `RS485_CMD_ADDR_VC1CALIB_B` |
| `0x2002` | `RS485_CMD_ADDR_VC2CALIB_K` |
| `0x2003` | `RS485_CMD_ADDR_VC2CALIB_B` |
| `0x2004` | `RS485_CMD_ADDR_VC3CALIB_K` |
| `0x2005` | `RS485_CMD_ADDR_VC3CALIB_B` |
| `0x2006` | `RS485_CMD_ADDR_VC4CALIB_K` |
| `0x2007` | `RS485_CMD_ADDR_VC4CALIB_B` |
| `0x2008` | `RS485_CMD_ADDR_VC5CALIB_K` |
| `0x2009` | `RS485_CMD_ADDR_VC5CALIB_B` |
| `0x200A` | `RS485_CMD_ADDR_VC6CALIB_K` |
| `0x200B` | `RS485_CMD_ADDR_VC6CALIB_B` |
| `0x200C` | `RS485_CMD_ADDR_VC7CALIB_K` |
| `0x200D` | `RS485_CMD_ADDR_VC7CALIB_B` |
| `0x200E` | `RS485_CMD_ADDR_VC8CALIB_K` |
| `0x200F` | `RS485_CMD_ADDR_VC8CALIB_B` |
| `0x2010` | `RS485_CMD_ADDR_VC9CALIB_K` |
| `0x2011` | `RS485_CMD_ADDR_VC9CALIB_B` |
| `0x2012` | `RS485_CMD_ADDR_VC10CALIB_K` |
| `0x2013` | `RS485_CMD_ADDR_VC10CALIB_B` |
| `0x2014` | `RS485_CMD_ADDR_VC11CALIB_K` |
| `0x2015` | `RS485_CMD_ADDR_VC11CALIB_B` |
| `0x2016` | `RS485_CMD_ADDR_VC12CALIB_K` |
| `0x2017` | `RS485_CMD_ADDR_VC12CALIB_B` |
| `0x2018` | `RS485_CMD_ADDR_VC13CALIB_K` |
| `0x2019` | `RS485_CMD_ADDR_VC13CALIB_B` |
| `0x201A` | `RS485_CMD_ADDR_VC14CALIB_K` |
| `0x201B` | `RS485_CMD_ADDR_VC14CALIB_B` |
| `0x201C` | `RS485_CMD_ADDR_VC15CALIB_K` |
| `0x201D` | `RS485_CMD_ADDR_VC15CALIB_B` |
| `0x201E` | `RS485_CMD_ADDR_VC16CALIB_K` |
| `0x201F` | `RS485_CMD_ADDR_VC16CALIB_B` |
| `0x2020` | `RS485_CMD_ADDR_VC17CALIB_K` |
| `0x2021` | `RS485_CMD_ADDR_VC17CALIB_B` |
| `0x2022` | `RS485_CMD_ADDR_VC18CALIB_K` |
| `0x2023` | `RS485_CMD_ADDR_VC18CALIB_B` |
| `0x2024` | `RS485_CMD_ADDR_VC19CALIB_K` |
| `0x2025` | `RS485_CMD_ADDR_VC19CALIB_B` |
| `0x2026` | `RS485_CMD_ADDR_VC20CALIB_K` |
| `0x2027` | `RS485_CMD_ADDR_VC20CALIB_B` |
| `0x2028` | `RS485_CMD_ADDR_VC21CALIB_K` |
| `0x2029` | `RS485_CMD_ADDR_VC21CALIB_B` |
| `0x202A` | `RS485_CMD_ADDR_VC22CALIB_K` |
| `0x202B` | `RS485_CMD_ADDR_VC22CALIB_B` |
| `0x202C` | `RS485_CMD_ADDR_VC23CALIB_K` |
| `0x202D` | `RS485_CMD_ADDR_VC23CALIB_B` |
| `0x202E` | `RS485_CMD_ADDR_VC24CALIB_K` |
| `0x202F` | `RS485_CMD_ADDR_VC24CALIB_B` |
| `0x2030` | `RS485_CMD_ADDR_VC25CALIB_K` |
| `0x2031` | `RS485_CMD_ADDR_VC25CALIB_B` |
| `0x2032` | `RS485_CMD_ADDR_VC26CALIB_K` |
| `0x2033` | `RS485_CMD_ADDR_VC26CALIB_B` |
| `0x2034` | `RS485_CMD_ADDR_VC27CALIB_K` |
| `0x2035` | `RS485_CMD_ADDR_VC27CALIB_B` |
| `0x2036` | `RS485_CMD_ADDR_VC28CALIB_K` |
| `0x2037` | `RS485_CMD_ADDR_VC28CALIB_B` |
| `0x2038` | `RS485_CMD_ADDR_VC29CALIB_K` |
| `0x2039` | `RS485_CMD_ADDR_VC29CALIB_B` |
| `0x203A` | `RS485_CMD_ADDR_VC30CALIB_K` |
| `0x203B` | `RS485_CMD_ADDR_VC30CALIB_B` |
| `0x203C` | `RS485_CMD_ADDR_VC31CALIB_K` |
| `0x203D` | `RS485_CMD_ADDR_VC31CALIB_B` |
| `0x203E` | `RS485_CMD_ADDR_VC32CALIB_K` |
| `0x203F` | `RS485_CMD_ADDR_VC32CALIB_B` |
| `0x2040` | `RS485_CMD_ADDR_AFE1CALIB_K` |
| `0x2041` | `RS485_CMD_ADDR_AFE1CALIB_B` |
| `0x2042` | `RS485_CMD_ADDR_AFE2CALIB_K` |
| `0x2043` | `RS485_CMD_ADDR_AFE2CALIB_B` |
| `0x2044` | `RS485_CMD_ADDR_VBUSCALIB_K` |
| `0x2045` | `RS485_CMD_ADDR_VBUSCALIB_B` |
| `0x2046` | `RS485_CMD_ADDR_ICHGCALIB_K` |
| `0x2047` | `RS485_CMD_ADDR_ICHGCALIB_B` |
| `0x2048` | `RS485_CMD_ADDR_IDISCHGCALIB_K` |
| `0x2049` | `RS485_CMD_ADDR_IDISCHGCALIB_B` |
| `0x204A` | `RS485_CMD_ADDR_TEMP1_CALIB_K` |
| `0x204B` | `RS485_CMD_ADDR_TEMP1_CALIB_B` |
| `0x204C` | `RS485_CMD_ADDR_TEMP2_CALIB_K` |
| `0x204D` | `RS485_CMD_ADDR_TEMP2_CALIB_B` |
| `0x204E` | `RS485_CMD_ADDR_TEMP3_CALIB_K` |
| `0x204F` | `RS485_CMD_ADDR_TEMP3_CALIB_B` |
| `0x2050` | `RS485_CMD_ADDR_TEMP4_CALIB_K` |
| `0x2051` | `RS485_CMD_ADDR_TEMP4_CALIB_B` |
| `0x2052` | `RS485_CMD_ADDR_TEMP5_CALIB_K` |
| `0x2053` | `RS485_CMD_ADDR_TEMP5_CALIB_B` |
| `0x2054` | `RS485_CMD_ADDR_TEMP6_CALIB_K` |
| `0x2055` | `RS485_CMD_ADDR_TEMP6_CALIB_B` |
| `0x2056` | `RS485_CMD_ADDR_TEMP_ENV1_CALIB_K` |
| `0x2057` | `RS485_CMD_ADDR_TEMP_ENV1_CALIB_B` |
| `0x2058` | `RS485_CMD_ADDR_TEMP_ENV2_CALIB_K` |
| `0x2059` | `RS485_CMD_ADDR_TEMP_ENV2_CALIB_B` |
| `0x205A` | `RS485_CMD_ADDR_TEMP_ENV3_CALIB_K` |
| `0x205B` | `RS485_CMD_ADDR_TEMP_ENV3_CALIB_B` |
| `0x205C` | `RS485_CMD_ADDR_TEMP_MOS_CALIB_K` |
| `0x205D` | `RS485_CMD_ADDR_TEMP_MOS_CALIB_B` |
| `0x2100` | `RS485_CMD_ADDR_VCELL_OVP_FIRST` |
| `0x2101` | `RS485_CMD_ADDR_VCELL_OVP_SECOND` |
| `0x2102` | `RS485_CMD_ADDR_VCELL_OVP_THIRD` |
| `0x2103` | `RS485_CMD_ADDR_VCELL_OVP_RCV` |
| `0x2104` | `RS485_CMD_ADDR_VCELL_OVP_FILTER` |
| `0x2105` | `RS485_CMD_ADDR_VCELL_UVP_FIRST` |
| `0x2106` | `RS485_CMD_ADDR_VCELL_UVP_SECOND` |
| `0x2107` | `RS485_CMD_ADDR_VCELL_UVP_THIRD` |
| `0x2108` | `RS485_CMD_ADDR_VCELL_UVP_RCV` |
| `0x2109` | `RS485_CMD_ADDR_VCELL_UVP_FILTER` |
| `0x210A` | `RS485_CMD_ADDR_VBUS_OVP_FIRST` |
| `0x210B` | `RS485_CMD_ADDR_VBUS_OVP_SECOND` |
| `0x210C` | `RS485_CMD_ADDR_VBUS_OVP_THIRD` |
| `0x210D` | `RS485_CMD_ADDR_VBUS_OVP_RCV` |
| `0x210E` | `RS485_CMD_ADDR_VBUS_OVP_FILTER` |
| `0x210F` | `RS485_CMD_ADDR_VBUS_UVP_FIRST` |
| `0x2110` | `RS485_CMD_ADDR_VBUS_UVP_SECOND` |
| `0x2111` | `RS485_CMD_ADDR_VBUS_UVP_THIRD` |
| `0x2112` | `RS485_CMD_ADDR_VBUS_UVP_RCV` |
| `0x2113` | `RS485_CMD_ADDR_VBUS_UVP_FILTER` |
| `0x2114` | `RS485_CMD_ADDR_ICHG_OCP_FIRST` |
| `0x2115` | `RS485_CMD_ADDR_ICHG_OCP_SECOND` |
| `0x2116` | `RS485_CMD_ADDR_ICHG_OCP_THIRD` |
| `0x2117` | `RS485_CMD_ADDR_ICHG_OCP_RCV` |
| `0x2118` | `RS485_CMD_ADDR_ICHG_OCP_FILTER` |
| `0x2119` | `RS485_CMD_ADDR_IDSG_OCP_FIRST` |
| `0x211A` | `RS485_CMD_ADDR_IDSG_OCP_SECOND` |
| `0x211B` | `RS485_CMD_ADDR_IDSG_OCP_THIRD` |
| `0x211C` | `RS485_CMD_ADDR_IDSG_OCP_RCV` |
| `0x211D` | `RS485_CMD_ADDR_IDSG_OCP_FILTER` |
| `0x211E` | `RS485_CMD_ADDR_TCHG_OTP_FIRST` |
| `0x211F` | `RS485_CMD_ADDR_TCHG_OTP_SECOND` |
| `0x2120` | `RS485_CMD_ADDR_TCHG_OTP_THIRD` |
| `0x2121` | `RS485_CMD_ADDR_TCHG_OTP_RCV` |
| `0x2122` | `RS485_CMD_ADDR_TCHG_OTP_FILTER` |
| `0x2123` | `RS485_CMD_ADDR_TCHG_UTP_FIRST` |
| `0x2124` | `RS485_CMD_ADDR_TCHG_UTP_SECOND` |
| `0x2125` | `RS485_CMD_ADDR_TCHG_UTP_THIRD` |
| `0x2126` | `RS485_CMD_ADDR_TCHG_UTP_RCV` |
| `0x2127` | `RS485_CMD_ADDR_TCHG_UTP_FILTER` |
| `0x2128` | `RS485_CMD_ADDR_TDSG_OTP_FIRST` |
| `0x2129` | `RS485_CMD_ADDR_TDSG_OTP_SECOND` |
| `0x212A` | `RS485_CMD_ADDR_TDSG_OTP_THIRD` |
| `0x212B` | `RS485_CMD_ADDR_TDSG_OTP_RCV` |
| `0x212C` | `RS485_CMD_ADDR_TDSG_OTP_FILTER` |
| `0x212D` | `RS485_CMD_ADDR_TDSG_UTP_FIRST` |
| `0x212E` | `RS485_CMD_ADDR_TDSG_UTP_SECOND` |
| `0x212F` | `RS485_CMD_ADDR_TDSG_UTP_THIRD` |
| `0x2130` | `RS485_CMD_ADDR_TDSG_UTP_RCV` |
| `0x2131` | `RS485_CMD_ADDR_TDSG_UTP_FILTER` |
| `0x2132` | `RS485_CMD_ADDR_TMOS_OTP_FIRST` |
| `0x2133` | `RS485_CMD_ADDR_TMOS_OTP_SECOND` |
| `0x2134` | `RS485_CMD_ADDR_TMOS_OTP_THIRD` |
| `0x2135` | `RS485_CMD_ADDR_TMOS_OTP_RCV` |
| `0x2136` | `RS485_CMD_ADDR_TMOS_OTP_FILTER` |
| `0x2137` | `RS485_CMD_ADDR_VDELTA_OP_FIRST` |
| `0x2138` | `RS485_CMD_ADDR_VDELTA_OP_SECOND` |
| `0x2139` | `RS485_CMD_ADDR_VDELTA_OP_THIRD` |
| `0x213A` | `RS485_CMD_ADDR_VDELTA_OP_RCV` |
| `0x213B` | `RS485_CMD_ADDR_VDELTA_OP_FILTER` |
| `0x213C` | `RS485_CMD_ADDR_SOC_UP_FIRST` |
| `0x213D` | `RS485_CMD_ADDR_SOC_UP_SECOND` |
| `0x213E` | `RS485_CMD_ADDR_SOC_UP_THIRD` |
| `0x213F` | `RS485_CMD_ADDR_SOC_UP_RCV` |
| `0x2140` | `RS485_CMD_ADDR_SOC_UP_FILTER` |
| `0x2200` | `RS485_CMD_ADDR_SOC_VOLTAGE1` |
| `0x2201` | `RS485_CMD_ADDR_SOC_VALUE1` |
| `0x2202` | `RS485_CMD_ADDR_SOC_VOLTAGE2` |
| `0x2203` | `RS485_CMD_ADDR_SOC_VALUE2` |
| `0x2204` | `RS485_CMD_ADDR_SOC_VOLTAGE3` |
| `0x2205` | `RS485_CMD_ADDR_SOC_VALUE3` |
| `0x2206` | `RS485_CMD_ADDR_SOC_VOLTAGE4` |
| `0x2207` | `RS485_CMD_ADDR_SOC_VALUE4` |
| `0x2208` | `RS485_CMD_ADDR_SOC_VOLTAGE5` |
| `0x2209` | `RS485_CMD_ADDR_SOC_VALUE5` |
| `0x220A` | `RS485_CMD_ADDR_SOC_VOLTAGE6` |
| `0x220B` | `RS485_CMD_ADDR_SOC_VALUE6` |
| `0x220C` | `RS485_CMD_ADDR_SOC_VOLTAGE7` |
| `0x220D` | `RS485_CMD_ADDR_SOC_VALUE7` |
| `0x220E` | `RS485_CMD_ADDR_SOC_VOLTAGE8` |
| `0x220F` | `RS485_CMD_ADDR_SOC_VALUE8` |
| `0x2210` | `RS485_CMD_ADDR_SOC_VOLTAGE9` |
| `0x2211` | `RS485_CMD_ADDR_SOC_VALUE9` |
| `0x2212` | `RS485_CMD_ADDR_SOC_VOLTAGE10` |
| `0x2213` | `RS485_CMD_ADDR_SOC_VALUE10` |
| `0x2214` | `RS485_CMD_ADDR_SOC_VOLTAGE11` |
| `0x2215` | `RS485_CMD_ADDR_SOC_VALUE11` |
| `0x2216` | `RS485_CMD_ADDR_SOC_VOLTAGE12` |
| `0x2217` | `RS485_CMD_ADDR_SOC_VALUE12` |
| `0x2218` | `RS485_CMD_ADDR_SOC_VOLTAGE13` |
| `0x2219` | `RS485_CMD_ADDR_SOC_VALUE13` |
| `0x221A` | `RS485_CMD_ADDR_SOC_VOLTAGE14` |
| `0x221B` | `RS485_CMD_ADDR_SOC_VALUE14` |
| `0x221C` | `RS485_CMD_ADDR_SOC_VOLTAGE15` |
| `0x221D` | `RS485_CMD_ADDR_SOC_VALUE15` |
| `0x221E` | `RS485_CMD_ADDR_SOC_VOLTAGE16` |
| `0x221F` | `RS485_CMD_ADDR_SOC_VALUE16` |
| `0x2220` | `RS485_CMD_ADDR_SOC_VOLTAGE17` |
| `0x2221` | `RS485_CMD_ADDR_SOC_VALUE17` |
| `0x2222` | `RS485_CMD_ADDR_SOC_VOLTAGE18` |
| `0x2223` | `RS485_CMD_ADDR_SOC_VALUE18` |
| `0x2224` | `RS485_CMD_ADDR_SOC_VOLTAGE19` |
| `0x2225` | `RS485_CMD_ADDR_SOC_VALUE19` |
| `0x2226` | `RS485_CMD_ADDR_SOC_VOLTAGE20` |
| `0x2227` | `RS485_CMD_ADDR_SOC_VALUE20` |
| `0x2228` | `RS485_CMD_ADDR_SOC_VOLTAGE21` |
| `0x2229` | `RS485_CMD_ADDR_SOC_VALUE21` |
| `0x222A` | `RS485_CMD_ADDR_COPPERLOSS1` |
| `0x222B` | `RS485_CMD_ADDR_COPPERLOSS2` |
| `0x222C` | `RS485_CMD_ADDR_COPPERLOSS3` |
| `0x222D` | `RS485_CMD_ADDR_COPPERLOSS4` |
| `0x222E` | `RS485_CMD_ADDR_COPPERLOSS5` |
| `0x222F` | `RS485_CMD_ADDR_COPPERLOSS6` |
| `0x2230` | `RS485_CMD_ADDR_COPPERLOSS7` |
| `0x2231` | `RS485_CMD_ADDR_COPPERLOSS8` |
| `0x2232` | `RS485_CMD_ADDR_COPPERLOSS9` |
| `0x2233` | `RS485_CMD_ADDR_COPPERLOSS10` |
| `0x2234` | `RS485_CMD_ADDR_COPPERLOSS11` |
| `0x2235` | `RS485_CMD_ADDR_COPPERLOSS12` |
| `0x2236` | `RS485_CMD_ADDR_COPPERLOSS13` |
| `0x2237` | `RS485_CMD_ADDR_COPPERLOSS14` |
| `0x2238` | `RS485_CMD_ADDR_COPPERLOSS15` |
| `0x2239` | `RS485_CMD_ADDR_COPPERLOSS16` |
| `0x223A` | `RS485_CMD_ADDR_CELLNUM1` |
| `0x223B` | `RS485_CMD_ADDR_CELLNUM2` |
| `0x223C` | `RS485_CMD_ADDR_CELLNUM3` |
| `0x223D` | `RS485_CMD_ADDR_CELLNUM4` |
| `0x223E` | `RS485_CMD_ADDR_CELLNUM5` |
| `0x223F` | `RS485_CMD_ADDR_CELLNUM6` |
| `0x2240` | `RS485_CMD_ADDR_CELLNUM7` |
| `0x2241` | `RS485_CMD_ADDR_CELLNUM8` |
| `0x2242` | `RS485_CMD_ADDR_CELLNUM9` |
| `0x2243` | `RS485_CMD_ADDR_CELLNUM10` |
| `0x2244` | `RS485_CMD_ADDR_CELLNUM11` |
| `0x2245` | `RS485_CMD_ADDR_CELLNUM12` |
| `0x2246` | `RS485_CMD_ADDR_CELLNUM13` |
| `0x2247` | `RS485_CMD_ADDR_CELLNUM14` |
| `0x2248` | `RS485_CMD_ADDR_CELLNUM15` |
| `0x2249` | `RS485_CMD_ADDR_CELLNUM16` |
| `0x224A` | `RS485_CMD_ADDR_RTC_TIME_YEAR` |
| `0x224B` | `RS485_CMD_ADDR_RTC_TIME_MONTH` |
| `0x224C` | `RS485_CMD_ADDR_RTC_TIME_DAY` |
| `0x224D` | `RS485_CMD_ADDR_RTC_TIME_HOUR` |
| `0x224E` | `RS485_CMD_ADDR_RTC_TIME_MINUTE` |
| `0x224F` | `RS485_CMD_ADDR_RTC_TIME_SECOND` |
| `0x2250` | `RS485_CMD_ADDR_RTC_ALARM_YEAR` |
| `0x2251` | `RS485_CMD_ADDR_RTC_ALARM_MONTH` |
| `0x2252` | `RS485_CMD_ADDR_RTC_ALARM_DAY` |
| `0x2253` | `RS485_CMD_ADDR_RTC_ALARM_HOUR` |
| `0x2254` | `RS485_CMD_ADDR_RTC_ALARM_MINUTE` |
| `0x2255` | `RS485_CMD_ADDR_RTC_ALARM_SECOND` |
| `0x2300` | `RS485_CMD_ADDR_BALANCE_OV` |
| `0x2301` | `RS485_CMD_ADDR_BALANCE_OW` |
| `0x2302` | `RS485_CMD_ADDR_BALANCE_CW1` |
| `0x2303` | `RS485_CMD_ADDR_BALANCE_CW2` |
| `0x2304` | `RS485_CMD_ADDR_OPENTIME_ODD` |
| `0x2305` | `RS485_CMD_ADDR_OPENTIME_EVEN` |
| `0x2306` | `RS485_CMD_ADDR_OPENTIME_MOS` |
| `0x2307` | `RS485_CMD_ADDR_OPENTIME_RES` |
| `0x2308` | `RS485_CMD_ADDR_CS_CUR_CHGMAX` |
| `0x2309` | `RS485_CMD_ADDR_CS_CUR_DSGMAX` |
| `0x230A` | `RS485_CMD_ADDR_CBC_CUR_CHG` |
| `0x230B` | `RS485_CMD_ADDR_CBC_CUR_DSG` |
| `0x230C` | `RS485_CMD_ADDR_COOL_DSG_H` |
| `0x230D` | `RS485_CMD_ADDR_COOL_DSG_L` |
| `0x230E` | `RS485_CMD_ADDR_COOL_CHG_H` |
| `0x230F` | `RS485_CMD_ADDR_COOL_CHG_L` |
| `0x2310` | `RS485_CMD_ADDR_SLEEP_V_NORMAL` |
| `0x2311` | `RS485_CMD_ADDR_SLEEP_TIME_NORMAL` |
| `0x2312` | `RS485_CMD_ADDR_SLEEP_V_LOW` |
| `0x2313` | `RS485_CMD_ADDR_SLEEP_TIME_LOW` |
| `0x2314` | `RS485_CMD_ADDR_SLEEP_I_CHG` |
| `0x2315` | `RS485_CMD_ADDR_SLEEP_I_DSG` |
| `0x2316` | `RS485_CMD_ADDR_SLEEP_RES1` |
| `0x2317` | `RS485_CMD_ADDR_SLEEP_RES2` |
| `0x2318` | `RS485_CMD_ADDR_SOC_AH` |
| `0x2319` | `RS485_CMD_ADDR_SOC_CYCLE_TIME` |
| `0x231A` | `RS485_CMD_ADDR_SOC_RES1` |
| `0x231B` | `RS485_CMD_ADDR_SOC_RES2` |
| `0x231C` | `RS485_CMD_ADDR_SYS_SERIES_NUM` |
| `0x231D` | `RS485_CMD_ADDR_SYS_CS_RESIS` |
| `0x231E` | `RS485_CMD_ADDR_SYS_CS_NUM` |
| `0x231F` | `RS485_CMD_ADDR_SYS_RES1` |
| `0x2320` | `RS485_CMD_ADDR_HEAT_DSG_HIGH` |
| `0x2321` | `RS485_CMD_ADDR_HEAT_DSG_LOW` |
| `0x2322` | `RS485_CMD_ADDR_HEAT_CHG_HIGH` |
| `0x2323` | `RS485_CMD_ADDR_HEAT_CHG_LOW` |
| `0x2324` | `RS485_CMD_ADDR_HEAT_CUR_MAX` |
| `0x2325` | `RS485_CMD_ADDR_HEAT_CUR_MIN` |
| `0x2326` | `RS485_CMD_ADDR_HEAT_TIME_MAX` |
| `0x2327` | `RS485_CMD_ADDR_HEAT_RES1` |
| `0x2328` | `RS485_CMD_ADDR_HEAT_RES2` |
| `0x2329` | `RS485_CMD_ADDR_HEAT_RES3` |
| `0x232A` | `RS485_CMD_ADDR_HEAT_RES4` |
| `0x232B` | `RS485_CMD_ADDR_HEAT_RES5` |
| `0x232C` | `RS485_CMD_ADDR_HEAT_RES6` |
| `0x232D` | `RS485_CMD_ADDR_COOL_DSG_HIGH` |
| `0x232E` | `RS485_CMD_ADDR_COOL_DSG_LOW` |
| `0x232F` | `RS485_CMD_ADDR_COOL_CHG_HIGH` |
| `0x2330` | `RS485_CMD_ADDR_COOL_CHG_LOW` |
| `0x2331` | `RS485_CMD_ADDR_COOL_CUR_MAX` |
| `0x2332` | `RS485_CMD_ADDR_COOL_CUR_MIN` |
| `0x2333` | `RS485_CMD_ADDR_COOL_TIME_MAX` |
| `0x2334` | `RS485_CMD_ADDR_COOL_RES1` |
| `0x2335` | `RS485_CMD_ADDR_COOL_RES2` |
| `0x2336` | `RS485_CMD_ADDR_COOL_RES3` |
| `0x2337` | `RS485_CMD_ADDR_COOL_RES4` |
