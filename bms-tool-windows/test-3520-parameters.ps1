$ErrorActionPreference="Stop"
$root=Split-Path -Parent $MyInvocation.MyCommand.Path
$testRoot=Join-Path $env:LOCALAPPDATA ("CodexTemp\bms-tool-windows\codec-tests-"+[Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$model=[System.Security.SecurityElement]::Escape((Join-Path $root "Shared\Sh3673520Parameters.cs"))
$test=[System.Security.SecurityElement]::Escape((Join-Path $root "Tests\Sh3520ParametersTest.cs"))
@"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net7.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><Compile Include="$model"/><Compile Include="$test"/></ItemGroup></Project>
"@ | Set-Content -LiteralPath (Join-Path $testRoot "Test.csproj") -Encoding UTF8
& dotnet run --project (Join-Path $testRoot "Test.csproj") -c Release
if($LASTEXITCODE -ne 0) {throw "3520 codec tests failed"}
