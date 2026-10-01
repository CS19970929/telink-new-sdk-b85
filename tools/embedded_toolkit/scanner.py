"""Portable lexical C/C++ inventory. Findings are review candidates, not proofs.

No preprocessor/AST is evaluated. Ambiguous call targets stay ambiguous. A
source-order/compile_commands list means listed membership, not active #if code.
"""
from collections import Counter, defaultdict
import html
import hashlib
import json
from pathlib import Path
import re

from .common import git, provenance, write_json

SOURCE_SUFFIXES = {'.c', '.h', '.cpp', '.hpp', '.cc', '.cxx'}
SKIP_DIRS = {'.git', 'build', 'node_modules', 'outputs', 'release', '.venv', '__pycache__'}
TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
FUNCTION = re.compile(r'(?m)^[ \t]*(?!if\b|while\b|for\b|switch\b|else\b)(?:[A-Za-z_]\w*[ \t*]+)+([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{')
CALL = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
KEYWORDS = {'if', 'for', 'while', 'switch', 'return', 'sizeof', '_Static_assert'}


def mask_literals(source):
    return TOKEN.sub(lambda match: re.sub(r'[^\n]', ' ', match.group(0)), source)


def functions(source):
    masked = mask_literals(source)
    result = []
    end_last = 0
    for match in FUNCTION.finditer(masked):
        if match.start() < end_last:
            continue
        start = match.end() - 1
        depth, end = 1, start + 1
        while end < len(masked) and depth:
            depth += (masked[end] == '{') - (masked[end] == '}')
            end += 1
        if depth:
            continue
        end_last = end
        body = masked[start:end]
        result.append({'name': match.group(1), 'line': masked.count('\n', 0, match.start()) + 1,
                       'lines': masked.count('\n', match.start(), end) + 1,
                       'calls': sorted(set(CALL.findall(body)) - KEYWORDS),
                       'blocking_calls': sorted(set(re.findall(r'\b(?:Delay\w*|delay_\w+|sleep\w*|usleep)\s*(?=\()', body))),
                       'unbounded_loop_candidate': bool(re.search(r'while\s*\(\s*1\s*\)|for\s*\(\s*;\s*;\s*\)', body)),
                       'isr_candidate': bool(re.search(r'irq|interrupt|isr|handler', match.group(1), re.I)),
                       'hardware_tokens': sorted(set(re.findall(r'\b(?:gpio_\w+|reg_\w+|HAL_\w+|LL_\w+)\b', body)))})
        result[-1]['_identifiers'] = set(re.findall(r'\b[A-Za-z_]\w*\b', body))
    return result


def source_membership(root, source_order=None, source_root=None, compile_commands=None):
    if source_order and compile_commands:
        raise ValueError('choose source_order or compile_commands, not both')
    if source_order:
        if not source_root:
            raise ValueError('--source-order needs --source-root')
        entries = [x.strip() for x in Path(source_order).read_text(encoding='utf-8').splitlines()
                   if x.strip() and not x.lstrip().startswith('#')]
        return {(Path(source_root) / x).resolve() for x in entries}
    if compile_commands:
        records = json.loads(Path(compile_commands).read_text(encoding='utf-8'))
        return {(Path(x['directory']) / x['file']).resolve() for x in records}
    return None


def dependency_cycles(edges):
    """Iterative Kosaraju; large repositories cannot overflow Python recursion."""
    graph, reverse = defaultdict(set), defaultdict(set)
    for edge in edges:
        graph[edge['from']].add(edge['to'])
        reverse[edge['to']].add(edge['from'])
    nodes = set(graph) | set(reverse)
    visited, order = set(), []
    for node in sorted(nodes):
        if node in visited:
            continue
        stack = [(node, False)]
        while stack:
            current, finished = stack.pop()
            if finished:
                order.append(current)
            elif current not in visited:
                visited.add(current)
                stack.append((current, True))
                stack += [(child, False) for child in sorted(graph[current], reverse=True) if child not in visited]
    visited, cycles = set(), []
    for node in reversed(order):
        if node in visited:
            continue
        group, stack = [], [node]
        while stack:
            current = stack.pop()
            if current in visited:
                continue
            visited.add(current)
            group.append(current)
            stack += sorted(reverse[current] - visited)
        if len(group) > 1 or node in graph[node]:
            cycles.append(sorted(group))
    return cycles


