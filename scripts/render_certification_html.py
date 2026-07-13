"""将认证 Markdown 转为无需外部依赖的可打印 HTML。"""

from __future__ import annotations

import argparse
import html
import re
from pathlib import Path


STYLE = """
:root { --ink:#18212b; --muted:#5d6875; --line:#cfd6de; --blue:#174d7a; --soft:#eef5fa; --warn:#fff4d6; }
* { box-sizing:border-box; }
body { margin:0; color:var(--ink); font:14px/1.65 "Microsoft YaHei","Noto Sans CJK SC",Arial,sans-serif; background:#edf1f4; }
main { width:min(1180px,calc(100% - 40px)); margin:28px auto; padding:48px 58px; background:white; box-shadow:0 3px 18px #0002; }
h1 { margin:0 0 22px; padding-bottom:14px; color:#123e61; font-size:30px; border-bottom:3px solid var(--blue); }
h2 { margin:34px 0 14px; color:var(--blue); font-size:22px; border-bottom:1px solid var(--line); }
h3 { margin:24px 0 10px; color:#244f70; font-size:17px; }
p { margin:8px 0 12px; }
blockquote { margin:18px 0; padding:12px 16px; background:var(--warn); border-left:5px solid #c78700; }
blockquote p { margin:0; }
table { width:100%; margin:12px 0 22px; border-collapse:collapse; table-layout:auto; }
th,td { padding:7px 9px; vertical-align:top; border:1px solid var(--line); overflow-wrap:anywhere; }
th { color:#173d5c; background:var(--soft); text-align:left; }
tr:nth-child(even) td { background:#fafbfd; }
code { padding:1px 4px; color:#8b2e20; background:#f4f4f4; border-radius:3px; font-family:Consolas,monospace; }
pre { padding:14px; overflow:auto; color:#e8eef4; background:#1e2933; border-radius:5px; }
pre code { padding:0; color:inherit; background:transparent; }
ul,ol { margin:8px 0 14px 24px; padding:0; }
.doc-meta { margin-bottom:28px; padding:10px 14px; color:var(--muted); background:#f5f7f9; border:1px solid var(--line); }
.footer { margin-top:42px; padding-top:10px; color:var(--muted); border-top:1px solid var(--line); font-size:12px; }
@media print { body{background:white} main{width:auto;margin:0;padding:15mm;box-shadow:none} h2,h3{break-after:avoid} table{break-inside:avoid} @page{size:A4;margin:12mm} }
"""


def inline(text: str) -> str:
    escaped = html.escape(text.strip())
    escaped = re.sub(r"`([^`]+)`", r"<code>\1</code>", escaped)
    escaped = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", escaped)
    return escaped


def cells(line: str) -> list[str]:
    return [part.strip() for part in line.strip().strip("|").split("|")]


def is_table_separator(line: str) -> bool:
    parts = cells(line)
    return bool(parts) and all(re.fullmatch(r":?-{3,}:?", part) for part in parts)


def markdown_to_html(markdown: str) -> tuple[str, str]:
    lines = markdown.replace("\r\n", "\n").splitlines()
    output: list[str] = []
    title = "认证文档"
    index = 0
    while index < len(lines):
        line = lines[index]
        stripped = line.strip()
        if not stripped:
            index += 1
            continue
        if stripped.startswith("```"):
            language = stripped[3:].strip()
            block: list[str] = []
            index += 1
            while index < len(lines) and not lines[index].strip().startswith("```"):
                block.append(lines[index])
                index += 1
            index += 1
            output.append(f'<pre data-language="{html.escape(language)}"><code>{html.escape(chr(10).join(block))}</code></pre>')
            continue
        heading = re.match(r"^(#{1,6})\s+(.+)$", stripped)
        if heading:
            level = len(heading.group(1))
            text = heading.group(2).strip()
            if level == 1:
                title = text
            output.append(f"<h{level}>{inline(text)}</h{level}>")
            index += 1
            continue
        if stripped.startswith(">"):
            block: list[str] = []
            while index < len(lines) and lines[index].strip().startswith(">"):
                block.append(lines[index].strip()[1:].strip())
                index += 1
            output.append(f"<blockquote><p>{inline(' '.join(block))}</p></blockquote>")
            continue
        if stripped.startswith("|") and index + 1 < len(lines) and is_table_separator(lines[index + 1]):
            headers = cells(stripped)
            index += 2
            rows: list[list[str]] = []
            while index < len(lines) and lines[index].strip().startswith("|"):
                rows.append(cells(lines[index]))
                index += 1
            output.append("<table><thead><tr>" + "".join(f"<th>{inline(item)}</th>" for item in headers) + "</tr></thead><tbody>")
            for row in rows:
                row += [""] * (len(headers) - len(row))
                output.append("<tr>" + "".join(f"<td>{inline(item)}</td>" for item in row[:len(headers)]) + "</tr>")
            output.append("</tbody></table>")
            continue
        list_match = re.match(r"^([-*])\s+(.+)$", stripped)
        ordered_match = re.match(r"^\d+\.\s+(.+)$", stripped)
        if list_match or ordered_match:
            tag = "ul" if list_match else "ol"
            items: list[str] = []
            while index < len(lines):
                current = lines[index].strip()
                match = re.match(r"^[-*]\s+(.+)$", current) if tag == "ul" else re.match(r"^\d+\.\s+(.+)$", current)
                if not match:
                    break
                items.append(match.group(1))
                index += 1
            output.append(f"<{tag}>" + "".join(f"<li>{inline(item)}</li>" for item in items) + f"</{tag}>")
            continue

        paragraph = [stripped]
        index += 1
        while index < len(lines):
            next_line = lines[index].strip()
            if (not next_line or next_line.startswith(("#", ">", "```", "|")) or
                    re.match(r"^[-*]\s+", next_line) or re.match(r"^\d+\.\s+", next_line)):
                break
            paragraph.append(next_line)
            index += 1
        output.append(f"<p>{inline(' '.join(paragraph))}</p>")

    body = "\n".join(output)
    page = f"""<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{html.escape(title)}</title><style>{STYLE}</style></head>
<body><main><div class="doc-meta">TLSR8258-SH367309 BMS · 认证准备资料 · 生成日期 2026-07-13</div>
{body}
<div class="footer">由受控 Markdown 生成。未执行项不得作为认证通过证据；纸质副本使用前应核对 Git 提交和固件哈希。</div>
</main></body></html>"""
    return title, page


def render(source: Path, target: Path) -> None:
    _, page = markdown_to_html(source.read_text(encoding="utf-8"))
    target.write_text(page, encoding="utf-8", newline="\n")
    print(f"{source} -> {target}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("sources", nargs="+", type=Path)
    args = parser.parse_args()
    for source in args.sources:
        render(source, source.with_suffix(".html"))


if __name__ == "__main__":
    main()
