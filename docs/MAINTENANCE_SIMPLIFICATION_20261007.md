# 启动、诊断与产品命名收口

实施基线 `dfe893f8db74709486974839220869a9fdf8a7c9`，需求为审核最新聊天方案并实施可行部分。

最终代码与验证提交 `d6979d9e5de080122619c60afb2b9dc908424a59`：Windows host **110/110**；WSL ASan/UBSan **41/41**；四开发、六生产 ELF/MAP/resources 均通过，**0 编译 warning**。明细见 [固定提交证据](MAINTENANCE_SIMPLIFICATION_20261007_EVIDENCE.json)。

## 审核决定

- CI 源码当前上传整个 TC32 build root。收为 ELF、MAP、resources、输入/链接收据、build.log、static 报告；排除对象、LST 和 Cppcheck 缓存。上传失败仍失败，不以 continue-on-error 隐藏。
- 四产品 `BMS_PRODUCT_RELEASE_APPROVED` 全部保持 0，正式 production 镜像再叠加 D008/D013 专项批准；工程 link/resources 继续允许。禁止 EXTRA_DEFINES 绕过。
- 参数启动收为一个公共入口。Config→State→Event 各尝试初始化一次，即使前域失败仍加载其他健康域；整体失败门禁保持，再加载软件保护；删除 app 中重复显式 State/Event init，不改变 reset 保持或跨域原子策略。
- 运行诊断共用入口，但 D008 新鲜度与 SH 原样本资格仍由原消费者判断。直接传本帧 raw/current/tick，不重新取一次 aux；DVC 主循环的 storage/parameters/AFE 诊断时点保留，避免停机时失去诊断或额外 AFE 访问。
- 产品宏和字段名称直接替换，不留 alias。D008 一个公共产品入口包含私有 DVC 默认文件；不改寄存器编码、工作时序、保护阈值、产品容量和通信名称。
- 字段重命名单独提交，类型/成员顺序/bit-field、65-word 软件保护、CFG2/State/Event 编码、wire 地址和 Flash/OTA 边界保持。

## 已执行与验证

首批：收口 CI 上传范围，四产品整体签核门及 EXTRA_DEFINES 防绕过；四产品生产配置/批准控制及工具检查 5 组通过。正式源批准值保持 0，测试的批准正控制只存在树外夹具。后续固定提交验证另列。

临时快照、对象、ELF/MAP、日志在 `%LOCALAPPDATA%/CodexTemp/bms-round5-20261007/`。继承上轮两处官方 SDK Patch_0001 覆盖，冻结实际字节哈希，本轮不修改/提交。没有镜像请求，使用 link/resources，不生成 BIN、烧录或 OTA。

第二批：app 只调用 bms_parameters_init；保留 Config→State→Event→保护加载顺序及启动失败持续门禁。诊断使用四参数共用入口，旧 wire 225 槽保持 0；清除重复声明。相关启动、存储、AFE 配置门禁、D008 调度、SH 电源和诊断共 17 组全部通过。新增真实生产 TU 夹具检查域失败、在线提交不能清门禁、同帧诊断与无额外 aux 读取。初跑夹具中的旧类型、旧模式及 SH inline stub 冲突均已修正后重跑通过。

第三批：产品旧宏直接换为 BMS_PRODUCT_* 与具单位的 BMS_SLEEP_*；通信能力采用显式 0/1。D008 公共入口包含私有 dvc1124_product_defaults.h，原默认数值逐行保留。四产品 TC32 预处理比较 488 项原产品/板级/后端宏，归一改名后 0 差异；99 项关键活跃源码/函数体 0 差异。产品默认、核心契约和 production 批准正负控制 12 组通过。

第四批：133 个旧成员名直接替换为描述业务与单位的名称，生产代码仅机械改名与定义注释/对齐，不改表达式。TC32 四产品各 104 项 sizeof/offset 和 38 个位域常量对象一致；18 个修改生产文件归一改名后源码一致。六装配的串数、容量、65 项软件保护及 35 项 AFE 默认数值一致。核心、诊断、Modbus、默认配置 16 组、保护场景 4 组和 D008 配置/后端 2 组通过。对应源码解析/拼接名夹具同步名称，断言保持。后续统一固定提交执行完整 host/sanitizers 与四开发、六生产 link/resources。

异常路径补查：原 app 重复 State init 还保证 Config 失败后读取健康 State，初次集中入口的早返回遗漏了这一点。最终入口依次尝试三个独立域，再按首个失败保持诊断和输出禁止；不会因 Config 失败让 SOC 启动读取默认循环次数。增加仅 Config 区读失败的真实 Config/State/Event/journal 场景：State 保持 SOC=63、放电累计=27、循环=123，三个域各尝试一次，在线保护提交仍不能清门禁；相关核心、存储与 AFE 门禁 9 组通过。最终增加四产品 core_contract 的 sanitizer 覆盖，共 41 组。

## 最终固定提交验证

三个持久域逐一初始化一次，然后整体判定输出资格；Config 区读失败时，健康 State 的 SOC/放电累计/循环缓存仍加载。首个失败诊断及在线提交不能解除启动门禁的规则保持。

| 项目 | 结果 |
|---|---|
| Windows 完整 host | 110/110，0 失败，源指纹与 HEAD 全程稳定 |
| WSL ASan/UBSan | 41/41，增加四产品 core_contract 中的生产启动 TU 夹具 |
| TC32 | 四开发、六生产 link/resources，0 编译 warning，未生成 BIN |
| 产品/板级/AFE 输入 | 500 项宏归一改名后无差异，包含版本与序列号名称 |
| 六装配默认 | 串数、容量、全部 65 项软件保护及 35 项 AFE 默认数值无变化 |
| TC32 数据布局 | 四产品各 104 项大小/偏移、38 个位域对象不变 |
| 关键源码 | 99 项活跃代码/函数体归一改名后无差异；启动/诊断变动另有上述场景验证 |
| sources.txt | 四产品编译成员与顺序不变，sources --check 通过 |

| 产品/装配 | 开发 Flash free B | production Flash free B |
|---|---:|---:|
| D008 16S LFP | 7788 | 12332 |
| D008 20S NMC | — | 12332 |
| D008 24S LFP | — | 12348 |
| D011 | 11468 | 16108 |
| D013 | 12460 | 17148 |
| D014 | 11644 | 16300 |

D008 production 达到 12 KiB 目标，最小仅高出 44 B；8 KiB 硬门不变。开发配置仍有低于 8 KiB 的预算告警。资源结果随此固定配置记录，不当成今后提交的保证。

CI 上传白名单在本机十配置中选出 60 个构建证据文件，另保留 static 报告并排除缓存；没有把上传失败设为成功。本轮未执行远端 CI，未证明 ECONNRESET 已消失。

四产品整体批准与 D008/D013 专项批准仍为 0。工程验证通过不等于量产批准；没有烧录、实板、掉电、Sleep/Wake 或 OTA 验收。原两处官方 SDK Patch_0001 实际字节覆盖未改动/提交，完整哈希与源指纹保存在证据 JSON；结论对应固定代码提交加这些 SDK 实际输入。
