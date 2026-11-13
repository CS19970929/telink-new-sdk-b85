param(
 [Parameter(Mandatory=$true)][string]$ClientAssembly,
 [Parameter(Mandatory=$true)][string]$Mac,
 [Parameter(Mandatory=$true)][string]$Baseline,
 [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
 [switch]$WriteSame
)
$ErrorActionPreference='Stop'
$testRoot=Join-Path $env:TEMP ('CodexTemp/bms-tool-windows/ble-module-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Tests/C060BleModuleTest.cs') -Destination (Join-Path $testRoot 'Program.cs')
$assembly=[Security.SecurityElement]::Escape((Resolve-Path -LiteralPath $ClientAssembly).Path)
@"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0-windows10.0.19041.0</TargetFramework><OutputType>Exe</OutputType><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><Reference Include="ClientUnderTest"><HintPath>$assembly</HintPath></Reference><PackageReference Include="System.IO.Ports" Version="8.0.0"/></ItemGroup></Project>
"@ | Set-Content -LiteralPath (Join-Path $testRoot 'Test.csproj') -Encoding utf8
$arguments=@($Mac,$EvidenceDirectory,$Baseline)
if($WriteSame){$arguments+='--write-same'}
& dotnet run --project (Join-Path $testRoot 'Test.csproj') -c Release -- @arguments
if($LASTEXITCODE -ne 0){throw 'C060 BLE module verification failed'}
