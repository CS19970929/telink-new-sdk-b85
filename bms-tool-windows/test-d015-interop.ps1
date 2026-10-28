param(
 [Parameter(Mandatory=$true)][string]$CustomerAssembly,
 [Parameter(Mandatory=$true)][string]$InternalAssembly,
 [Parameter(Mandatory=$true)][string]$FirmwareHarness,
 [Parameter(Mandatory=$true)][string]$EvidenceDirectory
)
$ErrorActionPreference='Stop'
$testRoot=Join-Path $env:LOCALAPPDATA ('CodexTemp/bms-tool-windows/d015-interop-'+[Guid]::NewGuid().ToString('N'))
foreach($edition in @(@{Name='customer';Assembly=$CustomerAssembly},@{Name='internal';Assembly=$InternalAssembly})) {
 $root=Join-Path $testRoot $edition.Name
 New-Item -ItemType Directory -Path $root,$EvidenceDirectory -Force | Out-Null
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Tests/D015FirmwareInteropTest.cs') -Destination (Join-Path $root 'Program.cs')
 $assembly=[Security.SecurityElement]::Escape((Resolve-Path -LiteralPath $edition.Assembly).Path)
 @"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0-windows10.0.19041.0</TargetFramework><OutputType>Exe</OutputType><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><Reference Include="BmsUnderTest"><HintPath>$assembly</HintPath></Reference><PackageReference Include="ClosedXML" Version="0.104.2"/><PackageReference Include="System.IO.Ports" Version="8.0.0"/></ItemGroup></Project>
"@ | Set-Content -LiteralPath (Join-Path $root 'Test.csproj') -Encoding utf8
 & dotnet run --project (Join-Path $root 'Test.csproj') -c Release -- $FirmwareHarness 2>&1 | Tee-Object -FilePath (Join-Path $EvidenceDirectory ($edition.Name+'-d015-interop.log'))
 if($LASTEXITCODE -ne 0){throw "$($edition.Name) D015 interop failed"}
}
