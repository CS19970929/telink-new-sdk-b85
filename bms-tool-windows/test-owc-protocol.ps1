param()
$ErrorActionPreference = 'Stop'
$testRoot = Join-Path $env:LOCALAPPDATA ('CodexTemp/bms-tool-windows/owc-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$protocol = [Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'Shared/CommToolOwcProtocol.cs'))
$client = [Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'Shared/CommToolOwcClient.cs'))
$tests = [Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'Tests/OwcProtocolTest.cs'))
$project = @"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup>
  <ItemGroup><Compile Include="$protocol" Link="CommToolOwcProtocol.cs"/><Compile Include="$client" Link="CommToolOwcClient.cs"/><PackageReference Include="System.IO.Ports" Version="8.0.0"/><Compile Include="$tests" Link="Program.cs"/></ItemGroup>
</Project>
"@
Set-Content -LiteralPath (Join-Path $testRoot 'OwcTest.csproj') -Value $project -Encoding utf8
& dotnet run --project (Join-Path $testRoot 'OwcTest.csproj') -c Release
if ($LASTEXITCODE -ne 0) { throw 'OWC protocol check failed' }
