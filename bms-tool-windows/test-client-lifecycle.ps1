param([string]$CustomerAssembly,[string]$InternalAssembly,[string]$OutputDirectory)
$ErrorActionPreference='Stop'
if(-not $CustomerAssembly -and -not $InternalAssembly){throw 'Provide at least one actual client assembly.'}
if(-not $OutputDirectory){$OutputDirectory=Join-Path $env:LOCALAPPDATA ('CodexTemp/bms-tool-windows/lifecycle-'+[Guid]::NewGuid().ToString('N'))}
$destination=[IO.Path]::GetFullPath($OutputDirectory)
$sourceRoot=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
if($destination.Equals($sourceRoot,[StringComparison]::OrdinalIgnoreCase) -or $destination.StartsWith($sourceRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Test output must be outside the source tree.'}
foreach($edition in @(@{Name='customer';Assembly=$CustomerAssembly},@{Name='internal';Assembly=$InternalAssembly})) {
    if(-not $edition.Assembly){continue}
    $root=Join-Path $destination $edition.Name
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Tests/autonomous/ClientLifecycleTest.cs') -Destination (Join-Path $root 'Program.cs')
    $assembly=[Security.SecurityElement]::Escape((Resolve-Path -LiteralPath $edition.Assembly).Path)
    @"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0-windows10.0.19041.0</TargetFramework><OutputType>Exe</OutputType><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><Reference Include="BmsUnderTest"><HintPath>$assembly</HintPath></Reference><PackageReference Include="ClosedXML" Version="0.104.2"/><PackageReference Include="System.IO.Ports" Version="8.0.0"/></ItemGroup></Project>
"@ | Set-Content -LiteralPath (Join-Path $root 'Test.csproj') -Encoding utf8
    dotnet run --project (Join-Path $root 'Test.csproj') -c Release -p:NuGetAudit=false 2>&1 | Tee-Object -FilePath (Join-Path $root 'result.log')
    if($LASTEXITCODE -ne 0){throw ($edition.Name+' lifecycle integration failed')}
}
