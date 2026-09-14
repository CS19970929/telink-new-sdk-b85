$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$testRoot = Join-Path $env:LOCALAPPDATA ("CodexTemp\bms-tool-windows\event-log-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$customer = Get-Content -LiteralPath (Join-Path $root "BmsTool.Windows\MainWindow.EventLogUi.cs") -Raw
$factory = Get-Content -LiteralPath (Join-Path $root "BmsFactoryTest.Windows\MainWindow.EventLogUi.cs") -Raw
if ($customer.Contains('DeleteEventLogs') -or $customer.Contains('ResetEventRecord')) { throw "客户版不能接入日志删除" }
if (-not $factory.Contains('deleteButton.Click += DeleteEventLogs_Click;')) { throw "完整版缺少删除入口" }
$start = $customer.IndexOf('int count = 500;')
$end = $customer.IndexOf('_deviceEventLogs.Clear();', $start)
if ($start -lt 0 -or $end -le $start) { throw "找不到真实分页读取代码" }
$body = $customer.Substring($start, $end - $start)
$factoryStart = $factory.IndexOf('int count = 500;')
$factoryEnd = $factory.IndexOf('_deviceEventLogs.Clear();', $factoryStart)
if ($body -cne $factory.Substring($factoryStart, $factoryEnd - $factoryStart)) { throw "两版日志分页逻辑必须一致" }
$constants = [regex]::Matches($factory, 'private const ushort ResetEventRecord\w+ = 0x[0-9A-Fa-f]+;').Value -join "`n"
if (-not $constants) { throw "找不到删除协议常量" }
$sleep = Get-Content -LiteralPath (Join-Path $root "Shared\MainWindow.SleepUi.cs") -Raw
$constants += "`n" + ([regex]::Matches($sleep, 'private const ushort SleepCommand\w+ = 0x[0-9A-Fa-f]+;').Value -join "`n")
foreach ($app in @("BmsTool.Windows", "BmsFactoryTest.Windows")) {
    $xaml = Get-Content -LiteralPath (Join-Path $root "$app\MainWindow.xaml") -Raw
    if (-not $xaml.Contains('Click="SleepBms_Click"')) { throw "两版均须包含休眠入口" }
}
$source = @'
using BmsTool.Windows;
using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
sealed class Status { public string Text = ""; }
sealed class Client {
    public int Capacity=500, FailPage=-1, ShortPage=-1, RejectPage=-1;
    public byte ErrorCode=1, Function=3;
    public List<ushort> Starts=new();
    public Task<ushort[]> ReadRegistersAsync(ushort start, ushort count) {
        int page=Starts.Count; Starts.Add(start);
        if(count!=100 || page==FailPage) throw new IOException("Read failed");
        if(start+count>0xC008+Capacity || page==RejectPage) throw new BmsModbusException(Function,ErrorCode);
        ushort[] words=new ushort[page==ShortPage?99:count];
        for(int i=0;i<words.Length;++i) words[i]=(ushort)(start-0xC008+i);
        return Task.FromResult(words);
    }
}
static class Test {
    __CONSTANTS__
    static Status _eventLogStatus=new();
    static void Check(bool ok) { if(!ok) throw new Exception("Assertion failed"); }
    static async Task<ushort[]> Read(Client bms) {
__BODY__
        return words;
    }
    static async Task Main() {
        byte[] sleepRequest=ModbusRtu.WriteSingle(SleepCommandRegister,SleepCommandValue);
        Check(Convert.ToHexString(sleepRequest.AsSpan(0,6))=="01061102000A");
        ModbusRtu.ValidateWriteSingleAck(sleepRequest,SleepCommandRegister,SleepCommandValue);
        byte[] deleteRequest=ModbusRtu.WriteSingle(ResetEventRecordRegister,ResetEventRecordValue);
        Check(Convert.ToHexString(deleteRequest.AsSpan(0,6))=="010610070001");
        ModbusRtu.ValidateWriteSingleAck(deleteRequest,ResetEventRecordRegister,ResetEventRecordValue);
        foreach(byte[] bad in new[]{
            ModbusRtu.WriteSingle(0x1006,1), ModbusRtu.WriteSingle(0x1007,0),
            ModbusRtu.Frame(new byte[]{1,0x86,4}),
            ModbusRtu.Frame(new byte[]{2,6,0x10,7,0,1}),
            ModbusRtu.Frame(new byte[]{1,3,0x10,7,0,1}),
            new byte[]{1,6,0x10,7,0,1,0,0}}) {
            bool rejected=false;
            try { ModbusRtu.ValidateWriteSingleAck(bad,ResetEventRecordRegister,ResetEventRecordValue); }
            catch(IOException) { rejected=true; }
            Check(rejected);
        }
        foreach(int capacity in new[]{100,500}) {
            var bms=new Client{Capacity=capacity};
            ushort[] words=await Read(bms);
            Check(words.Length==capacity && bms.Starts.Count==(capacity==100?2:5));
            for(int i=0;i<capacity;++i) Check(words[i]==i);
            for(int i=0;i<bms.Starts.Count;++i) Check(bms.Starts[i]==0xC008+i*100);
        }
        foreach(int page in new[]{0,1,2,4}) {
            foreach(var bms in new[]{
                new Client{FailPage=page}, new Client{ShortPage=page},
                new Client{RejectPage=page,ErrorCode=2},
                new Client{RejectPage=page,ErrorCode=3},
                new Client{RejectPage=page,Function=6},
                new Client{RejectPage=page==1?2:page}}) {
                bool failed=false;
                try { await Read(bms); } catch(IOException) { failed=true; } catch(InvalidOperationException) { failed=true; }
                Check(failed && bms.Starts.Count==(bms.RejectPage>=0?bms.RejectPage:page)+1);
            }
        }
        Console.WriteLine("PASS: factory-only delete entry and protocol ACK validation, synchronized pagination, 100/500 pages, ordering, automatic capacity detection, address/CRC/function errors, short response and transport failure");
    }
}
'@
Copy-Item -LiteralPath (Join-Path $root "BmsTool.Windows\ModbusRtu.cs") -Destination $testRoot
$source.Replace('__BODY__', $body).Replace('__CONSTANTS__', $constants) | Set-Content -LiteralPath (Join-Path $testRoot "Test.cs") -Encoding UTF8
'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net7.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings></PropertyGroup></Project>' | Set-Content -LiteralPath (Join-Path $testRoot "Test.csproj") -Encoding UTF8
& dotnet run --project (Join-Path $testRoot "Test.csproj") -c Release
if ($LASTEXITCODE -ne 0) { throw "日志分页测试失败" }
