$ErrorActionPreference = 'Stop'
$testRoot = Join-Path $env:LOCALAPPDATA ('CodexTemp/bms-tool-windows/afe-registers-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
foreach ($file in @('BmsFactoryTest.Windows/AfeRegisterCatalog.cs', 'BmsFactoryTest.Windows/BmsClient.AfeRegisters.cs', 'BmsTool.Windows/ModbusRtu.cs', 'Tests/AfeRegisterProtocolTest.cs', 'BmsFactoryTest.Windows/afe-register-catalog.json')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $testRoot
}
'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><EmbeddedResource Include="afe-register-catalog.json" LogicalName="BmsFactoryTest.AfeRegisterCatalog" /></ItemGroup></Project>' | Set-Content -LiteralPath (Join-Path $testRoot 'Test.csproj') -Encoding utf8
& dotnet run --project (Join-Path $testRoot 'Test.csproj') -c Release
if ($LASTEXITCODE -ne 0) { throw 'AFE register protocol tests failed' }
