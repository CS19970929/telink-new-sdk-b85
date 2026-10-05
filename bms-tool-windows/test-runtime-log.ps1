$ErrorActionPreference = 'Stop'
$testRoot = Join-Path $env:LOCALAPPDATA ('CodexTemp/bms-tool-windows/runtime-log-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$core = [System.Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'BmsTool.Core/BmsTool.Core.csproj'))
$test = [System.Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'Tests/RuntimeLogTest.cs'))
@"
<Project Sdk="Microsoft.NET.Sdk">
 <PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup>
 <ItemGroup><ProjectReference Include="$core"/><Compile Include="$test"/></ItemGroup>
</Project>
"@ | Set-Content -LiteralPath (Join-Path $testRoot 'RuntimeLogTest.csproj') -Encoding utf8
& dotnet build (Join-Path $testRoot 'RuntimeLogTest.csproj') -c Release --artifacts-path (Join-Path $testRoot 'artifacts') --nologo -v quiet
if ($LASTEXITCODE -ne 0) { throw 'Runtime log test build failed' }
& dotnet (Join-Path $testRoot 'artifacts/bin/RuntimeLogTest/release/RuntimeLogTest.dll')
if ($LASTEXITCODE -ne 0) { throw 'Runtime log tests failed' }
