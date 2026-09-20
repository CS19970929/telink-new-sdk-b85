$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$customer = Get-Content -LiteralPath (Join-Path $root "BmsTool.Windows\MainWindow.EventLogUi.cs") -Raw
$factory = Get-Content -LiteralPath (Join-Path $root "BmsFactoryTest.Windows\MainWindow.EventLogUi.cs") -Raw

foreach ($source in @($customer, $factory)) {
    if (-not $source.Contains('private const ushort EventLogRegister = 0xC008;')) { throw "事件日志起始寄存器必须为 0xC008" }
    if (-not $source.Contains('private const ushort EventLogCount = 100;')) { throw "D008/D011/D013 事件日志必须固定读取 100 条" }
    if (-not $source.Contains('ReadRegistersAsync(EventLogRegister, EventLogCount)')) { throw "事件日志必须直接读取现有 0xC008/100 协议" }
    if ($source.Contains('int count = 500;') -or $source.Contains('0xC008 + offset')) { throw "不得恢复 500 条自动探测/分页逻辑" }
}

if ($customer.Contains('DeleteEventLogs') -or $customer.Contains('ResetEventRecord')) { throw "客户版不能接入日志删除" }
if (-not $factory.Contains('deleteButton.Click += DeleteEventLogs_Click;')) { throw "完整版缺少删除入口" }
if (-not $factory.Contains('private const ushort ResetEventRecordRegister = 0x1007;')) { throw "完整版缺少日志删除寄存器" }
if (-not $factory.Contains('private const ushort ResetEventRecordValue = 0x0001;')) { throw "完整版缺少日志删除值" }

$testRoot = Join-Path $env:LOCALAPPDATA ("CodexTemp\bms-tool-windows\event-log-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root "BmsTool.Windows\ModbusRtu.cs") -Destination $testRoot
@'
using BmsTool.Windows;
using System;
static class Test {
    static void Check(bool ok) { if(!ok) throw new Exception("Assertion failed"); }
    static void Main() {
        byte[] read = ModbusRtu.ReadHolding(0xC008, 100);
        Check(Convert.ToHexString(read) == "0103C0080064F9E3");
        byte[] delete = ModbusRtu.WriteSingle(0x1007, 0x0001);
        ModbusRtu.ValidateWriteSingleAck(delete, 0x1007, 0x0001);
        Console.WriteLine("PASS: Windows event log uses existing C008/100 protocol only; delete command remains valid in internal build");
    }
}
'@ | Set-Content -LiteralPath (Join-Path $testRoot "Test.cs") -Encoding UTF8
'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings></PropertyGroup></Project>' | Set-Content -LiteralPath (Join-Path $testRoot "Test.csproj") -Encoding UTF8
& dotnet run --project (Join-Path $testRoot "Test.csproj") -c Release
if ($LASTEXITCODE -ne 0) { throw "事件日志协议测试失败" }
