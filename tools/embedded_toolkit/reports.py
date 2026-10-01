"""Self-contained escaped HTML, JUnit and Markdown reports with source identity."""
from datetime import datetime, timezone
import html
import json
from pathlib import Path
import xml.etree.ElementTree as ET

from .common import write_json


def write_reports(output, results, identity, compiler):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    failed = sum(not r['passed'] for r in results)
    summary = {'schema_version': 1, 'created_utc': datetime.now(timezone.utc).isoformat(),
               'provenance': identity, 'compiler': compiler, 'cases': len(results),
               'failed': failed, 'hardware_validated': False, 'results': results}
    write_json(output / 'results.json', summary)
    suite = ET.Element('testsuite', name='embedded-toolkit', tests=str(len(results)), failures=str(failed))
    properties = ET.SubElement(suite, 'properties')
    for key, value in identity.items():
        ET.SubElement(properties, 'property', name=key, value=json.dumps(value, ensure_ascii=False))
    rows, md = [], ['# Host / Fake HIL test report', '',
                    f"Commit: `{identity['commit']}`; dirty: `{identity['dirty']}`", '',
                    f'Cases: {len(results)}; failures: {failed}. Hardware validated: false.', '',
                    '| Case | Result | Assertions | Details |', '|---|---|---:|---|']
    for result in results:
        case = ET.SubElement(suite, 'testcase', name=result['name'], classname='host.protection')
        if not result['passed']:
            ET.SubElement(case, 'failure', message='; '.join(result['failures'])).text = '\n'.join(result['failures'])
        details = '; '.join(result['failures']) or 'PASS'
        safe_name = result['name'].replace('|', '\\|').replace('\n', ' ')
        md.append(f"| {safe_name} | {'PASS' if result['passed'] else 'FAIL'} | {result['assertions']} | {details.replace('|', '/') } |")
        rows.append('<tr><td>' + html.escape(result['name']) + '</td><td>' +
                    ('PASS' if result['passed'] else 'FAIL') + '</td><td>' + str(result['assertions']) +
                    '</td><td>' + html.escape(details) + '</td></tr>')
    ET.ElementTree(suite).write(output / 'junit.xml', encoding='utf-8', xml_declaration=True)
    (output / 'report.md').write_text('\n'.join(md) + '\n', encoding='utf-8')
    document = ('<!doctype html><html lang="zh"><meta charset="utf-8"><title>BMS Host Report</title>'
        '<style>body{font:16px system-ui;margin:32px;max-width:1400px}table{border-collapse:collapse;width:100%}'
        'td,th{border:1px solid #ddd;padding:8px;text-align:left}pre{white-space:pre-wrap}</style>'
        f'<h1>BMS Host / Fake HIL</h1><p>{len(results)} cases / {failed} failures. Hardware validated: false.</p>'
        '<p>Software thresholds: production C. Final MOS / short release: host reference policy.</p>'
        '<details><summary>Source identity / compiler</summary><pre>' +
        html.escape(json.dumps({'provenance': identity, 'compiler': compiler}, ensure_ascii=False, indent=2)) +
        '</pre></details><table><tr><th>Case</th><th>Result</th><th>Assertions</th><th>Details</th></tr>' +
        ''.join(rows) + '</table></html>')
    (output / 'report.html').write_text(document, encoding='utf-8')
    return summary
