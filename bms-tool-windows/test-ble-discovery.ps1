param([string]$OutputDirectory=(Join-Path $env:LOCALAPPDATA ('CodexTemp/bms-tool-windows/ble-discovery-'+[Guid]::NewGuid().ToString('N'))))
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath($OutputDirectory)
$source=[IO.Path]::GetFullPath($PSScriptRoot)+[IO.Path]::DirectorySeparatorChar
if($root.StartsWith($source,[StringComparison]::OrdinalIgnoreCase)){throw '测试输出必须位于源码树外'}
New-Item -ItemType Directory -Path $root -Force | Out-Null
$items=@('Tests/BleDiscoveryTest.cs','Shared/BleDiscovery.cs','Shared/LatestConnectionOperation.cs') | ForEach-Object {
    '<Compile Include="'+[Security.SecurityElement]::Escape((Join-Path $PSScriptRoot $_))+'" />'
}
$project='<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup><ItemGroup>'+($items -join '')+'</ItemGroup></Project>'
Set-Content -LiteralPath (Join-Path $root 'BleDiscoveryTest.csproj') -Value $project -Encoding utf8
& dotnet run --project (Join-Path $root 'BleDiscoveryTest.csproj') -c Release -p:NuGetAudit=false
if($LASTEXITCODE -ne 0){throw '蓝牙发现与连接串行测试失败'}
