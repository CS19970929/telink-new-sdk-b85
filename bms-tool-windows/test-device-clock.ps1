param(
 [Parameter(Mandatory=$true)][string]$CustomerAssembly,
 [Parameter(Mandatory=$true)][string]$InternalAssembly,
 [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
 [string]$FirmwareHarness
)
$ErrorActionPreference='Stop'
$testRoot=Join-Path $env:LOCALAPPDATA ('CodexTemp/bms-tool-windows/device-clock-'+[Guid]::NewGuid().ToString('N'))
foreach($edition in @(@{Name='customer';Assembly=$CustomerAssembly},@{Name='internal';Assembly=$InternalAssembly})) {
 $root=Join-Path $testRoot $edition.Name
 New-Item -ItemType Directory -Path $root,$EvidenceDirectory -Force | Out-Null
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Tests/DeviceClockTest.cs') -Destination (Join-Path $root 'Program.cs')
 $interop=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'Tests/D015FirmwareInteropTest.cs') -Raw
 $fixture=$interop.Substring($interop.IndexOf('sealed class FirmwareTransport')).Split('sealed class IapTransport')[0]
 ('using BmsTool.Windows; using System.Diagnostics; using System.IO;' + [Environment]::NewLine + $fixture) | Set-Content -LiteralPath (Join-Path $root 'FirmwareTransport.cs') -Encoding utf8
 $assembly=[Security.SecurityElement]::Escape((Resolve-Path -LiteralPath $edition.Assembly).Path)
 @"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0-windows10.0.19041.0</TargetFramework><OutputType>Exe</OutputType><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><Reference Include="BmsUnderTest"><HintPath>$assembly</HintPath></Reference><PackageReference Include="ClosedXML" Version="0.104.2"/><PackageReference Include="System.IO.Ports" Version="8.0.0"/></ItemGroup></Project>
"@ | Set-Content -LiteralPath (Join-Path $root 'Test.csproj') -Encoding utf8
 $runArguments=@('run','--project',(Join-Path $root 'Test.csproj'),'-c','Release')
 if(-not [string]::IsNullOrWhiteSpace($FirmwareHarness)){$runArguments+=@('--',$FirmwareHarness)}
 & dotnet @runArguments 2>&1 | Tee-Object -FilePath (Join-Path $EvidenceDirectory ($edition.Name+'-device-clock.log'))
 if($LASTEXITCODE -ne 0){throw "$($edition.Name) device clock failed"}
}
