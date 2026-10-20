$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$testRoot = Join-Path $env:LOCALAPPDATA ('CodexTemp\bms-soc-profile-test\' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'Shared\BmsClient.SocProfile.cs') -Destination $testRoot
Copy-Item -LiteralPath (Join-Path $root 'Tests\SocProfilePolicyTest.cs') -Destination $testRoot
'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><StartupObject>SocProfilePolicyTest</StartupObject></PropertyGroup></Project>' |
    Set-Content -LiteralPath (Join-Path $testRoot 'SocProfilePolicyTest.csproj') -Encoding utf8
dotnet run --project (Join-Path $testRoot 'SocProfilePolicyTest.csproj') --configuration Release
if ($LASTEXITCODE -ne 0) { throw "SOC模型回归失败，夹具保留在 $testRoot" }
