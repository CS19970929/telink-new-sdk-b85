$ErrorActionPreference = "Stop"
$testRoot = Join-Path $env:LOCALAPPDATA ("CodexTemp/bms-tool-windows/d008-parameters-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
foreach ($file in @("Shared/D008Parameters.cs", "Shared/BmsClient.Parameters.cs", "Tests/D008ParametersTest.cs")) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $testRoot
}
'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup></Project>' | Set-Content -LiteralPath (Join-Path $testRoot 'Test.csproj') -Encoding utf8
& dotnet run --project (Join-Path $testRoot 'Test.csproj') -c Release
if ($LASTEXITCODE -ne 0) { throw 'D008 parameter tests failed' }
