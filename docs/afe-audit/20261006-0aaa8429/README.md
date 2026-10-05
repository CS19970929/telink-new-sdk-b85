# AFE 原厂文档与四产品审查归档

日期：2026-10-06。审查基线：`0aaa842938c37b0016a23d8a29ce2aa416ca2c4d`，分支 `codex-bms-monorepo`。这是固定提交记录；加入文档不重新定义审查基线，也不证明后续修改已通过。

- [审查报告](审查报告.md)：风险顺序、原页、代码位置、结论与验证边界。
- [四产品有效 AFE 配置](四产品有效AFE配置.md)：编译输入、默认、持久化、运行 overlay 和实际量化。
- [资料库更正与补充](资料库更正与补充清单.md)：确定转写错误、原文矛盾与资料缺口。
- 分项：[D008/DVC](dvc_audit/findings.md)、[SH 三产品](sh_audit/findings.md)、[参数存储与 OTA](storage_audit/findings.md)、[资料库](library_audit/findings.md)。各目录 coverage.md 保留覆盖与缺口。
- [验证收据说明](verification/README.md)、[现有回归](verification/host/report.md)、[正常生产构建命令](verification/normal_production_commands.json)。独立反例与原始退出码保留在相应 JSON。
- [源码身份](local_snapshot.json)、[资料身份](library_audit/material_manifest.json)、[归档清单](archive_manifest.json)、[归档哈希](CHECKSUMS.sha256)。

本目录只包含正式报告、分项结论、原始验证收据和身份清单。没有复制业务源码或构建产物；仅保留被结论直接引用的原页图片作为正式证据。原资料库已另存项目 `references/afe/`，它是正式版本化开发参考资料。166 组既有回归及六配置生产 ELF/MAP/资源检查通过，独立反例仍成立；仿真/HIL/实板未执行，修复未实施。

## 完整材料与可重现证据

六原厂 PDF 与全部资料库内容已原样保存于 [项目资料库](../../../references/afe/README.md)。当时精确源码字节、分析脚本、预处理文件、主机工具输出与 ELF/MAP 等完整材料同时保存在外部正式证据包。下列本机绝对链接需要访问对应文件；仅克隆 Git 仓库不会获得这些外部材料。移交时应一并传递证据包，并核对哈希。报告内源码链接指向冻结快照，避免将未来工作区行号当作已审核状态。

- [完整证据包](<C:/Users/Administrator/Documents/CodexOutputs/telink-new-sdk-b85/afe-factory-audit-local-20261006-0aaa8429/evidence.zip>)
- [外部完整交付目录哈希](<C:/Users/Administrator/Documents/CodexOutputs/telink-new-sdk-b85/afe-factory-audit-local-20261006-0aaa8429/CHECKSUMS.sha256>)
- [冻结源码包](<C:/Users/Administrator/Documents/CodexOutputs/telink-new-sdk-b85/afe-factory-audit-local-20261006-0aaa8429/source-snapshot-0aaa8429.zip>)

完整证据包 SHA256：`30c46e97239b194a7427c0359c9f6b42b4583a3c9c89b82fdb274d98bd6f8474`；大小：39720188 B。包内原始日志/JSON的绝对路径保留历史运行位置，不代表本目录可直接重跑。

原 ZIP 临时预览文件在最终归档时已不可访问；首次读取的原 ZIP 身份及完整解压内容/六 PDF 哈希均已保留。见 [原压缩包可用性记录](original_zip_availability.json)。

## 项目归档后的文档维护

2026-10-06 导入原资料库并更新开发导航；`docs/D008_PRODUCT_REFERENCE.md` 已将“0 mV 为官方断线判据”更正为项目判据。这是文档归因修正，审查报告中的 DVC-06 描述的是固定提交 `0aaa8429` 当时状态。驱动、保护参数、协议和存储布局未修改；其他固件问题与实板待验证项保持未关闭。导入完整性与链接验证见 [资料导入收据](../../../references/afe/import_validation.json)。
