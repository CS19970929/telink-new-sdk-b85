param(
    [Parameter(Mandatory=$true)][string]$CustomerAssembly,
    [Parameter(Mandatory=$true)][string]$InternalAssembly,
    [string]$EvidenceDirectory
)
$ErrorActionPreference='Stop'
$testRoot=Join-Path $env:LOCALAPPDATA ('CodexTemp/bms-tool-windows/business-parameters-ui-'+[Guid]::NewGuid().ToString('N'))
if(-not $EvidenceDirectory){$EvidenceDirectory=Join-Path $testRoot 'evidence'}
foreach($edition in @(@{Name='customer';Assembly=$CustomerAssembly},@{Name='internal';Assembly=$InternalAssembly})){
    $assemblyPath=(Resolve-Path -LiteralPath $edition.Assembly).Path
    $editionRoot=Join-Path $testRoot $edition.Name
    $editionEvidence=Join-Path $EvidenceDirectory $edition.Name
    New-Item -ItemType Directory -Path $editionRoot,$editionEvidence -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Tests/BusinessParametersUiTest.cs') -Destination (Join-Path $editionRoot 'Program.cs')
    $escapedAssembly=[Security.SecurityElement]::Escape($assemblyPath)
    $project=@"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0-windows10.0.19041.0</TargetFramework><OutputType>Exe</OutputType><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><Reference Include="BmsUnderTest"><HintPath>$escapedAssembly</HintPath></Reference><PackageReference Include="ClosedXML" Version="0.104.2"/><PackageReference Include="System.IO.Ports" Version="8.0.0"/></ItemGroup></Project>
"@
    Set-Content -LiteralPath (Join-Path $editionRoot 'Test.csproj') -Value $project -Encoding utf8
    & dotnet run --project (Join-Path $editionRoot 'Test.csproj') -c Release -- $editionEvidence | Tee-Object -FilePath (Join-Path $editionEvidence 'ui.log')
    if($LASTEXITCODE -ne 0){throw "$($edition.Name) business parameters UI failed"}
}
