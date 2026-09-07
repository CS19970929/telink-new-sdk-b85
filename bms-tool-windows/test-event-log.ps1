$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$testRoot = Join-Path $env:LOCALAPPDATA ("CodexTemp\bms-tool-windows\event-log-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$customer = Get-Content -LiteralPath (Join-Path $root "BmsTool.Windows\MainWindow.EventLogUi.cs") -Raw
$factory = Get-Content -LiteralPath (Join-Path $root "BmsFactoryTest.Windows\MainWindow.EventLogUi.cs") -Raw
if ($customer -cne $factory) { throw "两版事件日志页面必须同步维护" }
$start = $customer.IndexOf('ushort[] words = new ushort[count];')
$end = $customer.IndexOf('_deviceEventLogs.Clear();', $start)
if ($start -lt 0 -or $end -le $start) { throw "找不到真实分页读取代码" }
$body = $customer.Substring($start, $end - $start)
$source = @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
sealed class Status { public string Text = ""; }
sealed class Client {
    public int Capacity=500, FailPage=-1, ShortPage=-1;
    public List<ushort> Starts=new();
    public Task<ushort[]> ReadRegistersAsync(ushort start, ushort count) {
        int page=Starts.Count; Starts.Add(start);
        if(count!=100 || start+count>0xC008+Capacity || page==FailPage) throw new IOException("Read failed");
        ushort[] words=new ushort[page==ShortPage?99:count];
        for(int i=0;i<words.Length;++i) words[i]=(ushort)(start-0xC008+i);
        return Task.FromResult(words);
    }
}
static class Test {
    static Status _eventLogStatus=new();
    static void Check(bool ok) { if(!ok) throw new Exception("Assertion failed"); }
    static async Task<ushort[]> Read(Client bms,int count) {
__BODY__
        return words;
    }
    static async Task Main() {
        foreach(int capacity in new[]{100,500}) {
            var bms=new Client{Capacity=capacity};
            ushort[] words=await Read(bms,capacity);
            Check(words.Length==capacity && bms.Starts.Count==capacity/100);
            for(int i=0;i<capacity;++i) Check(words[i]==i);
            for(int i=0;i<bms.Starts.Count;++i) Check(bms.Starts[i]==0xC008+i*100);
        }
        Check((await Read(new Client{Capacity=500},100)).Length==100);
        foreach(bool shortPage in new[]{false,true}) {
            var bms=new Client{FailPage=shortPage?-1:2,ShortPage=shortPage?2:-1};
            bool failed=false;
            try { await Read(bms,500); } catch(IOException) { failed=true; } catch(InvalidOperationException) { failed=true; }
            Check(failed && bms.Starts.Count==3);
        }
        var old=new Client{Capacity=100}; bool rejected=false;
        try { await Read(old,500); } catch(IOException) { rejected=true; }
        Check(rejected && old.Starts.Count==2);
        Console.WriteLine("PASS: synchronized UIs, 100/500 pages, ordering, legacy 100, short response and mid-read failure");
    }
}
'@
$source.Replace('__BODY__', $body) | Set-Content -LiteralPath (Join-Path $testRoot "Test.cs") -Encoding UTF8
'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net7.0</TargetFramework><OutputType>Exe</OutputType></PropertyGroup></Project>' | Set-Content -LiteralPath (Join-Path $testRoot "Test.csproj") -Encoding UTF8
& dotnet run --project (Join-Path $testRoot "Test.csproj") -c Release
if ($LASTEXITCODE -ne 0) { throw "日志分页测试失败" }
