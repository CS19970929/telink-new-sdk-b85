#!/usr/bin/env python3
"""Run cppcheck for the 825x ble_sample BMS sources."""

from __future__ import annotations

import argparse
import datetime as _dt
import os
import shutil
import subprocess
import xml.etree.ElementTree as ET
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


SCRIPT_PATH = Path(__file__).resolve()
SDK_ROOT = SCRIPT_PATH.parents[1]
PACKAGE_ROOT = SDK_ROOT.parent
REPO_ROOT = PACKAGE_ROOT.parent

PROJECT_ROOT = SDK_ROOT / "project" / "tlsr_tc32" / "B85" / "825x_ble_sample"
BMS_ROOT = SDK_ROOT / "vendor" / "ble_sample"
BUILD_ROOT = PACKAGE_ROOT / "build" / "static_analysis"
DOC_ROOT = PACKAGE_ROOT / "doc"

CPPCHECK_CANDIDATES = (
    os.environ.get("CPPCHECK"),
    shutil.which("cppcheck"),
    r"C:\Program Files\Cppcheck\cppcheck.exe",
    r"C:\Program Files (x86)\Cppcheck\cppcheck.exe",
)

INCLUDE_DIRS = (
    SDK_ROOT / "project" / "tlsr_tc32" / "B85",
    SDK_ROOT,
    SDK_ROOT / "vendor" / "common",
    SDK_ROOT / "common",
    SDK_ROOT / "drivers" / "B85",
)

DEFINES = (
    "__PROJECT_8258_BLE_SAMPLE__=1",
    "CHIP_TYPE=CHIP_TYPE_825x",
)

ENABLE_SET = "warning,style,performance,portability,information"


@dataclass
class CppcheckIssue:
    severity: str
    issue_id: str
    message: str
    file: str
    line: str


def rel(path: Path | str) -> str:
    path_obj = Path(path)
    try:
        return path_obj.resolve().relative_to(REPO_ROOT).as_posix()
    except ValueError:
        return str(path)


def find_cppcheck() -> Path:
    for candidate in CPPCHECK_CANDIDATES:
        if candidate:
            path = Path(candidate)
            if path.exists():
                return path
    raise FileNotFoundError(
        "cppcheck not found. Set CPPCHECK or install Cppcheck under Program Files."
    )


def source_files() -> list[Path]:
    return sorted(BMS_ROOT.glob("*.c"))


def base_cppcheck_args(cppcheck: Path) -> list[str]:
    args = [
        str(cppcheck),
        "--enable=" + ENABLE_SET,
        "--std=c99",
        "--language=c",
        "--platform=unix32",
        "--max-configs=1",
        "--check-level=exhaustive",
        "--quiet",
        "--inline-suppr",
    ]
    for include_dir in INCLUDE_DIRS:
        args.extend(["-I", str(include_dir)])
    for define in DEFINES:
        args.append("-D" + define)
    args.extend(str(path) for path in source_files())
    return args


