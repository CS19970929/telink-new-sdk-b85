param(
    [Parameter(Mandatory=$true)][string]$CustomerAssembly,
    [Parameter(Mandatory=$true)][string]$InternalAssembly
)
$ErrorActionPreference = 'Stop'
$testRoot = Join-Path $env:LOCALAPPDATA ('CodexTemp/bms-tool-windows/owc-ui-' + [Guid]::NewGuid().ToString('N'))
foreach ($edition in @(@{Name='customer';Assembly=$CustomerAssembly}, @{Name='internal';Assembly=$InternalAssembly})) {
    $assembly = [Security.SecurityElement]::Escape((Resolve-Path -LiteralPath $edition.Assembly).Path)
    $tests = [Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'Tests/OwcWindowTest.cs'))
    $editionRoot = Join-Path $testRoot $edition.Name
    New-Item -ItemType Directory -Path $editionRoot -Force | Out-Null
    $project = @"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><TargetFramework>net8.0-windows10.0.19041.0</TargetFramework><OutputType>Exe</OutputType><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup>
  <ItemGroup><Reference Include="BmsUnderTest"><HintPath>$assembly</HintPath></Reference><Compile Include="$tests" Link="Program.cs"/><PackageReference Include="ClosedXML" Version="0.104.2"/><PackageReference Include="System.IO.Ports" Version="8.0.0"/></ItemGroup>
</Project>
"@
    $projectPath = Join-Path $editionRoot 'OwcUiTest.csproj'
    Set-Content -LiteralPath $projectPath -Value $project -Encoding utf8
    & dotnet run --project $projectPath -c Release -- $editionRoot
    if ($LASTEXITCODE -ne 0) { throw "OWC WPF check failed: $($edition.Name)" }
}
Write-Host "UI fixture evidence: $testRoot"
