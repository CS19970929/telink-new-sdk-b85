$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$testRoot = Join-Path $env:LOCALAPPDATA ('CodexTemp/bms-tool-windows/afe-fragments-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
foreach ($file in @('Tests/AfeFragmentsTest.cs')) {
    Copy-Item -LiteralPath (Join-Path $root $file) -Destination $testRoot
}
# Compile the same complete source set as production Core, including partial
# BmsClient implementations. Unit checks intentionally access internal helpers.
$coreDirectory = Join-Path $root 'BmsTool.Core'
[xml]$core = Get-Content -LiteralPath (Join-Path $coreDirectory 'BmsTool.Core.csproj') -Raw
$includes = foreach ($item in $core.Project.ItemGroup.Compile) {
    if ($item.Include) {
        $path = [Security.SecurityElement]::Escape([IO.Path]::GetFullPath((Join-Path $coreDirectory $item.Include)))
        '<Compile Include="' + $path + '" />'
    }
}
$project = '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup>' + ($includes -join '') + '</ItemGroup></Project>'
$project | Set-Content -LiteralPath (Join-Path $testRoot 'Test.csproj') -Encoding utf8
& dotnet run --project (Join-Path $testRoot 'Test.csproj') -c Release --artifacts-path (Join-Path $testRoot 'artifacts') -p:NuGetAudit=false
if ($LASTEXITCODE -ne 0) { throw 'afe-fragments tests failed' }