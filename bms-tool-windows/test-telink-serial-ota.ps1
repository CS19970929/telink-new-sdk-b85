$ErrorActionPreference = 'Stop'
$testRoot = Join-Path $env:LOCALAPPDATA ('CodexTemp/bms-tool-windows/serial-ota-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
foreach ($file in @('BmsTool.Windows/BmsTransport.cs', 'BmsTool.Windows/ModbusRtu.cs',
    'BmsTool.Windows/TelinkOtaProtocol.cs', 'Shared/BmsRuntime.cs', 'Shared/TelinkOtaCore.cs',
    'Shared/TelinkSerialOta.cs', 'Tests/TelinkSerialOtaTest.cs')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $testRoot
}
'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup></Project>' |
    Set-Content -LiteralPath (Join-Path $testRoot 'Test.csproj') -Encoding utf8
dotnet run --project (Join-Path $testRoot 'Test.csproj') -c Release
if ($LASTEXITCODE -ne 0) { throw 'Telink serial OTA tests failed' }
