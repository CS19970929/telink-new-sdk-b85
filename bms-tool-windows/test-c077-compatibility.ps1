$ErrorActionPreference='Stop'
$testRoot=Join-Path $env:TEMP ('CodexTemp/bms-tool-windows/c077-contract-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Tests/C077CompatibilityTest.cs') -Destination (Join-Path $testRoot 'Program.cs')
$existing=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Tests/C060CompatibilityTest.cs') -Raw
$peer=$existing.Substring($existing.IndexOf('internal sealed class Peer'))
[IO.File]::WriteAllText((Join-Path $testRoot 'Peer.cs'),"using BmsTool.Windows;`nusing System.Text;`nusing System.Diagnostics;`n"+$peer,[Text.UTF8Encoding]::new($false))
$core=[Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'BmsTool.Core/BmsTool.Core.csproj'))
@"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><ProjectReference Include="$core"/></ItemGroup></Project>
"@ | Set-Content -LiteralPath (Join-Path $testRoot 'Test.csproj') -Encoding utf8
& dotnet run --project (Join-Path $testRoot 'Test.csproj') -c Release --artifacts-path (Join-Path $testRoot 'artifacts')
if($LASTEXITCODE -ne 0){throw 'C077 compatibility failed'}
