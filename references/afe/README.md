# AFE 原厂资料库与项目使用入口

资料版本：用户提供的 `AFE_完整开发参考库_20261005.zip`。本目录的 [原资料库](AFE_完整开发参考库_20261005/README.md) 原样保存原始 PDF、提取文本、中文参考、结构化数据、质量记录与工具。六份 PDF 共 328 页；387 条原包 checksum 已重新核对。原包现存文件全部逐字节匹配，导入身份见 [import_manifest.json](import_manifest.json)。

开发先读 [项目使用指南](../../docs/AFE_REFERENCE_GUIDE.md) 和 [资料库更正与补充](../../docs/afe-audit/20261006-0aaa8429/资料库更正与补充清单.md)，再沿原库入口查原 PDF。已确认 BQ 中文 SHIP 顺序有转写错误，原包保持不变；采用原文的 `00→01→10`，不采用中文错误的 `00→01→01→10`。BQ 未参与本次四产品编译。其他未决项按更正清单逐项保留。

## 直接入口

|材料|项目路径|
|---|---|
|原始 PDF|[sources](AFE_完整开发参考库_20261005/sources/)|
|来源、版本、SHA256|[来源清单](AFE_完整开发参考库_20261005/SOURCE_MANIFEST.md)、[结构化清单](AFE_完整开发参考库_20261005/data/source_manifest.json)|
|DVC1124-2|[中文检索参考](AFE_完整开发参考库_20261005/chips/dvc1124_2/reference.md)|
|SH36735XX|[中文检索参考](AFE_完整开发参考库_20261005/chips/sh36735xx/reference.md)、[版本差异](AFE_完整开发参考库_20261005/chips/sh36735xx/version_differences.md)|
|SH367309 / BQ769x0|[SH309](AFE_完整开发参考库_20261005/chips/sh367309/reference.md)、[BQ](AFE_完整开发参考库_20261005/chips/bq769x0/reference.md)，仅作参考材料，不等于四产品当前后端|
|原包使用约定|[AI_CONTEXT.md](AFE_完整开发参考库_20261005/AI_CONTEXT.md)|
|固定提交审查|[2026-10-06 / 0aaa8429](../../docs/afe-audit/20261006-0aaa8429/README.md)|

这是正式项目资料目录，不是临时解压区。原 PDF 和原包文件按版本保留；更新原厂手册应另建版本目录，记录来源/hash/适用型号和差异。缓存、渲染中间文件、构建输出继续放用户临时区。新修正以独立更正记录先保留，不覆盖旧版本的来源证据。
