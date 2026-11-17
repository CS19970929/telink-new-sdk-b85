param(
 [Parameter(Mandatory=$true)][string]$ClientAssembly,
 [Parameter(Mandatory=$true)][string]$Endpoint,
 [Parameter(Mandatory=$true)][string]$ExpectedBuildId,
 [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
 [switch]$TestWriteRestore
)
$ErrorActionPreference='Stop'
if(-not $TestWriteRestore){throw '实板测试会暂改offset 1mA并恢复，必须显式指定-TestWriteRestore。'}
if(Test-Path -LiteralPath $EvidenceDirectory){throw '证据目录已存在，请使用新目录。'}
$root=Join-Path $env:LOCALAPPDATA ('CodexTemp/bms-tool-windows/c077-current-board-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force|Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Tests/C077CalibrationBoardTest.cs') -Destination (Join-Path $root 'Program.cs')
$assembly=[Security.SecurityElement]::Escape((Resolve-Path -LiteralPath $ClientAssembly).Path)
@"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0-windows10.0.19041.0</TargetFramework><OutputType>Exe</OutputType><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><Reference Include="ClientUnderTest"><HintPath>$assembly</HintPath></Reference><PackageReference Include="System.IO.Ports" Version="8.0.0"/></ItemGroup></Project>
"@|Set-Content -LiteralPath (Join-Path $root 'Test.csproj') -Encoding utf8
& dotnet run --project (Join-Path $root 'Test.csproj') -c Release --artifacts-path (Join-Path $root 'artifacts') -- $Endpoint $ExpectedBuildId $EvidenceDirectory '--test-write-restore'
if($LASTEXITCODE -ne 0){throw 'C077实板校准未完成全部确认，不自动重试。'}
