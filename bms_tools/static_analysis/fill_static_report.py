#!/usr/bin/env python3
"""Fill the BMS static-analysis workbook using artifact-tool.

The supplied template is always imported read-only and the completed workbook is
exported to a separate path.  This script intentionally contains no Cppcheck
parsing logic; bms.py provides a normalized, auditable JSON data contract.
"""

from __future__ import annotations

import json
import sys
from datetime import datetime
from pathlib import Path
from typing import Any

from artifact_tool_v2 import Blob, SpreadsheetFile


COLORS = {
    "navy": "#17365D",
    "blue": "#D9EAF7",
    "white": "#FFFFFF",
    "red": "#F4CCCC",
    "yellow": "#FFF2CC",
    "green": "#D9EAD3",
    "border": "#7F8C8D",
}


def put(sheet: Any, address: str, value: Any) -> None:
    sheet.get_range(address).values = [[value]]


def style_title(cell_range: Any) -> None:
    cell_range.format = {
        "fill": COLORS["navy"],
        "font": {"bold": True, "color": COLORS["white"], "size": 14},
        "vertical_alignment": "center",
    }
    cell_range.format.row_height = 28


def style_header(cell_range: Any) -> None:
    cell_range.format = {
        "fill": COLORS["blue"],
        "font": {"bold": True, "color": "#000000"},
        "horizontal_alignment": "center",
        "vertical_alignment": "center",
        "wrap_text": True,
        "borders": {"preset": "all", "style": "thin", "color": COLORS["border"]},
    }
    cell_range.format.row_height = 32


def style_body(cell_range: Any) -> None:
    cell_range.format = {
        "vertical_alignment": "top",
        "wrap_text": True,
        "borders": {
            "inside_horizontal": {"style": "thin", "color": "#D9D9D9"},
            "bottom": {"style": "thin", "color": "#B7B7B7"},
        },
    }


