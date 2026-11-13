$ErrorActionPreference='Stop'
$root=Join-Path $env:TEMP ('CodexTemp/bms-tool-windows/android-qr-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Tests/BluetoothMacCodeTest.cs') -Destination (Join-Path $root 'Program.cs')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'BmsTool.Android/BluetoothMacCode.cs') -Destination $root
$project='<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><PackageReference Include="ZXing.Net" Version="0.16.9"/></ItemGroup></Project>'
$project | Set-Content -LiteralPath (Join-Path $root 'Test.csproj') -Encoding utf8
& dotnet run --project (Join-Path $root 'Test.csproj') -c Release --artifacts-path (Join-Path $root 'artifacts') -p:NuGetAudit=false
if($LASTEXITCODE -ne 0){throw 'Android MAC QR regression failed'}
