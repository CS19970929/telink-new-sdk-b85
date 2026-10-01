"""Run actual WPF AFE handlers + protocol with fake transport; no device or window.

Copies production inputs into an external test project, replacing only modal
MessageBox with a recorded answer. DisposeBmsAsync is extracted verbatim from
each actual MainWindow (sha256 receipt records all input files and extraction).
No UI/controller/protocol behavior is reimplemented by the fixture.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import re
import os

BASE = Path(__file__).resolve().parents[2]


def method(source, name):
    match = re.search(r'^    private (?:async )?[^\n]+\b' + name + r'\(', source, re.M)
    if not match:
        raise ValueError('Missing production method: ' + name)
    start = match.start()
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--variant', choices=['Windows', 'Factory', 'both'], default='both')
    args = parser.parse_args()
    out = args.out.resolve()
    if out.is_relative_to(BASE.parent.resolve()):
        raise ValueError('Test output must be outside repository')
    results = []
    variants = ['Windows', 'Factory'] if args.variant == 'both' else [args.variant]
    for variant in variants:
        folder = 'BmsTool.Windows' if variant == 'Windows' else 'BmsFactoryTest.Windows'
        destination = out / variant
        destination.mkdir(parents=True, exist_ok=True)
        inputs = {}
        files = [
            'Shared/BmsDiagnostics.cs', 'Shared/BmsHealth.cs', 'Shared/BmsClient.Diagnostics.cs',
            'Shared/BmsClient.ParameterRead.cs', 'Shared/D008Parameters.cs', 'Shared/Sh3673520Parameters.cs',
            'BmsTool.Windows/BmsClient.cs', 'BmsTool.Windows/BmsTransport.cs',
            'BmsTool.Windows/ModbusRtu.cs', 'BmsTool.Windows/BmsClient.AfeHardware.cs',
            folder + '/ProtectionParameter.cs', folder + '/AfeHardwareParameters.cs', folder + '/MainWindow.AfeHardwareUi.cs',
            'Tests/autonomous/ClientLifecycleTest.cs']
        optional = BASE / 'Shared/MainWindow.ConnectionState.cs'
        if optional.exists():
            files.append('Shared/MainWindow.ConnectionState.cs')
        for filename in files:
            raw = (BASE / filename).read_bytes()
            inputs[filename] = hashlib.sha256(raw).hexdigest()
            text = raw.decode('utf-8-sig')
            if filename.endswith('MainWindow.AfeHardwareUi.cs'):
                text = 'using MessageBox = BmsTool.Windows.TestMessageBox;\n' + text
            (destination / Path(filename).name).write_text(text, encoding='utf-8')
        main_path = BASE / folder / 'MainWindow.xaml.cs'
        raw = main_path.read_bytes()
        extracted = '\n'.join(method(raw.decode('utf-8-sig'), name) for name in [
            'DisposeBmsAsync', 'ConnectBmsInternalAsync', 'ConnectSerialInternalAsync',
            'OnConnectionProgress', 'RefreshBatteryAsync', 'ReadProtection_Click', 'ReadProtectionAsync'])
        parameter_raw = (BASE / 'Shared/MainWindow.Parameters.cs').read_bytes()
        inputs['Shared/MainWindow.Parameters.cs'] = hashlib.sha256(parameter_raw).hexdigest()
        extracted += '\n' + method(parameter_raw.decode('utf-8-sig'), 'ParameterRunAsync')
        inputs[folder + '/MainWindow.xaml.cs'] = hashlib.sha256(raw).hexdigest()
        inputs['extracted connection and battery methods'] = hashlib.sha256(extracted.encode()).hexdigest()
        (destination / 'Lifecycle.partial.cs').write_text(
            'using System.Diagnostics; using System.IO; using System.Windows; namespace BmsTool.Windows; public partial class MainWindow {\n' + extracted + '\n}', encoding='utf-8')
        (destination / 'Test.csproj').write_text('''<Project Sdk="Microsoft.NET.Sdk">
<PropertyGroup><TargetFramework>net8.0-windows</TargetFramework><OutputType>Exe</OutputType>
<UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable>
<DefineConstants>''' + ('BMS_FACTORY_APP' if variant == 'Factory' else '') + '''</DefineConstants>
</PropertyGroup></Project>''', encoding='utf-8')
        process = subprocess.run(['dotnet', 'run', '--project', str(destination / 'Test.csproj'),
                                  '-c', 'Release', '--', '--enable-afe-hw-editor'],
                                 env={**os.environ, 'DOTNET_CLI_UI_LANGUAGE': 'en-US'},
                                 capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
        (destination / 'result.txt').write_text(process.stdout + process.stderr, encoding='utf-8')
        result = dict(variant=variant, exit_code=process.returncode, source_sha256=inputs,
                      output=process.stdout + process.stderr)
        results.append(result)
        print(variant + ':\n' + result['output'])
    (out / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    raise SystemExit(1 if any(r['exit_code'] for r in results) else 0)


if __name__ == '__main__':
    main()
