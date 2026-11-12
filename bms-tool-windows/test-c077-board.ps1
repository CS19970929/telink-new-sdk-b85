param(
    [string]$Port='COM18',
    [ValidateSet('read','zero','write','soc','runtime','idlewrite','controls','deep')][string]$Phase='read',
    [string]$OutputDirectory,
    [switch]$Lab13sNoLoad
)
$ErrorActionPreference='Stop'
# Writes intentionally exercise real protection and persistent configuration.
# This fixture is only for the authorized 13S simulator with no charger/load.
if($Phase -ne 'read' -and -not $Lab13sNoLoad){throw '写测试仅用于13串模拟器、无充电器/负载的台架；核实现场后显式指定 -Lab13sNoLoad。'}
if([string]::IsNullOrWhiteSpace($OutputDirectory)){
    $OutputDirectory=Join-Path $env:LOCALAPPDATA ('CodexTemp/c077/host-board-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+$Phase)
}
if(Test-Path -LiteralPath $OutputDirectory){throw '证据目录已存在；请使用新目录，避免覆盖基线。'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if($OutputDirectory.StartsWith($repoRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw '临时证据不得位于工作树。'}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$testRoot=Join-Path $env:TEMP ('CodexTemp/c077/host-runner-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Tests/C077BoardAcceptance.cs') -Destination (Join-Path $testRoot 'Program.cs')
$core=[Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'BmsTool.Core/BmsTool.Core.csproj'))
$serial=[Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'BmsTool.Windows/BmsSerialTransport.cs'))
@"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><ProjectReference Include="$core"/><Compile Include="$serial" Link="BmsSerialTransport.cs"/><PackageReference Include="System.IO.Ports" Version="8.0.0"/></ItemGroup></Project>
"@ | Set-Content -LiteralPath (Join-Path $testRoot 'Test.csproj') -Encoding utf8
& dotnet run --project (Join-Path $testRoot 'Test.csproj') -c Release --artifacts-path (Join-Path $testRoot 'artifacts') -- $Phase $OutputDirectory $Port
$testExit=$LASTEXITCODE
Write-Host "证据：$OutputDirectory"
if($testExit -ne 0){throw "C077实板验收失败，退出码$testExit；先看report.json与wire.log，不自动重试写入。"}
