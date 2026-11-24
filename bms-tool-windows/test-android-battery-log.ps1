$ErrorActionPreference='Stop'
$root=Join-Path $env:TEMP ('CodexTemp/bms-tool-windows/android-battery-log-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
$core=[Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'BmsTool.Core/BmsTool.Core.csproj'))
$program=[Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'Tests/AndroidBatteryLogTest.cs'))
$csv=[Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'BmsTool.Android/BatteryLogCsv.cs'))
@"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup>
  <ItemGroup><ProjectReference Include="$core"/><Compile Include="$program"/><Compile Include="$csv"/></ItemGroup>
</Project>
"@ | Set-Content -LiteralPath (Join-Path $root 'Test.csproj') -Encoding utf8
& dotnet run --project (Join-Path $root 'Test.csproj') -c Release --artifacts-path (Join-Path $root 'artifacts') -p:NuGetAudit=false
if ($LASTEXITCODE -ne 0) { throw 'Android battery data recording regression failed' }