def main() -> int:
    if len(sys.argv) != 5:
        raise SystemExit(
            "usage: fill_static_report.py <template.xlsx> <data.json> "
            "<output.xlsx> <preview-dir>"
        )
    template_path, data_path, output_path, preview_dir = map(Path, sys.argv[1:])
    data = json.loads(data_path.read_text(encoding="utf-8"))
    workbook = SpreadsheetFile.import_xlsx(Blob.load(template_path))
    for sheet_name in ("分析明细", "范围审计", "统计汇总", "Deviation候选"):
        workbook.worksheets.add(sheet_name)
    # Persist the new sheet topology before installing cross-sheet formulas so
    # every worksheet has a stable sheetId in the calculation engine.
    workbook = SpreadsheetFile.import_xlsx(SpreadsheetFile.export_xlsx(workbook))
    run_date = datetime.fromisoformat(data["finished_at"].replace("Z", "+00:00"))
    commit = data.get("git", {}).get("commit", "unknown")
    short_commit = commit if commit == "unknown" else commit[:12]
    findings = data.get("findings", [])
    scope = data.get("scope", [])
    candidates = data.get("deviation_candidates", [])
    row_end = max(2, len(findings) + 1)

    cover = workbook.worksheets.get_item("Cover")
    put(cover, "H3", "V1.00")
    for row in range(21, 28):
        cover.get_range(f"E{row}:H{row}").merge()
    put(cover, "E21", "TLSR8251 BMS（ble_sample）")
    for row in range(22, 26):
        put(cover, f"E{row}", "")
    put(cover, "E26", run_date)
    cover.get_range("E26").format.number_format = "yyyy-mm-dd"
    put(cover, "E27", f"Git {short_commit}{' + dirty' if data.get('git', {}).get('dirty') else ''}")

    history = workbook.worksheets.get_item("ChangeHistory")
    for address, value in (("B6", "V1.00"), ("C6", "Draft"), ("D6", ""),
                           ("E6", run_date), ("F6", ""), ("G6", ""),
                           ("H6", "自动生成 Cppcheck 静态分析结果及范围审计；待人工审核/批准")):
        put(history, address, value)
    history.get_range("E6").format.number_format = "yyyy-mm-dd"
    history.get_range("H:H").format.column_width = 42
    history.get_range("6:6").format.row_height = 44

    general = workbook.worksheets.get_item("General")
    app_headers = sum(
        1 for item in scope
        if item.get("owner") == "BMS应用层" and item.get("kind") == "头文件"
        and item.get("cppcheck_mode") == "随翻译单元解析"
    )
    scope_summary = [
        ("vendor/ble_sample/*.c（应用层翻译单元）",
         f"{data['project_c_translation_unit_count']} files @ {short_commit}"),
        ("应用层头文件（随真实编译依赖解析）", f"{app_headers} files"),
        ("官方 SDK/工具链依赖",
         f"仅解析 {data['sdk_parse_only_header_count']} 个头文件；诊断不纳入检查结果"),
        ("完整构建输入清单", f"{data['actual_build_source_count']} sources（见“范围审计”）"),
        ("范围排除证据", "sdk_scope_exclusions.json / cppcheck-sdk-scope-exclusions.txt"),
    ]
    general.get_range("B6:E14").clear({"apply_to": "contents"})
    for index, (label, value) in enumerate(scope_summary, start=6):
        put(general, f"B{index}", label)
        put(general, f"E{index}", value)
    put(general, "B20", "Cppcheck")
    put(general, "E20", data["cppcheck_version"])
    put(general, "B21", "Telink tc32-elf-gcc（配置提取）")
    put(general, "E21", data["configuration"]["compiler_version"])
    put(general, "A24", "配置由 build.mk + source_order.txt 经 make -B -n 自动提取；Cppcheck 使用筛选后的 compile_commands.json、TC32 数据模型和真实编译器条件宏。")
    put(general, "A25", f"规则：仅统计 vendor/ble_sample 应用层；应用层不启用 suppression。SDK 头文件仅为真实编译配置解析，其诊断按范围策略排除。MISRA：{data['misra']['coverage']}")
    general.get_range("E29").formulas = [[f"=COUNTA('分析明细'!$A$2:$A${row_end})"]]
    if data["misra"]["executed"]:
        general.get_range("E30").formulas = [[f'=COUNTIF(\'分析明细\'!$I$2:$I${row_end},"<>")']]
    else:
        put(general, "E30", "未执行（MISRA addon 未安装）")
    general.get_range("E31").formulas = [[f'=COUNTIF(\'分析明细\'!$K$2:$K${row_end},"已批准")']]
    blocking = data.get("severity_counts", {}).get("error", 0) + data.get("severity_counts", {}).get("warning", 0)
    put(general, "E35", "需整改" if blocking else ("需评审" if findings else "通过"))
    gap_text = "未发现应用层漏扫。" if data["coverage_gap_count"] == 0 else f"存在 {data['coverage_gap_count']} 个覆盖缺口，须先处理。"
    put(general, "E36", f"本次仅对应用层发现 {len(findings)} 个去重问题（原始 {data['raw_native_occurrence_count']} 条）；SDK问题不检查、不统计。{blocking} 个 error/warning 建议优先整改。{gap_text} Reviewer、Approver 及 Deviation 批准信息保留人工填写。")
    general.get_range("E:E").format.column_width = 42
    general.get_range("6:10").format.row_height = 34
    general.get_range("21:21").format.row_height = 38
    general.get_range("36:36").format.row_height = 76
    general.get_range("E6:E36").format.wrap_text = True

    detail = workbook.worksheets.get_item("分析明细")
    detail.show_grid_lines = False
    detail.freeze_panes.freeze_rows(1)
    detail_headers = ["序号", "文件", "函数", "行号", "Cppcheck ID", "严重程度", "问题描述", "CWE", "MISRA Rule", "分类", "当前状态", "处理建议", "分析单元", "重复次数", "不确定性"]
    detail.get_range("A1:O1").values = [detail_headers]
    style_header(detail.get_range("A1:O1"))
    if findings:
        rows = [[
            index, item["file"], item.get("function", ""), item["line"], item["id"],
            item["severity"], item["message"], item.get("cwe", ""),
            item.get("misra_rule", ""), item["classification"], item["status"],
            item["recommendation"], "; ".join(item.get("analysis_units", [])),
            item["occurrence_count"], "inconclusive" if item.get("inconclusive") else "",
        ] for index, item in enumerate(findings, start=1)]
        detail.get_range_by_indexes(1, 0, len(rows), len(detail_headers)).values = rows
        style_body(detail.get_range(f"A2:O{len(rows) + 1}"))
        status_range = detail.get_range(f"K2:K{len(rows) + 1}")
        status_range.data_validation = {"rule": {"type": "list", "values": [
            "待整改", "待评审", "待评审（SDK）", "待修正配置", "已整改",
            "误报确认", "Deviation候选（未批准）", "已批准",
        ]}}
        status_range.conditional_formats.add("containsText", {"text": "待整改", "format": {"fill": COLORS["red"], "font": {"color": "#9C0006"}}})
        status_range.conditional_formats.add("containsText", {"text": "Deviation", "format": {"fill": COLORS["yellow"], "font": {"color": "#9C6500"}}})
        status_range.conditional_formats.add("containsText", {"text": "已整改", "format": {"fill": COLORS["green"], "font": {"color": "#006100"}}})
        detail.tables.add(f"A1:O{len(rows) + 1}", True, "StaticFindingsTable").style = "TableStyleMedium2"
    for columns, width in (("A:A", 7), ("B:B", 42), ("C:C", 24), ("D:D", 9),
                           ("E:E", 24), ("F:F", 12), ("G:G", 54), ("H:I", 12),
                           ("J:K", 22), ("L:L", 46), ("M:M", 42), ("N:O", 12)):
        detail.get_range(columns).format.column_width = width

    scope_sheet = workbook.worksheets.get_item("范围审计")
    scope_sheet.show_grid_lines = False
    scope_sheet.freeze_panes.freeze_rows(1)
    scope_headers = ["文件", "类型", "归属", "相对基线已修改", "实际参与构建", "Cppcheck覆盖方式", "排除原因", "SHA-256"]
    scope_sheet.get_range("A1:H1").values = [scope_headers]
    style_header(scope_sheet.get_range("A1:H1"))
    if scope:
        rows = [[item["file"], item["kind"], item["owner"],
                 "是" if item["modified_from_baseline"] else "否",
                 "是" if item["participates_in_build"] else "否", item["cppcheck_mode"],
                 item["exclusion_reason"], item["sha256"]] for item in scope]
        scope_sheet.get_range_by_indexes(1, 0, len(rows), len(scope_headers)).values = rows
        style_body(scope_sheet.get_range(f"A2:H{len(rows) + 1}"))
        scope_sheet.tables.add(f"A1:H{len(rows) + 1}", True, "ScopeAuditTable").style = "TableStyleMedium2"
    for columns, width in (("A:A", 48), ("B:C", 22), ("D:E", 16), ("F:F", 22),
                           ("G:G", 44), ("H:H", 66)):
        scope_sheet.get_range(columns).format.column_width = width

    summary = workbook.worksheets.get_item("统计汇总")
    summary.show_grid_lines = False
    summary.get_range("A1:H1").merge()
    put(summary, "A1", "Cppcheck 静态分析统计汇总")
    style_title(summary.get_range("A1:H1"))
    summary.get_range("A3:B3").values = [["指标", "值"]]
    style_header(summary.get_range("A3:B3"))
    metrics = [
        ["Git Commit", f"{commit}{'（工作区有未提交变更）' if data.get('git', {}).get('dirty') else ''}"],
        ["Cppcheck 版本", data["cppcheck_version"]],
        ["实际构建源文件", data["actual_build_source_count"]],
        ["实际构建 C 翻译单元", data["actual_build_c_count"]],
        ["Cppcheck 直接分析翻译单元", data["analysis_translation_unit_count"]],
        ["应用层依赖头文件", data["application_header_dependency_count"]],
        ["SDK头文件（仅解析）", data["sdk_parse_only_header_count"]],
        ["SDK问题统计", "不检查/不统计"],
        ["覆盖缺口", data["coverage_gap_count"]],
        ["原始 Cppcheck 发生次数", data["raw_native_occurrence_count"]],
        ["去重问题数", len(findings)],
        ["MISRA 执行状态", "已执行（部分自动化规则覆盖）" if data["misra"]["executed"] else "未执行（Addon 未安装）"],
        ["已批准 Deviation", data["approved_deviation_count"]],
    ]
    summary.get_range_by_indexes(3, 0, len(metrics), 2).values = metrics
    style_body(summary.get_range(f"A4:B{len(metrics) + 3}"))
    summary.get_range("D3:E3").values = [["严重程度", "数量"]]
    style_header(summary.get_range("D3:E3"))
    severities = ["error", "warning", "performance", "portability", "style", "information", "debug"]
    summary.get_range_by_indexes(3, 3, len(severities), 1).values = [[item] for item in severities]
    summary.get_range("E4").formulas = [[f"=COUNTIF('分析明细'!$F$2:$F${row_end},D4)"]]
    summary.get_range(f"E4:E{len(severities) + 3}").fill_down()
    style_body(summary.get_range(f"D4:E{len(severities) + 3}"))
    summary.get_range("G3:H3").values = [["分类", "数量"]]
    style_header(summary.get_range("G3:H3"))
    classifications = list(dict.fromkeys(item["classification"] for item in findings)) or ["无问题"]
    summary.get_range_by_indexes(3, 6, len(classifications), 1).values = [[item] for item in classifications]
    summary.get_range("H4").formulas = [[f"=COUNTIF('分析明细'!$J$2:$J${row_end},G4)"]]
    summary.get_range(f"H4:H{len(classifications) + 3}").fill_down()
    style_body(summary.get_range(f"G4:H{len(classifications) + 3}"))
    config_start = max(len(metrics) + 5, len(severities) + 5, len(classifications) + 5)
    summary.get_range(f"A{config_start}:H{config_start}").merge()
    put(summary, f"A{config_start}", "真实工程配置与能力边界")
    style_title(summary.get_range(f"A{config_start}:H{config_start}"))
    predefines = "; ".join(f"{key}={value}" for key, value in data["configuration"]["compiler_predefines_applied"].items()) or "无"
    config_rows = [
        ["配置来源", data["configuration"]["configuration_source"]],
        ["检查范围策略", data["scope_policy"]],
        ["源文件选择策略", data["selection_policy"]],
        ["C 标准", data["configuration"]["c_standard"]],
        ["目标 MCU", f"{data['configuration']['target_mcu']}; {data['configuration']['startup_profile']}"],
        ["Include Path", "\n".join(data["configuration"]["include_paths"])],
        ["宏定义", "; ".join(data["configuration"]["defines"])],
        ["TC32 条件宏", predefines],
        ["Cppcheck 配置", f"{data['cppcheck_config']}；SDK范围排除证据见 sdk_scope_exclusions.json"],
        ["MISRA 能力边界", data["misra"]["coverage"]],
        ["漏扫判断", "未发现：应用层实际编译 C 文件均直接分析，引用头文件均经 -MM 核对" if data["coverage_gap_count"] == 0 else f"存在 {data['coverage_gap_count']} 项：{'; '.join(data['coverage_gaps'])}"],
    ]
    summary.get_range_by_indexes(config_start, 0, len(config_rows), 2).values = config_rows
    style_body(summary.get_range(f"A{config_start + 1}:B{config_start + len(config_rows)}"))
    for columns, width in (("A:A", 28), ("B:B", 76), ("C:C", 3), ("D:D", 22),
                           ("E:E", 12), ("F:F", 3), ("G:G", 34), ("H:H", 12)):
        summary.get_range(columns).format.column_width = width

    deviation_sheet = workbook.worksheets.get_item("Deviation候选")
    deviation_sheet.show_grid_lines = False
    deviation_sheet.freeze_panes.freeze_rows(1)
    deviation_headers = ["文件", "函数", "行号", "规则/Cppcheck ID", "问题描述", "不修改原因建议", "影响分析建议", "规避措施建议", "当前结论", "责任人", "批准人", "批准日期"]
    deviation_sheet.get_range("A1:L1").values = [deviation_headers]
    style_header(deviation_sheet.get_range("A1:L1"))
    if candidates:
        rows = [[item["file"], item.get("function", ""), item["line"],
                 f"MISRA C {item['misra_rule']}" if item.get("misra_rule") else item["id"],
                 item["message"], "应用层当前逻辑暂不修改；待评审确认保留依据",
                 "需结合调用路径判断对 BMS 安全功能、数据完整性和故障响应的实际影响",
                 "保持针对性回归测试；相关配置或调用路径变化时重新评估",
                 "候选（未批准）", "", "", ""] for item in candidates]
        deviation_sheet.get_range_by_indexes(1, 0, len(rows), len(deviation_headers)).values = rows
        style_body(deviation_sheet.get_range(f"A2:L{len(rows) + 1}"))
        deviation_sheet.tables.add(f"A1:L{len(rows) + 1}", True, "DeviationCandidatesTable").style = "TableStyleMedium2"
    for columns, width in (("A:A", 46), ("B:B", 22), ("C:C", 9), ("D:D", 24),
                           ("E:H", 44), ("I:I", 20), ("J:L", 16)):
        deviation_sheet.get_range(columns).format.column_width = width

    company = workbook.worksheets.get_item("公司软件审核单检查")
    company.delete_all_drawings()
    company.get_range("A4:O22").clear({"apply_to": "contents"})
    for row in range(4, 23):
        company.get_range(f"E{row}:O{row}").merge()
    company.get_range("A4:E4").values = [["Cppcheck ID", "严重程度", "数量", "分类", "说明（完整明细见“分析明细”）"]]
    style_header(company.get_range("A4:E4"))
    top_ids = list(data.get("id_counts", {}).items())[:18]
    for index, (issue_id, count) in enumerate(top_ids, start=5):
        sample = next((item for item in findings if item["id"] == issue_id), {})
        company.get_range(f"A{index}:E{index}").values = [[issue_id, sample.get("severity", ""), count, sample.get("classification", ""), sample.get("message", "")]]
    if top_ids:
        style_body(company.get_range(f"A5:E{len(top_ids) + 4}"))
    for columns, width in (("A:A", 25), ("B:B", 13), ("C:C", 9), ("D:D", 25), ("E:O", 7)):
        company.get_range(columns).format.column_width = width

    misra = workbook.worksheets.get_item("MISRA-Rules")
    misra.delete_all_drawings()
    for address, value in (("B4", "执行状态"), ("C4", "已执行（部分覆盖）" if data["misra"]["executed"] else "未执行"),
                           ("B5", "能力边界"), ("C5", data["misra"]["coverage"]),
                           ("B6", "重要说明"), ("C6", "普通 Cppcheck Warning/Style/Advisory 不统计为 MISRA violation；本报告不声明完整 MISRA C 合规。")):
        put(misra, address, value)
    misra.get_range("B4:C6").format.wrap_text = True
    style_header(misra.get_range("B4:B6"))
    style_body(misra.get_range("C4:C6"))
    misra_counts: dict[str, int] = {}
    for item in findings:
        if item.get("misra_rule"):
            misra_counts[item["misra_rule"]] = misra_counts.get(item["misra_rule"], 0) + 1
    if misra_counts:
        misra.get_range("B8:D8").values = [["MISRA Rule", "数量", "状态"]]
        style_header(misra.get_range("B8:D8"))
        rows = [[rule, count, "待评审"] for rule, count in misra_counts.items()]
        misra.get_range_by_indexes(8, 1, len(rows), 3).values = rows
        style_body(misra.get_range(f"B9:D{len(rows) + 8}"))
    misra.get_range("B:B").format.column_width = 22
    misra.get_range("C:C").format.column_width = 80
    misra.get_range("D:D").format.column_width = 18

    template_deviation = workbook.worksheets.get_item("DevitionList")
    template_deviation.get_range("B5:K17").clear({"apply_to": "contents"})
    for index, item in enumerate(candidates[:13], start=5):
        template_deviation.get_range(f"B{index}:K{index}").values = [[
            item["file"], item.get("function", ""),
            f"MISRA C {item['misra_rule']}" if item.get("misra_rule") else item["id"],
            "应用层候选；待人工确认", "需评估对安全功能与运行时行为的影响",
            "执行针对性回归测试", "", "候选（未批准）", "", "",
        ]]
    for columns, width in (("B:B", 46), ("C:C", 24), ("D:D", 24), ("E:G", 40),
                           ("H:H", 18), ("I:I", 22), ("J:K", 16)):
        template_deviation.get_range(columns).format.column_width = width
    template_deviation.get_range("5:17").format.row_height = 48
    template_deviation.get_range("B5:K17").format.wrap_text = True

    preview_dir.mkdir(parents=True, exist_ok=True)
    verification = {
        "general": workbook.inspect({"kind": "table", "sheet_id": "General", "range": "A16:E36", "include": "values,formulas", "table_max_rows": 24, "table_max_cols": 6, "max_chars": 12000}).ndjson,
        "summary": workbook.inspect({"kind": "table", "sheet_id": "统计汇总", "range": f"A1:H{config_start + len(config_rows)}", "include": "values,formulas", "table_max_rows": 60, "table_max_cols": 8, "max_chars": 16000}).ndjson,
        "formula_errors": workbook.inspect({"kind": "match", "search_term": "#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A", "options": {"use_regex": True, "max_results": 300}, "summary": "final formula error scan"}).ndjson,
    }
    (preview_dir / "verification.json").write_text(json.dumps(verification, ensure_ascii=False, indent=2), encoding="utf-8")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    # Write the exported bytes into the build evidence directory directly.
    # Blob.save() also registers an interactive desktop artifact operation;
    # that lifecycle is inappropriate for repeatable command-line build data.
    output_path.write_bytes(SpreadsheetFile.export_xlsx(workbook).data)
    print(json.dumps({"output_path": str(output_path), "sheets": [sheet.name for sheet in workbook.worksheets.items], "findings": len(findings)}, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