def run_command(args: Sequence[str], *, cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        cwd=str(cwd),
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def parse_cppcheck_xml(xml_path: Path) -> list[CppcheckIssue]:
    if not xml_path.exists() or xml_path.stat().st_size == 0:
        return []

    return parse_cppcheck_xml_root(ET.parse(xml_path).getroot())


def parse_cppcheck_xml_text(xml_text: str) -> list[CppcheckIssue]:
    if not xml_text.strip():
        return []
    return parse_cppcheck_xml_root(ET.fromstring(xml_text))


def parse_cppcheck_xml_root(root: ET.Element) -> list[CppcheckIssue]:
    issues: list[CppcheckIssue] = []
    for error in root.findall(".//error"):
        location = error.find("location")
        issues.append(
            CppcheckIssue(
                severity=error.attrib.get("severity", ""),
                issue_id=error.attrib.get("id", ""),
                message=error.attrib.get("msg", ""),
                file=location.attrib.get("file", "") if location is not None else "",
                line=location.attrib.get("line", "") if location is not None else "",
            )
        )
    return issues


def is_bms_scope_issue(issue: CppcheckIssue) -> bool:
    return "/vendor/ble_sample/" in issue.file.replace("\\", "/").lower()


def git_value(args: Sequence[str]) -> str:
    try:
        result = run_command(["git", *args], cwd=REPO_ROOT)
        return result.stdout.strip() or "unknown"
    except OSError:
        return "unknown"


def markdown_table_row(values: Sequence[str]) -> str:
    escaped = [value.replace("|", "\\|").replace("\n", " ") for value in values]
    return "| " + " | ".join(escaped) + " |"


def write_report(
    report_path: Path,
    xml_path: Path,
    issues: list[CppcheckIssue],
    cppcheck_version: str,
    cppcheck_args: Sequence[str],
    cppcheck_returncode: int,
) -> None:
    counts = Counter(issue.severity for issue in issues)
    now = _dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    commit = git_value(["rev-parse", "--short", "HEAD"])
    branch = git_value(["branch", "--show-current"])

    lines = [
        "# BMS cppcheck 静态分析报告",
        "",
        f"- 分析时间：{now}",
        f"- Git 分支：{branch}",
        f"- Git 提交：{commit}",
        f"- 工程：{rel(PROJECT_ROOT)}",
        f"- 分析范围：{rel(BMS_ROOT)}/*.c",
        f"- cppcheck 版本：{cppcheck_version}",
        f"- cppcheck 返回码：{cppcheck_returncode}",
        f"- 原始 XML：{rel(xml_path)}",
        "",
        "## 结果摘要",
        "",
        markdown_table_row(["级别", "数量"]),
        markdown_table_row(["---", "---:"]),
    ]

    for severity in ("error", "warning", "style", "performance", "portability", "information"):
        lines.append(markdown_table_row([severity, str(counts.get(severity, 0))]))
    lines.extend(
        [
            markdown_table_row(["total", str(len(issues))]),
            "",
            "## 问题明细",
            "",
        ]
    )
    if issues:
        lines.extend(
            [
                markdown_table_row(["级别", "规则", "文件", "行号", "描述"]),
                markdown_table_row(["---", "---", "---", "---:", "---"]),
            ]
        )
        for issue in issues:
            file_text = rel(issue.file) if issue.file else ""
            lines.append(
                markdown_table_row(
                    [
                        issue.severity,
                        issue.issue_id,
                        file_text,
                        issue.line,
                        issue.message,
                    ]
                )
            )
    else:
        lines.append("未发现 cppcheck 问题。")

    lines.extend(
        [
            "",
            "## 命令记录",
            "",
            "```powershell",
            " ".join('"' + arg + '"' if " " in arg else arg for arg in cppcheck_args),
            "```",
            "",
            "## 已知限制",
            "",
            "- 本报告使用 cppcheck 的通用 C 静态分析能力，不能替代 TC32 交叉编译器告警和目标板运行测试。",
            "- `--platform=unix32` 用于近似 32 位嵌入式目标；若后续获得 TC32 专用 cppcheck 平台配置，应替换为目标专用配置。",
            "- SDK/芯片库中依赖编译器扩展和内存映射寄存器，可能产生保守告警，需要结合代码审查确认。",
        ]
    )

    report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def run_report() -> int:
    cppcheck = find_cppcheck()
    BUILD_ROOT.mkdir(parents=True, exist_ok=True)
    DOC_ROOT.mkdir(parents=True, exist_ok=True)

    xml_path = BUILD_ROOT / "cppcheck_bms_ble_sample.xml"
    stdout_path = BUILD_ROOT / "cppcheck_bms_ble_sample.stdout.txt"
    stderr_path = BUILD_ROOT / "cppcheck_bms_ble_sample.stderr.txt"
    report_path = DOC_ROOT / f"static_analysis_report_{_dt.datetime.now():%Y%m%d}.md"

    version_result = run_command([str(cppcheck), "--version"], cwd=REPO_ROOT)
    cppcheck_version = (version_result.stdout or version_result.stderr).strip()

    args = base_cppcheck_args(cppcheck)
    args.insert(1, "--xml")
    args.insert(2, "--xml-version=2")

    result = run_command(args, cwd=REPO_ROOT)
    stdout_path.write_text(result.stdout, encoding="utf-8")
    stderr_path.write_text(result.stderr, encoding="utf-8")
    xml_path.write_text(result.stderr, encoding="utf-8")

    raw_issues = parse_cppcheck_xml(xml_path)
    issues = [issue for issue in raw_issues if is_bms_scope_issue(issue)]
    write_report(
        report_path,
        xml_path,
        issues,
        cppcheck_version,
        args,
        result.returncode,
    )

    print(f"cppcheck: {cppcheck_version}")
    print(f"issues: {len(issues)} in BMS scope, {len(raw_issues)} raw")
    print(f"report: {report_path}")
    print(f"xml: {xml_path}")
    return result.returncode


def run_vscode_problems() -> int:
    cppcheck = find_cppcheck()
    args = base_cppcheck_args(cppcheck)
    args.insert(1, "--xml")
    args.insert(2, "--xml-version=2")
    result = run_command(args, cwd=REPO_ROOT)
    try:
        issues = [issue for issue in parse_cppcheck_xml_text(result.stderr) if is_bms_scope_issue(issue)]
    except ET.ParseError:
        if result.stdout:
            print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="")
        return result.returncode

    for issue in issues:
        line = issue.line or "0"
        print(f"{issue.file}:{line}:1: {issue.severity}: {issue.message} [{issue.issue_id}]")
    return result.returncode


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--vscode-problems",
        action="store_true",
        help="Print cppcheck output using a gcc-like template for VS Code Problems.",
    )
    args = parser.parse_args(argv)
    if args.vscode_problems:
        return run_vscode_problems()
    return run_report()


if __name__ == "__main__":
    raise SystemExit(main())
