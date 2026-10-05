# 持久化 / OTA 补审覆盖与验证边界

基线 HEAD `0aaa842938c37b0016a23d8a29ce2aa416ca2c4d` clean 提交快照，内容 SHA256 `05fb3147811468a347d3b27538b364d6ce92d56c533a17d713bcc8e1ac689613`。只写外部审查目录；资料库原件哈希未改变。

| 范围 | 实际审查 / 验证 | 未包含 / 不可声称 |

|---|---|---|

| 四产品源文件归属 | 四份sources.txt包含Config/State/Event/param/OTA/Flash/journal/AFE事务/guard；九目标编号均1，持久tag8/11/13/14 | 本子审不代替父审六目标真实链接 / MAP |

| Config加载/default/policy | 原源码全部加载、编码、保存、各group和startup授权路径检查；322byte schema2 | 默认阈值正确性由原厂资料对应审核，host default API只是当前编译默认来源 |

| OTA选择覆盖 | D014 22个原源码TU，64类别掩码，64次重复boot；六组至少一个差异字段，整payload独立对比保留/重置 | 不注入State/Event更新编号；不代表OTA全链 / Bootloader镜像验收 |

| 编程中断 | 同22TU，所有组更新，cut=0..356共357个编程字节边界，每例第二个进程重启；CRC/标记只接受旧或完整新记录，失败启动不授权 | RAM NOR模型无部分擦除/欠压、真实掉电/MCU reset；非在线AFE跨介质事务原子性 |

| 在线AFE事务 | D01422TU完整backend/control/register编解码+profile/guard/Config；有效授权，OVL回读错误，失败关闭、rollback失败及随后健康采样；业务反例exit1 | SPI peer不模拟芯片电气；DSGMOS ON命令不是实际Gate/Vgs；D008/D011/D013同路径仅源码支持 |

| 宏模式 | online开发host与BMS_PRODUCTION_BUILD=1宏host同反例；clean原目录身份、非零buildID、dirty0，无身份override | host不等于TC32 production link；本子审host未编译SDK库/app main |

| 原厂寄存器意义 | 原始SH36735XX CV1.0A PDF p34、p36文字及实际渲染视觉复核，确认SCONF6禁用语义和OVcode×5mV | 未给原厂文档臆造journal/MCU锁存/在线commit成功定义 |

| 校准 | 当前持久offset/gain编解码、范围、getter缓存、授权路径；64组合实证CALIBRATION保持/更新 | 不把±1000A和0.1..10倍代码合法范围当工厂签核值；电流准确性/bootzero完整计算由父审 |

| reset fault owner | SW fault、guard、SH本地fault及State/Event字段源码检查，cold RAM与历史事件严格区分 | 当前真实MCU reset / AFE保持供电时故障恢复不由本子审实板证明；产品reset政策Unknown |

| Flash/OTA平台 | 原源码OTA写互锁、write/readverify/eraseverify、4096erase/256page和512K地址检查 | 平台接口替身未编入该22TU host；没有真实Flash锁与SDK OTA运行证明 |

## Oracle 来源

- ST-AFE-01 的原厂物理含义：PDF p34、p36；保持fail-safe要求来自用户提供的D014 AGENTS。具体锁存接口与修复方式是建议，原厂未定义软件事务。

- OTA六组期望：`docs/OTA_PARAMETERS.md`明确“相等保留 / 不等恢复该组默认 / 失败启动阻断 / 重启重试”。独立Python字段span与zlibCRC判journal结果；不读取现有测试断言作为正确答案。

- 字节布局取自当前schema以定位opaque payload，默认payload由public编译默认API取得；这些机制信息不是保护阈值正确性oracle。

- coldreset锁存、在线persist-before-apply掉电、会话单次消费、允许disable profile的工程签核：产品政策Unknown。不得默认安全或擅自选策略。

## 可重现证据

- `online_transaction_probe.py/.c`：仓库fixture只复用端口环境，删除其main、scenario和断言；22原TU成员均由sources.txt检查；源码SHA与命令在compile JSON。

- `probe_results.json`：最初development宏host反例。

- `probe_results-production.json`：production宏host反例。`probe_compile-production.json`明确actual_dirty=false和analysis_only_identity_override=false；该身份基于原目录clean提交，不以冻结副本的Git换行状态冒充原目录身份。业务runtime exit=1，不是crash/compile失败。脚本exit0仅表明已保存反例，不是安全通过。

- `ota_selection_probe.py/.c`、`ota_selection_results.json`：64掩码、64repeat no-write、357cut/reboot详细逐项结果；原源码TU不修改。宏为development。

- `source_evidence.json`：当前关键文件hash、真实行号；`effective_storage_policy.json`：四产品编号和存储策略摘要。

- `SH3510-p034.png / SH3510-p036.png`：本次实际已视觉检查。

未进行仿真器指令级验证、实板、HIL、BIN生成、烧录、协议修改、正式源修改、提交/推送/合并。资料库已有3286字段检查保留，不能把本次源验证并入原厂资料准确性检查数量。