def scan(root, source_order=None, source_root=None, compile_commands=None):
    root = Path(root).resolve()
    if not root.is_dir():
        raise ValueError('repository directory missing')
    membership = source_membership(root, source_order, source_root, compile_commands)
    paths = sorted(p for p in root.rglob('*') if p.is_file() and p.suffix.lower() in SOURCE_SUFFIXES
                   and not (set(p.relative_to(root).parts[:-1]) & SKIP_DIRS))
    files, symbols, risks = [], [], []
    basename = defaultdict(list)
    for p in paths:
        basename[p.name].append(p.relative_to(root).as_posix())
    for path in paths:
        raw = path.read_bytes()
        try:
            source, encoding = raw.decode('utf-8-sig'), 'utf-8'
        except UnicodeDecodeError:
            try:
                source, encoding = raw.decode('gb18030'), 'gb18030'
            except UnicodeDecodeError:
                source, encoding = raw.decode('latin-1'), 'latin-1-fallback'
        relative = path.relative_to(root).as_posix()
        parsed = functions(source)
        includes = re.findall(r'^\s*#\s*include\s*[<"]([^>"\n]+)', re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S), re.M)
        masked = mask_literals(source)
        # Top-level declaration candidates exclude function bodies and struct
        # fields by brace depth; multi-line declarations remain a known limit.
        depth, globals_ = 0, []
        for line_no, line in enumerate(masked.splitlines(), 1):
            if depth == 0 and not line.lstrip().startswith('#') and ';' in line and '(' not in line:
                declaration = re.search(r'^\s*(?:extern\s+)?(?:static\s+)?(?:volatile\s+)?(?:const\s+)?(?:[\w]+[\s*]+)+([A-Za-z_]\w*)\s*(?:\[[^;]*\])?\s*(?:=[^;]*)?;', line)
                if declaration and not line.lstrip().startswith(('typedef ', 'return ')):
                    globals_.append({'name': declaration.group(1), 'line': line_no,
                                     'extern': bool(re.search(r'\bextern\b', line)),
                                     'static': bool(re.search(r'\bstatic\b', line))})
            depth += line.count('{') - line.count('}')
        file = {'path': relative, 'encoding': encoding, 'sha256': hashlib.sha256(raw).hexdigest(),
                'lines': len(source.splitlines()),
                'listed_source': path.resolve() in membership if membership is not None else None,
                'functions': len(parsed), 'globals': globals_, 'includes': includes}
        files.append(file)
        if file['lines'] > 1500:
            risks.append({'path': relative, 'line': 1, 'kind': 'large_file', 'detail': f"{file['lines']} lines"})
        for f in parsed:
            f['path'] = relative
            symbols.append(f)
            for kind, found in [('large_function', f['lines'] > 120),
                                ('blocking_call', bool(f['blocking_calls'])),
                                ('unbounded_loop_candidate', f['unbounded_loop_candidate']),
                                ('isr_complexity_candidate', f['isr_candidate'] and (f['lines'] > 60 or f['blocking_calls'])),
                                ('hardware_coupling_candidate', bool(f['hardware_tokens']) and
                                 bool(re.search(r'soc|protect|sleep|param', f['name'], re.I)))]:
                if found:
                    risks.append({'path': relative, 'line': f['line'], 'kind': kind, 'detail': f['name']})
    known = {x['path'] for x in files}
    include_edges, unresolved = [], []
    for f in files:
        for include in f['includes']:
            local = (Path(f['path']).parent / include).as_posix()
            candidates = [local] if local in known else basename.get(Path(include).name, [])
            if len(candidates) == 1:
                include_edges.append({'from': f['path'], 'to': candidates[0]})
            else:
                unresolved.append({'from': f['path'], 'include': include, 'candidates': candidates})
    owners = defaultdict(list)
    for f in symbols:
        owners[f['name']].append(f['path'])
    call_edges = []
    global_owners = defaultdict(list)
    for file in files:
        for declaration in file['globals']:
            if not declaration['extern']:
                global_owners[declaration['name']].append((file['path'], declaration['static']))
    global_edges = []
    for f in symbols:
        for name in sorted(f.pop('_identifiers') & global_owners.keys()):
            candidates = sorted({p for p, local in global_owners[name] if p == f['path'] or not local})
            if f['path'] in candidates:
                candidates = [f['path']]
            global_edges.append({'from': f['path'] + ':' + f['name'], 'global': name,
                                 'targets': candidates, 'evidence': 'lexical reference candidate'})
        for call in f['calls']:
            candidates = sorted(set(owners.get(call, [])))
            if f['path'] in candidates:
                candidates = [f['path']]
            call_edges.append({'from': f['path'] + ':' + f['name'], 'call': call,
                               'targets': candidates, 'resolved': len(candidates) == 1})
    # Listed .c files only: authoritative order excludes SDK/alternate products.
    listed = [f for f in files if f['listed_source']]
    missing = sorted(str(p) for p in (membership or ()) if not p.is_file())
    return {'schema_version': 1, 'root': str(root), 'provenance': provenance(root),
            'analysis': 'lexical, no preprocessor/AST; candidates require source review',
            'membership': 'listed by build input; does not evaluate #if' if membership is not None else 'unknown',
            'summary': {'files': len(files), 'lines': sum(f['lines'] for f in files),
                        'functions': len(symbols), 'global_candidates': sum(len(f['globals']) for f in files),
                        'listed_sources': len(listed), 'listed_lines': sum(f['lines'] for f in listed),
                        'missing_sources': missing},
            'files': files, 'symbols': symbols, 'include_edges': include_edges,
            'include_cycles': dependency_cycles(include_edges), 'global_edges': global_edges,
            'unresolved_includes': unresolved, 'call_edges': call_edges, 'risks': risks}


