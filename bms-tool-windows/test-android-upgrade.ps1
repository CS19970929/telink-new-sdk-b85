param([Parameter(Mandatory=$true)][string]$C060Package)
$ErrorActionPreference='Stop'
$root=Join-Path $env:TEMP ('CodexTemp/bms-tool-windows/android-upgrade-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Tests/AndroidUpgradeTest.cs') -Destination (Join-Path $root 'Program.cs')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'BmsTool.Android/AndroidFirmwareInbox.cs') -Destination $root
# Compile the actual Android endpoint guard, with inert state instead of GATT.
$text=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'BmsTool.Android/MainActivity.SerialUpgrade.cs') -Raw
$start=$text.IndexOf('    private void CheckUpgradeEndpoint(')
$open=$text.IndexOf('{',$start);$depth=1;$end=$open+1
while($depth -gt 0){if($text[$end] -eq '{'){$depth++};if($text[$end] -eq '}'){$depth--};$end++}
$method=$text.Substring($start,$end-$start)
('using System.Text.RegularExpressions; partial class EndpointState {'+$method+'}') | Set-Content -LiteralPath (Join-Path $root 'Endpoint.cs') -Encoding utf8
$core=[Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'BmsTool.Core/BmsTool.Core.csproj'))
@"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><ProjectReference Include="$core"/></ItemGroup></Project>
"@ | Set-Content -LiteralPath (Join-Path $root 'Test.csproj') -Encoding utf8
& dotnet run --project (Join-Path $root 'Test.csproj') -c Release --artifacts-path (Join-Path $root 'artifacts') -- (Join-Path $root 'fixtures') $C060Package
if($LASTEXITCODE -ne 0){throw 'Android firmware routing/endpoint regression failed'}
