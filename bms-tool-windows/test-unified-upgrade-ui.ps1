param(
 [Parameter(Mandatory=$true)][string]$CustomerAssembly,
 [Parameter(Mandatory=$true)][string]$InternalAssembly,
 [Parameter(Mandatory=$true)][string]$C060Package,
 [Parameter(Mandatory=$true)][string]$EvidenceDirectory
)
$ErrorActionPreference='Stop'
$testRoot=Join-Path $env:TEMP ('CodexTemp/bms-tool-windows/unified-ui-'+[Guid]::NewGuid().ToString('N'))
foreach($edition in @(@{Name='customer';Assembly=$CustomerAssembly},@{Name='internal';Assembly=$InternalAssembly})) {
 $root=Join-Path $testRoot $edition.Name
 $evidence=Join-Path $EvidenceDirectory $edition.Name
 New-Item -ItemType Directory -Path $root,$evidence -Force | Out-Null
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Tests/UnifiedUpgradeUiTest.cs') -Destination (Join-Path $root 'Program.cs')
 $assembly=[Security.SecurityElement]::Escape((Resolve-Path -LiteralPath $edition.Assembly).Path)
 @"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0-windows10.0.19041.0</TargetFramework><OutputType>Exe</OutputType><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><Reference Include="ClientUnderTest"><HintPath>$assembly</HintPath></Reference><PackageReference Include="ClosedXML" Version="0.104.2"/><PackageReference Include="System.IO.Ports" Version="8.0.0"/></ItemGroup></Project>
"@ | Set-Content -LiteralPath (Join-Path $root 'Test.csproj') -Encoding utf8
 & dotnet run --project (Join-Path $root 'Test.csproj') -c Release -- $C060Package $evidence
 if($LASTEXITCODE -ne 0){throw "$($edition.Name) unified upgrade UI regression failed"}
}