def write_scan(output, data):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    write_json(output / 'architecture.json', data)
    write_json(output / 'SYMBOL_INDEX.json', {'schema_version': 1, 'provenance': data['provenance'], 'symbols': data['symbols']})
    summary = data['summary']
    rows = ['| File | LOC | Functions | Listed |', '|---|---:|---:|---|']
    rows += [f"| {f['path']} | {f['lines']} | {f['functions']} | {f['listed_source']} |" for f in data['files']]
    (output / 'MODULE_INDEX.md').write_text('# Module index\n\n' + '\n'.join(rows) + '\n', encoding='utf-8')
    facts = ['# AI_CONTEXT', '', f"Source: `{data['root']}`", f"Commit: `{data['provenance']['commit']}`",
             f"Dirty: `{data['provenance']['dirty']}`", '', data['analysis'], '',
             '## Inventory', '', json.dumps(summary, ensure_ascii=False, indent=2), '',
             '## Evidence limits', '', 'Listed sources are build membership, not proof of active conditional code.',
             'Unresolved calls/includes remain unresolved. No hardware claim is inferred.', '',
             '## Repository entry documents', '']
    for name in ('AGENTS.md', 'README.md', 'CMakeLists.txt', 'Makefile', 'platformio.ini'):
        p = Path(data['root']) / name
        if p.is_file():
            facts.append(f'- `{name}`: available; read directly before changes.')
    categories = {'startup/main loop': r'\bmain\b|user_init|main_loop', 'ISR': r'irq|interrupt|isr',
                  'protection': r'protect|recover|fault', 'SOC': r'soc|capacity', 'storage': r'flash|eeprom|storage',
                  'sleep/wake': r'sleep|wake|suspend', 'protocol/upgrade': r'modbus|uart|can_|ble_|ota|iap'}
    for category, pattern in categories.items():
        matches = [f for f in data['symbols'] if re.search(pattern, f['name'], re.I)]
        facts += ['', '## ' + category, '']
        facts += [f"- `{f['path']}:{f['line']}` `{f['name']}`" for f in matches[:30]] or ['No lexical candidates found.']
    (output / 'AI_CONTEXT.md').write_text('\n'.join(facts) + '\n', encoding='utf-8')
    risk_counts = Counter(r['kind'] for r in data['risks'])
    architecture = ['# Generated architecture inventory', '', data['analysis'], '',
                    'Build membership: ' + data['membership'], '',
                    '## Risk candidates', '', json.dumps(risk_counts, indent=2), '',
                    '## Include dependency graph', '', '```mermaid', 'graph LR']
    # Whole SDK graphs are unreadable. Limit diagram and disclose truncation;
    # complete edges are in architecture.json.
    edges = data['include_edges'][:60]
    nodes = sorted({e[x] for e in edges for x in ('from', 'to')})
    ids = {name: 'n' + str(i) for i, name in enumerate(nodes)}
    architecture += [f'  {ids[name]}["{name}"]' for name in nodes]
    architecture += [f"  {ids[e['from']]} --> {ids[e['to']]}" for e in edges]
    architecture += ['```', '', f"Diagram: first {len(edges)} edges; complete data in architecture.json."]
    (output / 'ARCHITECTURE.md').write_text('\n'.join(architecture) + '\n', encoding='utf-8')
    risk_rows = ''.join('<tr><td>' + html.escape(r['path']) + '</td><td>' + str(r['line']) +
                        '</td><td>' + html.escape(r['kind']) + '</td><td>' + html.escape(r['detail']) +
                        '</td></tr>' for r in data['risks'])
    (output / 'architecture_report.html').write_text('<!doctype html><html><meta charset="utf-8">'
        '<title>Firmware Architecture</title><style>body{font:15px system-ui;margin:30px}td,th{padding:6px;border:1px solid #ccc}'
        'table{border-collapse:collapse}pre{white-space:pre-wrap}</style><h1>Architecture inventory</h1><p>' +
        html.escape(data['analysis']) + '</p><pre>' + html.escape(json.dumps(summary, indent=2)) +
        '</pre><h2>Review candidates</h2><table><tr><th>File</th><th>Line</th><th>Kind</th><th>Symbol</th></tr>' +
        risk_rows + '</table></html>', encoding='utf-8')


def diff(root, before, after):
    root = Path(root).resolve()
    # Resolve revisions to hashes before passing to diff, avoiding option parsing.
    a = git(root, 'rev-parse', '--verify', '--end-of-options', before + '^{commit}')
    b = git(root, 'rev-parse', '--verify', '--end-of-options', after + '^{commit}')
    if not a or not b:
        raise ValueError('both revisions must resolve to commits')
    return {'schema_version': 1, 'before': a, 'after': b,
            'name_status': git(root, 'diff', '--name-status', a, b, '--'),
            'numstat': git(root, 'diff', '--numstat', a, b, '--'),
            'evidence': 'Git file-level diff; no inferred function/safety equivalence'}
