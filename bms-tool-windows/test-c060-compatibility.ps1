param([Parameter(Mandatory=$true)][string]$ClientAssembly)
$ErrorActionPreference='Stop'
$testRoot=Join-Path $env:TEMP ('CodexTemp/bms-tool-windows/c060-contract-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Tests/C060CompatibilityTest.cs') -Destination (Join-Path $testRoot 'Program.cs')
$assembly=[Security.SecurityElement]::Escape((Resolve-Path -LiteralPath $ClientAssembly).Path)
@"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0-windows10.0.19041.0</TargetFramework><OutputType>Exe</OutputType><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><Reference Include="ClientUnderTest"><HintPath>$assembly</HintPath></Reference></ItemGroup></Project>
"@ | Set-Content -LiteralPath (Join-Path $testRoot 'Test.csproj') -Encoding utf8
'<configuration><packageSources><clear /></packageSources></configuration>' | Set-Content -LiteralPath (Join-Path $testRoot 'NuGet.Config') -Encoding utf8
& dotnet run --project (Join-Path $testRoot 'Test.csproj') -c Release
if($LASTEXITCODE -ne 0){throw 'C060 compatibility failed'}
