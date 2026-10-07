# 启动、诊断与产品命名收口

实施基线 `dfe893f8db74709486974839220869a9fdf8a7c9`，需求为审核最新聊天方案并实施可行部分。

## 审核决定

- CI 源码当前上传整个 TC32 build root。收为 ELF、MAP、resources、输入/链接收据、build.log、static 报告；排除对象、LST 和 Cppcheck 缓存。上传失败仍失败，不以 continue-on-error 隐藏。
- 四产品 `BMS_PRODUCT_RELEASE_APPROVED` 全部保持 0，正式 production 镜像再叠加 D008/D013 专项批准；工程 link/resources 继续允许。禁止 EXTRA_DEFINES 绕过。
- 参数启动收为一个公共入口。保留原 Config→State→Event 验证顺序及其失败门禁，再加载软件保护；删除 app 中重复显式 State/Event init，不改变 reset 保持或跨域原子策略。
- 运行诊断共用入口，但 D008 新鲜度与 SH 原样本资格仍由原消费者判断。直接传本帧 raw/current/tick，不重新取一次 aux；DVC 主循环的 storage/parameters/AFE 诊断时点保留，避免停机时失去诊断或额外 AFE 访问。
- 产品宏和字段名称直接替换，不留 alias。D008 一个公共产品入口包含私有 DVC 默认文件；不改寄存器编码、工作时序、保护阈值、产品容量和通信名称。
- 字段重命名单独提交，类型/成员顺序/bit-field、65-word 软件保护、CFG2/State/Event 编码、wire 地址和 Flash/OTA 边界保持。

## 已执行与验证

首批：收口 CI 上传范围，四产品整体签核门及 EXTRA_DEFINES 防绕过；四产品生产配置/批准控制及工具检查 5 组通过。正式源批准值保持 0，测试的批准正控制只存在树外夹具。后续固定提交验证另列。

临时快照、对象、ELF/MAP、日志在 `%LOCALAPPDATA%/CodexTemp/bms-round5-20261007/`。继承上轮两处官方 SDK Patch_0001 覆盖，冻结实际字节哈希，本轮不修改/提交。没有镜像请求，使用 link/resources，不生成 BIN、烧录或 OTA。

第二批：app 只调用 bms_parameters_init；保留 Config→State→Event→保护加载顺序及启动失败持续门禁。诊断使用四参数共用入口，旧 wire 225 槽保持 0；清除重复声明。相关启动、存储、AFE 配置门禁、D008 调度、SH 电源和诊断共 17 组全部通过。新增真实生产 TU 夹具检查域失败、在线提交不能清门禁、同帧诊断与无额外 aux 读取。初跑夹具中的旧类型、旧模式及 SH inline stub 冲突均已修正后重跑通过。
