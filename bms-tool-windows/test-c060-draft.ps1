$ErrorActionPreference='Stop'
$root=Join-Path $env:TEMP ('CodexTemp/bms-tool-windows/c060-draft-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Tests/C060DraftTest.cs') -Destination (Join-Path $root 'Program.cs')
$core=[Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'BmsTool.Core/BmsTool.Core.csproj'))
@"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><ProjectReference Include="$core"/></ItemGroup></Project>
"@ | Set-Content -LiteralPath (Join-Path $root 'Test.csproj') -Encoding utf8
& dotnet run --project (Join-Path $root 'Test.csproj') -c Release --artifacts-path (Join-Path $root 'artifacts')
if($LASTEXITCODE -ne 0){throw 'C060 draft regression failed'}
