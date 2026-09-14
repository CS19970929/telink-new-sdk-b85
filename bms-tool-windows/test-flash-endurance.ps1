$ErrorActionPreference="Stop"
$root=Split-Path -Parent $MyInvocation.MyCommand.Path
$out=Join-Path $env:LOCALAPPDATA ("CodexTemp\bms-tool-windows\flash-test-"+[guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $out -Force | Out-Null
$ui=Get-Content -LiteralPath (Join-Path $root "BmsFactoryTest.Windows\MainWindow.FlashEndurance.cs") -Raw
$customer=Get-Content -LiteralPath (Join-Path $root "BmsTool.Windows\MainWindow.AdvancedFeatures.cs") -Raw
if($customer.Contains('AddFlashEnduranceTab')){throw "Customer build must not expose endurance testing"}
Copy-Item -LiteralPath (Join-Path $root "BmsTool.Windows\ModbusRtu.cs") -Destination $out
Copy-Item -LiteralPath (Join-Path $root "BmsFactoryTest.Windows\BmsClient.FlashTest.cs") -Destination $out
$sample=[regex]::Match($ui,'private static ushort FlashSample[^;]+;').Value
$decode=[regex]::Match($ui,'private static uint FlashU32[^;]+;').Value
if(-not $sample -or -not $decode){throw "Missing production pattern/decoder"}
$source=@'
using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
namespace BmsTool.Windows;
public sealed partial class BmsClient {
 public byte[] Request=Array.Empty<byte>(); public bool BadAck;
 private Task<byte[]> TransactAsync(byte[] request,CancellationToken ct) {
  Request=request;
  return Task.FromResult(ModbusRtu.Frame(new byte[]{1,0x10,0x27,0x20,0,(byte)(BadAck?19:20)}));
 }
}
static class Tests {
 __SAMPLE__
 __DECODE__
 static void Check(bool ok){if(!ok)throw new Exception("Assertion failed");}
 static async Task Main(){
  Check(FlashU32(new ushort[]{0x1234,0x5678},0)==0x12345678);
  for(int i=0;i<1000;i++){Check(FlashSample(0,i)==i%101);Check(FlashSample(1,i)==50);Check(FlashSample(2,i)==49+i%2);}
  var bms=new BmsClient();ushort[] data=new ushort[20];for(int i=0;i<20;i++)data[i]=(ushort)(0x100+i);
  await bms.RestoreFlashTestSnapshotAsync(data);
  Check(Convert.ToHexString(bms.Request.AsSpan(0,7))=="01102720001428");
  for(int i=0;i<20;i++)Check(bms.Request[7+i*2]==1 && bms.Request[8+i*2]==i);
  ModbusRtu.ValidateFrame(bms.Request);
  bool rejected=false; bms.BadAck=true;
  try{await bms.RestoreFlashTestSnapshotAsync(data);}catch(IOException){rejected=true;}Check(rejected);
  rejected=false;try{await bms.RestoreFlashTestSnapshotAsync(new ushort[19]);}catch(ArgumentException){rejected=true;}Check(rejected);
  Console.WriteLine("PASS: factory-only UI, real SOC patterns, counters, snapshot byte order/CRC/ACK rejection");
 }
}
'@
$source.Replace('__SAMPLE__',$sample).Replace('__DECODE__',$decode) | Set-Content -LiteralPath (Join-Path $out 'Tests.cs') -Encoding UTF8
'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net7.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings></PropertyGroup></Project>' | Set-Content -LiteralPath (Join-Path $out 'Tests.csproj') -Encoding UTF8
& dotnet run --project (Join-Path $out 'Tests.csproj') -c Release
if($LASTEXITCODE -ne 0){throw "Flash endurance protocol test failed"}
