$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$testRoot = Join-Path $env:LOCALAPPDATA ("CodexTemp/bms-tool-windows/diagnostics-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
foreach ($file in @("Shared/BmsDiagnostics.cs", "Shared/BmsHealth.cs", "Shared/BmsTestEngine.cs", "Shared/BmsClient.Diagnostics.cs", "Shared/D008Parameters.cs", "Shared/Sh3673520Parameters.cs", "BmsTool.Windows/BmsClient.cs", "BmsTool.Windows/BmsTransport.cs", "BmsTool.Windows/ModbusRtu.cs", "Tests/DiagnosticsTest.cs")) {
    Copy-Item -LiteralPath (Join-Path $root $file) -Destination $testRoot
}
'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup></Project>' | Set-Content -LiteralPath (Join-Path $testRoot 'Test.csproj') -Encoding utf8
& dotnet run --project (Join-Path $testRoot 'Test.csproj') -c Release
if ($LASTEXITCODE -ne 0) { throw 'Diagnostics tests failed' }
