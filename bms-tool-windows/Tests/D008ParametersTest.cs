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
  Check(D008Parameters.Heater(true,"-5.5","5").SequenceEqual(new ushort[]{1,345,450}));
  Reject(()=>D008Parameters.Heater(true,"5","5"));Reject(()=>D008Parameters.Heater(true,"5","0"));
  Check(D008Parameters.Balance(true,"3300","30","15").SequenceEqual(new ushort[]{1,3300,30,15}));
  Check(D008Parameters.Balance(false,"1000","1","0")[3]==0);
  foreach(var words in new[]{new ushort[]{1,3300,30},new ushort[]{2,3300,30,15},new ushort[]{1,999,30,15},
      new ushort[]{1,4501,30,15},new ushort[]{1,3300,0,0},new ushort[]{1,3300,1001,15},new ushort[]{1,3300,30,30}})
      Reject(()=>D008Parameters.ValidateBalance(words));
  foreach(var words in new[]{new ushort[]{1,400},new ushort[]{2,400,450},new ushort[]{1,400,400},new ushort[]{1,400,1651}})
      Reject(()=>D008Parameters.ValidateHeater(words));
  var old=new BmsClient{Supported=false};var capture=await old.ReadD008ParametersAsync();Check(!capture.Supported && capture.Blocks.ContainsKey("SoftwareProtection"));
  try{await old.WriteD008VerifiedAsync(0x2318,new ushort[]{1000},false);throw new Exception("write accepted");}catch(NotSupportedException){}Check(old.Writes==0);
  var client=new BmsClient();
  capture=await client.ReadD008ParametersAsync();Check(capture.Supported&&capture.ProtocolVersion==2&&capture.Blocks["Capability"].Length==16&&capture.Blocks.ContainsKey("Balance"));
  var v1=new BmsClient{ProtocolVersion=1};capture=await v1.ReadD008ParametersAsync();Check(capture.Supported&&capture.ProtocolVersion==1&&capture.Blocks["Capability"].Length==12&&!capture.Blocks.ContainsKey("Balance"));
  Check(!D008Parameters.BalanceAvailable(capture)&&D008Parameters.HeaterAvailable(capture));
  try{await v1.WriteD008VerifiedAsync(D008Parameters.BalanceBase,new ushort[]{1,3300,30,15},false);throw new Exception("v1 balance write accepted");}catch(NotSupportedException){}Check(v1.Writes==0);
  var noBalance=new BmsClient{CapabilityFlags=0x003f};
  try{await noBalance.WriteD008VerifiedAsync(D008Parameters.BalanceBase,new ushort[]{1,3300,30,15},false);throw new Exception("unsupported balance write accepted");}catch(NotSupportedException){}Check(noBalance.Writes==0);
  var noHeater=new BmsClient{HeaterSupported=false};
  try{await noHeater.WriteD008VerifiedAsync(D008Parameters.HeaterBase,new ushort[]{1,400,450},false);throw new Exception("unsupported heater write accepted");}catch(NotSupportedException){}Check(noHeater.Writes==0);
  var business=new BmsClient();
  await business.WriteD008VerifiedAsync(D008Parameters.BalanceBase,new ushort[]{1,3300,30,15},false);
  Check(business.Writes==1 && business.MaxFrame==17 && business.LastWriteAddress==0x2e70);
  await business.WriteD008VerifiedAsync(D008Parameters.HeaterBase,D008Parameters.Heater(true,"-5","5"),false);
  capture=await business.ReadD008ParametersAsync();
  Check(capture.Blocks["Balance"].SequenceEqual(new ushort[]{1,3300,30,15}) && capture.Blocks["Heater"].SequenceEqual(new ushort[]{1,350,450}));
  Check(D008Parameters.BalanceAvailable(capture)&&D008Parameters.HeaterAvailable(capture));
  await business.SetD008BalanceAllowedAsync(false);
  capture=await business.ReadD008ParametersAsync();
  Check(capture.Blocks["Balance"].SequenceEqual(new ushort[]{0,3300,30,15}));
  await business.WriteD008BalanceThresholdsAsync("3500","40","20");
  capture=await business.ReadD008ParametersAsync();
  Check(capture.Blocks["Balance"].SequenceEqual(new ushort[]{0,3500,40,20}));
  await business.SetD008BalanceAllowedAsync(true);
  capture=await business.ReadD008ParametersAsync();
  Check(capture.Blocks["Balance"].SequenceEqual(new ushort[]{1,3500,40,20}));
  await business.WriteD008BalanceThresholdsAsync("3300","30","15");
  capture=await business.ReadD008ParametersAsync();Check(capture.Blocks["Balance"][0]==1);
  business.FailRead=D008Parameters.BalanceBase;int beforeFailure=business.Writes;
  try{await business.SetD008BalanceAllowedAsync(false);throw new Exception("permission read failure ignored");}catch(IOException){}
  Check(business.Writes==beforeFailure);business.FailRead=0;
  try{await noBalance.SetD008BalanceAllowedAsync(true);throw new Exception("unsupported permission write accepted");}catch(NotSupportedException){}
  Check(noBalance.Writes==0);
  int writes=business.Writes;
  try{await business.WriteD008VerifiedAsync(D008Parameters.BalanceBase,new ushort[]{1,3300,15,30},false);throw new Exception("invalid balance write accepted");}catch(ArgumentException){}Check(business.Writes==writes);
  business.MismatchReadback=true;
  try{await business.WriteD008VerifiedAsync(D008Parameters.BalanceBase,new ushort[]{1,3300,30,15},false);throw new Exception("mismatch accepted");}catch(IOException){}
  using(var cancelled=new CancellationTokenSource()){cancelled.Cancel();writes=business.Writes;
   try{await business.WriteD008VerifiedAsync(D008Parameters.HeaterBase,new ushort[]{1,400,450},false,cancelled.Token);throw new Exception("cancelled write accepted");}catch(OperationCanceledException){}Check(business.Writes==writes);}
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
  Console.WriteLine("PASS D008 parameters: persistent balance permission, threshold/permission preservation, read failure refusal, heater/balance validation, exact writes/readback, mismatch/cancellation, capability refusal, v1/v2/future, AFE authorization, MTU23 SN, partial ZIP, session cleanup");
 }
}
namespace BmsTool.Windows {
 public sealed class BmsModbusException:IOException{public byte Function{get;init;}public byte Code{get;init;}}
 public sealed record AfeHardwareAccessSession(ushort Token);
 public static class BmsRegisters{public const ushort Hardware=0xc012,Software=0xc022;}
 public sealed partial class BmsClient {
  public bool Supported=true,HeaterSupported=true,FailWrite,FailAccess,AccessActive,MismatchReadback;
  public ushort ProtocolVersion=2,CapabilityFlags=0x007f,FailRead,LastWriteAddress;public int Writes,MaxFrame,Closed,ModeReads;
  private readonly ushort[] serial=new ushort[16];private readonly Dictionary<ushort,ushort[]> saved=new();
  private Task<ushort[]> ReadRegistersAsync(ushort address,ushort count,CancellationToken ct){
   ct.ThrowIfCancellationRequested();if(address==FailRead)throw new IOException("injected read failure");
   var result=new ushort[count];if(address==0x2e00 && Supported){if(count>0)result[0]=0xd008;if(count>1)result[1]=ProtocolVersion;if(count>2)result[2]=CapabilityFlags;if(count>9)result[9]=(ushort)(HeaterSupported?1:0);}
   if(address==0x2e70){result[0]=1;result[1]=3400;result[2]=50;result[3]=30;}
   if(address==0x2e20){result[0]=1;result[1]=400;result[2]=450;}
   if(saved.TryGetValue(address,out var stored)){Array.Copy(stored,result,Math.Min(stored.Length,result.Length));if(MismatchReadback)result[0]^=1;}
   if(address==0x2e06)result[0]=7;if(address==0x2e30)serial.CopyTo(result,0);if(address==0x2ae1)ModeReads++;return Task.FromResult(result);
  }
  private Task WriteSingleRegisterAsync(ushort address,ushort value,CancellationToken ct){if(FailWrite)throw new IOException("save failed");CheckAccess(address);if(address==0x2e10&&value==6)throw new Exception("retired command sent");Writes++;return Task.CompletedTask;}
  private Task WriteRegistersAsync(ushort address,ushort[] values,CancellationToken ct){ct.ThrowIfCancellationRequested();if(FailWrite)throw new IOException("save failed");CheckAccess(address);Writes++;LastWriteAddress=address;MaxFrame=Math.Max(MaxFrame,9+2*values.Length);if(address>=0x2e50 && address<=0x2e5f)values.CopyTo(serial,address-0x2e50);else saved[address]=values.ToArray();return Task.CompletedTask;}
  private Task<AfeHardwareAccessSession> OpenAfeHardwareAccessAsync(CancellationToken ct){
   if(FailAccess)throw new IOException("access rejected");AccessActive=true;return Task.FromResult(new AfeHardwareAccessSession(42));}
  private void CheckAccess(ushort address){if((address==0x2e24||address==0x2e40||address>=0x2e50&&address<=0x2e5f)&&!AccessActive)throw new IOException("unauthorized write");}
  private Task TryCloseAfeHardwareAccessAsync(ushort token,CancellationToken ct){AccessActive=false;Closed++;return Task.CompletedTask;}
 }
}
