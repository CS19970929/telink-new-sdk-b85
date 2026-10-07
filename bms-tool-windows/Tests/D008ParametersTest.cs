using System.IO.Compression;
using BmsTool.Windows;
class Program {
 static void Check(bool ok){if(!ok)throw new Exception("assertion failed");}
 static void Reject(Action action){try{action();}catch(ArgumentException){return;}throw new Exception("expected rejection");}
 static async Task Main(){
  var cal=D008Parameters.Calibration(-123456,1234567);Check(D008Parameters.I32(cal,0)==-123456 && D008Parameters.U32(cal,2)==1234567);
  Check(D008Parameters.Serial(D008Parameters.SerialWords("D008-TEST"))=="D008-TEST");Reject(()=>D008Parameters.SerialWords("中文"));Reject(()=>D008Parameters.SerialWords(new string('A',33)));
  Check(D008Parameters.Temperature("-40")==0 && D008Parameters.Temperature("125")==1650);Reject(()=>D008Parameters.Temperature("126"));
  Check(D008Parameters.Capacity("100")==1000);Reject(()=>D008Parameters.Capacity("0"));
  Check(D008Parameters.Gain(-1010,-10,-2000)==2000000);Reject(()=>D008Parameters.Gain(100,0,-100));
  Check(D008Parameters.SupportsProtocol(1)&&D008Parameters.SupportsProtocol(2)&&!D008Parameters.SupportsProtocol(3));
  var old=new BmsClient{Supported=false};var capture=await old.ReadD008ParametersAsync();Check(!capture.Supported && capture.Blocks.ContainsKey("SoftwareProtection"));
  try{await old.WriteD008VerifiedAsync(0x2318,new ushort[]{1000},false);throw new Exception("write accepted");}catch(NotSupportedException){}Check(old.Writes==0);
  var client=new BmsClient();
  capture=await client.ReadD008ParametersAsync();Check(capture.Supported&&capture.ProtocolVersion==2&&capture.Blocks["Capability"].Length==16&&capture.Blocks.ContainsKey("Balance"));
  var v1=new BmsClient{ProtocolVersion=1};capture=await v1.ReadD008ParametersAsync();Check(capture.Supported&&capture.ProtocolVersion==1&&capture.Blocks["Capability"].Length==12&&!capture.Blocks.ContainsKey("Balance"));
  var future=new BmsClient{ProtocolVersion=3};capture=await future.ReadD008ParametersAsync();Check(!capture.Supported);try{await future.WriteD008VerifiedAsync(0x2318,new ushort[]{1000},false);throw new Exception("future protocol write accepted");}catch(NotSupportedException){}
  var blocked=new BmsClient{FailAccess=true};
  try{await blocked.WriteD008SerialAsync("D008-TEST");throw new Exception("unauthorized SN write accepted");}catch(IOException){}
  Check(blocked.Writes==0 && blocked.Closed==0);
  client.ModeReads=0;await client.WriteD008SerialAsync("D008-TEST");
  Check(client.Writes==6 && client.MaxFrame==17 && client.Closed==1 && client.ModeReads==0);
  client.FailRead=0x2e20;capture=await client.ReadD008ParametersAsync();Check(capture.Errors.Count==1 && capture.Blocks.ContainsKey("SoftwareProtection"));
  var path=Path.Combine(Path.GetTempPath(),Guid.NewGuid()+".zip");
  try{D008Parameters.Export(path,capture);D008Parameters.Export(path,capture);using var zip=ZipFile.OpenRead(path);using var reader=new StreamReader(zip.GetEntry("parameters.json")!.Open());var json=reader.ReadToEnd();Check(json.Contains("Heater:") && !json.Contains("Token") && !json.Contains("password"));}finally{File.Delete(path);}
  client.FailWrite=true;try{await client.WriteD008VerifiedAsync(0x2e24,cal,true);throw new Exception("save failure ignored");}catch(IOException){}Check(client.Closed==2);
  Console.WriteLine("PASS D008 parameter v1/v2 capability probing, v2 balance read, future-version write refusal, codecs, AFE authorization, no retired command, MTU23 SN, partial ZIP, session cleanup");
 }
}
namespace BmsTool.Windows {
 public sealed class BmsModbusException:IOException{public byte Function{get;init;}public byte Code{get;init;}}
 public sealed record AfeHardwareAccessSession(ushort Token);
 public static class BmsRegisters{public const ushort Hardware=0xc012,Software=0xc022;}
 public sealed partial class BmsClient {
  public bool Supported=true,FailWrite,FailAccess,AccessActive;public ushort ProtocolVersion=2,FailRead;public int Writes,MaxFrame,Closed,ModeReads;private readonly ushort[] serial=new ushort[16];
  private Task<ushort[]> ReadRegistersAsync(ushort address,ushort count,CancellationToken ct){
   ct.ThrowIfCancellationRequested();if(address==FailRead)throw new IOException("injected read failure");
   var result=new ushort[count];if(address==0x2e00 && Supported){if(count>0)result[0]=0xd008;if(count>1)result[1]=ProtocolVersion;if(count>2)result[2]=0x007f;}
   if(address==0x2e70){result[0]=1;result[1]=3400;result[2]=50;result[3]=30;}
   if(address==0x2e06)result[0]=7;if(address==0x2e30)serial.CopyTo(result,0);if(address==0x2ae1)ModeReads++;return Task.FromResult(result);
  }
  private Task WriteSingleRegisterAsync(ushort address,ushort value,CancellationToken ct){if(FailWrite)throw new IOException("save failed");CheckAccess(address);if(address==0x2e10&&value==6)throw new Exception("retired command sent");Writes++;return Task.CompletedTask;}
  private Task WriteRegistersAsync(ushort address,ushort[] values,CancellationToken ct){if(FailWrite)throw new IOException("save failed");CheckAccess(address);Writes++;MaxFrame=Math.Max(MaxFrame,9+2*values.Length);if(address>=0x2e50 && address<=0x2e5f)values.CopyTo(serial,address-0x2e50);return Task.CompletedTask;}
  private Task<AfeHardwareAccessSession> OpenAfeHardwareAccessAsync(CancellationToken ct){
   if(FailAccess)throw new IOException("access rejected");AccessActive=true;return Task.FromResult(new AfeHardwareAccessSession(42));}
  private void CheckAccess(ushort address){if((address==0x2e24||address==0x2e40||address>=0x2e50&&address<=0x2e5f)&&!AccessActive)throw new IOException("unauthorized write");}
  private Task TryCloseAfeHardwareAccessAsync(ushort token,CancellationToken ct){AccessActive=false;Closed++;return Task.CompletedTask;}
 }
}
