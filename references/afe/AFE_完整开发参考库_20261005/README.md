# AFE 官方手册开发参考库

本资料包把已提供的 6 份官方 PDF 整理为中文开发参考、可检索逐页原文、寄存器结构化数据与原件证据。范围为 SH367309、SH36735XX、DVC1124-2、BQ76920/30/40；汇总保留原文的不确定性，不能替代对应版本的原 PDF 和实板验证。

## 建议阅读顺序

1. 先阅读 [AI 使用约定](AI_CONTEXT.md)，确定准确芯片、手册版本、硬件条件与问题范围
2. 选择对应芯片参考：
   - [SH367309 V1.1](chips/sh367309/reference.md)
   - [SH36735XX CV1.0A](chips/sh36735xx/reference.md)，并阅读 [CV0.2C 与 CV1.0A 差异](chips/sh36735xx/version_differences.md)
   - [DVC1124-2 数据手册 V1.1 与参考手册 V1.2](chips/dvc1124_2/reference.md)
   - [BQ76920 BQ76930 BQ76940 SLUSBK2F](chips/bq769x0/reference.md)
3. 根据问题查寄存器 JSON、逐页原文及页码链接；任何写入、换算或安全关键数值都回到原 PDF 核对
4. 实现前使用 [驱动核对清单](DRIVER_REVIEW_CHECKLIST.md)，跨芯片迁移前使用 [差异索引](CROSS_CHIP_GUIDE.md)
5. 使用 [质量与覆盖说明](QUALITY_REPORT.md) 了解提取、视觉核对与尚未确认的边界

## 文件层级

- `sources/`：原始 PDF，原文件名与字节内容保留
- `chips/<family>/reference.md`：按开发任务组织的中文说明
- `chips/<family>/registers.json`：按该芯片原生结构整理的寄存器及位域；不同芯片结构不强行统一
- `chips/<family>/coverage.json`：源章节或页码到整理结果的对应关系
- `chips/<family>/validation.md`：图表核对范围、源文件疑点及限制
- `extracted/<source_id>/page-NNN.txt`：每一 PDF 页的原始布局文本
- `extracted/<source_id>/full_by_page.md`：带页码及 PDF 链接的完整原文提取
- `extracted/<source_id>/pages.jsonl`：逐页检索数据，每行是一个独立 JSON 对象
- `data/source_manifest.json`：精确来源、版本、页数、SHA-256
- `data/page_index.json`：全量页面索引及提取状态
- `data/coverage_index.json`：6份来源的328页覆盖检查
- `data/validation_summary.json`：验证范围和已知边界
- `data/register_index.json`：各芯片寄存器数据文件的入口与统计
- `tools/search_reference.py`：纯标准库本地关键词检索
- `tools/verify_pack.py`：文件哈希、页码、JSON 与链接检查

## 版本与命名

“309”在本包中指 SH367309。“3520”按收到的 SH36735XX 家族手册处理，不能把家族内其他型号的所有通道和引脚能力自动套用到 SH3673520。“DVC”指 DVC1124-2；手册中简写 DVC1124，不表示所有 DVC1124 版本均兼容。“BQ”仅覆盖本次上传的 BQ76940.pdf 内明确列出的 BQ76920、BQ76930、BQ76940。

没有 BQ76942、BQ76952 或 BQ33100 的独立原始手册，本包不补写这些器件的寄存器或行为。收到新修订后，应作为新的来源加入并重新对比，不能改掉旧来源记录后继续沿用旧结论。

## 证据与风险边界

正文使用“文件名 + PDF 页码”的引用；PDF 页码从文件第一张页面起算。若印刷页码不同，在相应芯片资料中注明。所有引用都针对随包保存的原件，不代表截至今天最新的厂家修订。

原文提取可能丢失图形、连线、公式、符号、上标及表格列关系；这是检索支持层。结构化数据中的 `null`、`not_specified`、`unverified` 或类似字段表示尚未得到可靠值，绝不表示零、默认值、关闭或可忽略。保留位、读清/写清行为和具有副作用的寄存器必须按对应芯片逐项处理。

示例参数、源文件默认值及典型应用均不是用户产品量产配置。芯片手册不能证明实际 PCB、分流器、电芯、MOS、NTC、焊接装配、Flash 参数或既有驱动已正确匹配。本包没有修改仓库、驱动或任何量产参数。

## 本地检索示例

```sh
python3 tools/search_reference.py --chip sh367309 BFLAG
python3 tools/search_reference.py --source SH36735XX_CV1_0A CRC
python3 tools/search_reference.py --source DVC1124_2_RM_V1_2 CC1
python3 tools/search_reference.py --source BQ769x0_SLUSBK2F OV_TRIP
python3 tools/verify_pack.py
```

本包离线可用，不需要账号或网络。中文概念和英文寄存器名可分别搜索。检索默认返回命中行及少量上下文，完整证据须打开被引用的页面。
