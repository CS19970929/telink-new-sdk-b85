# AFE 修复验证归档

日期：2026-10-06；实际被验证的clean代码提交：`5418a9faf9b64ad1ae137d815e90525aeb9791f6`，分支 `codex-bms-monorepo`。

- [修复记录与原文页码](../../AFE_AUDIT_REMEDIATION.md)：各发现、入口、规则、资源与发布缺口。
- [修复后四产品配置](CURRENT_AFE_CONFIG.md)：当前默认、原厂nominal、编译输入及运行覆盖；严格区分持久值/实机。
- [主机报告](host/report.md) / [机器收据](host/report.json)：178组、零失败、完整范围、输入指纹及证据等级。未启用ASan/UBSan，主机夹具不能证明实板。
- [六配置汇总](production_summary.json) / [生产命令](production/commands.json)：TC32生产模式，core `-Os` / AFE与平台 `-O2`，实际输入和link-completed收据在各配置目录。ELF/MAP/LST哈希对应外部完整产物，无BIN。
- [兼容与原件完整性](compatibility_and_library_integrity.json)：388个原资料文件逐hash相同；产品配置、默认参数、参数存储和写入入口等指定文件逐字节对比基线一致。这不能证明设备持久值或断电一致性。
- [验证身份](identity.json)、[历史失败](previous-failures/summary.json)：保留未通过轮次及资源失败，不覆盖为通过。
- [减少内联的函数体等价记录](resource-analysis/body-equivalence.json)：仅增加声明、未改函数体；组合实验是混合对象试验，其结论由最终clean构建确认。实验记录的bin_size是MAP符号，不是生成了BIN。

`reproduction/build_six_confirmed.py` 是本轮实际外部运行脚本的归档。复现时先复制到用户临时目录，再修改root/工具路径；它在自身目录下生成输出，禁止在项目中直接执行。主机复现：在项目根设置CC与PATH后，`python -B tests/run_host_regression.py --output <源码树外的空目录>`。生产复现使用独立外部 `BMS_BUILD_ROOT`，依次运行各产品/profile的 `link --jobs 4` 与 `resources`，不要执行需要BIN的构建/打包命令。

历史保留项：首次178组4个夹具适配失败；第二次178组2个抽取失败；初始代码提交1804e235虽六配置链接，D008资源余量7148B未过门槛。还有一次内联执行脚本时__file__错误导致输出误落项目根并触发构建身份/clean检查失败，已原样移至用户临时区，正式目录仅保留失败日志；这次不计入成功验证。最后的178组与12条生产命令来自本页固定clean提交。

用户决定：现有reset/watchdog/OTA启动行为保留且作为发布阻断；旧默认与持久参数不重置，新完整写入严格校验。原LSH-02、板级阈值、ADC时序、MOS Gate、Flash掉电、OTA及其他硬件缺口保持未签核。没有硬件仿真/HIL/实板通过、烧录、OTA、merge或push。

外部完整ELF/MAP/LST与本目录收据在 `C:\Users\Administrator\Documents\CodexOutputs\telink-new-sdk-b85\afe-remediation-20261006-5418a9fa`，另有evidence.zip及SHA256。克隆项目可获得原厂资料、源代码、测试和正式收据；外部编译产物移交须核对归档哈希。
