$ErrorActionPreference = 'Stop'
$testRoot = Join-Path $env:LOCALAPPDATA ('CodexTemp/bms-tool-windows/mos-control-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$core = [Security.SecurityElement]::Escape((Resolve-Path -LiteralPath (Join-Path $PSScriptRoot 'BmsTool.Core/BmsTool.Core.csproj')).Path)
$test = [Security.SecurityElement]::Escape((Resolve-Path -LiteralPath (Join-Path $PSScriptRoot 'Tests/MosControlTest.cs')).Path)
$project = @"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup>
  <ItemGroup><ProjectReference Include="$core"/><Compile Include="$test" Link="Program.cs"/></ItemGroup>
</Project>
"@
Set-Content -LiteralPath (Join-Path $testRoot 'MosControlTest.csproj') -Value $project -Encoding utf8
& dotnet run --project (Join-Path $testRoot 'MosControlTest.csproj') -c Release --artifacts-path (Join-Path $testRoot 'artifacts')
if ($LASTEXITCODE -ne 0) { throw "MOS control test failed with exit code $LASTEXITCODE" }
