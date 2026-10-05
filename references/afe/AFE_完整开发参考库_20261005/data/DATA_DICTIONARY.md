# 结构化数据使用说明

## source_manifest.json

每个 source 保存 source_id、原文件名、精确修订、对应型号、PDF 页数、字节数、SHA-256、包内原件路径及逐页提取路径。source_id 是本资料包内部的稳定检索键，不是芯片型号或厂商文档编号。

## page_index.json 与 pages.jsonl

pdf_page 从 1 起算，独立于打印页码。pdf_page_label 是 PDF 内嵌标签，空字符串只表示 PDF 没提供可靠标签。character_count 为去除首尾空白后的提取字符数，不能衡量该页的技术信息量。extraction_status 为 no_text_detected 的页仍保留原 PDF，可由图像携带全部内容；不是删除页，也不是“无内容”。

pages.jsonl 额外含 text 原文；换行和空白用于尽量保留表格布局，不保证列语义。每个 PDF 的 source_id 与页码共同构成证据定位键。

## registers.json

每个芯片保留自己的 schema_version 和解释字段；不要用一个通用驱动直接消费所有文件。结构校验 schema 只验证基本形状、必要标识和来源存在，不认证电气事实或代码可用性。

- address 为字节地址；文件可能同时给出十进制与十六进制辅助字段
- name 可能为源文寄存器名，也可能按位域组合而成；以本文件说明为准
- width_bits 是条目宽度，不能据此推断一次 I2C/SPI 事务宽度或 ADC 有效位数
- revision 存在时必须参与键；SH36735XX 两版相同地址保留独立条目
- bitfields/decoded_fields/field_semantics 用于分别表达位图、组合字段及编码说明；若同时存在，均应读取
- reset、reset_value、reset_hex、reset_known_mask 等字段不是跨文件通用格式；未知或未指定为 null/明确状态，不能默认零
- access 除 R/RW 外，还可能含 W0C、W1C、RC、触发自清或混合访问；源表的 RW 与段落说明的实际副作用可能不同
- source/sources 给出原文件和 PDF 页码；数字值只有结合引用条件才可用于实现
- source_table_lines、source_bit_labels 等保存源表原始表达，可能保留厂商笔误；正常化处理必须在说明中可见
- reserved、reserved_or_undocumented 等字段为禁用/保留警示，不意味着允许尝试写入

本包故意保留各芯片的不同语义，避免把冲突、未知、保留位或读写副作用压平为一个看似统一但不安全的数组。

## register_index.json

这是入口目录：文件路径、内容 SHA-256、条目数和按版本计数。条目数包括文件有意列出的保留/未说明地址，也可能因多个版本重复同地址；不能当成功能寄存器数量、硬件容量或支持通道数。

## coverage.json

每个芯片文件声明自己的覆盖方式。覆盖表示页面已保留、已定位到整理章节或已明确作为原图/附录保留，不表示每一个机械尺寸、曲线采样点、图中导线均已重新录入结构化数据。原 PDF 在这些情况仍是权威证据。
