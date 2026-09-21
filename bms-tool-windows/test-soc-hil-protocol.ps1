$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$tempBase = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA "CodexTemp\bms-tool-windows"))
$testRoot = [IO.Path]::GetFullPath((Join-Path $tempBase ("soc-hil-protocol-" + [Guid]::NewGuid().ToString("N"))))
if (-not $testRoot.StartsWith($tempBase + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing unsafe test directory: $testRoot"
}
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
try {
    Copy-Item -LiteralPath (Join-Path $root "BmsTool.Windows\ModbusRtu.cs") -Destination $testRoot
    Copy-Item -LiteralPath (Join-Path $root "Shared\BmsClient.SocHil.cs") -Destination $testRoot
    Copy-Item -LiteralPath (Join-Path $root "Tests\SocHilProtocolTest.cs") -Destination $testRoot
    @'
namespace BmsTool.Windows;
public sealed partial class BmsClient
{
    private Task<byte[]> TransactAsync(byte[] request, CancellationToken ct, TimeSpan? timeout = null) =>
        Task.FromException<byte[]>(new NotSupportedException());
}
'@ | Set-Content -LiteralPath (Join-Path $testRoot "BmsClientStub.cs") -Encoding utf8
    '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><StartupObject>SocHilProtocolTest</StartupObject></PropertyGroup></Project>' |
        Set-Content -LiteralPath (Join-Path $testRoot "Test.csproj") -Encoding utf8
    & dotnet run --project (Join-Path $testRoot "Test.csproj") -c Release
    if ($LASTEXITCODE -ne 0) { throw "SOC HIL protocol tests failed" }
} finally {
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}
