using BmsTool.Windows;
using System.Buffers.Binary;
using System.IO;
static class Test
{
 static void Check(bool ok,string why){if(!ok)throw new Exception(why);}
 static async Task Main(string[] args)
 {
  foreach(string mode in new[]{"normal","once","always","bad-version","bad-record","bad-order","reset","unknown-time"})
  {
   var transport=new HistoryTransport(mode);await using var client=new BmsClient(transport);
   try {
    var history=await client.ReadEventHistoryAsync();
    Check(mode is "normal" or "once" or "unknown-time","异常数据不得发布");
    Check(history.Records.Count==100 && transport.Writes==0,"100条只读分页");
    Check(transport.InfoReads==(mode=="once"?4:2),"读取前后核对并有限重读");
    var row=history.Records[0];
    Check(row.CurrentDecia==-123 && row.TemperatureDecic==-25 && row.SocPermille==600 && row.CellIndex==3,"单位/符号/字序");
    Check(row.SourceText=="软件保护" && row.LevelText=="3级" && row.StateText=="发生","来源/等级/阶段");
    Check((row.UtcTime is null)==(mode=="unknown-time"),"未校时不能伪造日期");
    string csv=DeviceEventCsv.Export(history.Records,"=CMD\nSN",DateTimeOffset.UtcNow);
    Check(csv.Contains("'=CMD；SN") && csv.Contains("\"-12.3\"") && csv.Contains("事件UTC"),"CSV公式防护、负数和UTC");
    Check(transport.MaxCount==64 && transport.MaxRequest==8,"最多四条/64字，MTU23请求不分片");
   } catch(IOException) { Check(mode is not ("normal" or "once" or "unknown-time"),"合法读取失败"); }
  }
  if(args.Length==1){
   await using var transport=new FirmwareTransport(args[0]);await using var client=new BmsClient(transport);
   await client.ReadIdentityAsync("","event interop");
   await client.ReadAndSynchronizeDeviceClockAsync();
   var h=await client.ReadEventHistoryAsync();Check(h.Records.Count is >=1 and <=100,"真实C模块互操作");
   Check(h.Records.All(r=>r.SocPermille<=1000),"真实C模块SOC范围");
  }
  Console.WriteLine("PASS both-edition event history: 100 records, 64-word pages, signed values, UTC/unknown time, changed snapshots, malformed data, CSV; optional real C interop");
 }
}
sealed class HistoryTransport(string mode):IBmsTransport
{
 public bool IsConnected=>true;
 public string DiscoveryDescription=>"event-history peer";
 public event Action<ReadOnlyMemory<byte>>? DataReceived;
 public event Action<string>? ConnectionProgress{add{}remove{}}
 public Task ReconnectAsync(CancellationToken ct=default)=>Task.CompletedTask;
 public int InfoReads,Writes,MaxCount,MaxRequest;
 uint sequence=100;
 public Task WriteAsync(ReadOnlyMemory<byte> data,CancellationToken ct)
 {
  byte[] request=data.ToArray();MaxRequest=Math.Max(MaxRequest,request.Length);
  ushort address=BinaryPrimitives.ReadUInt16BigEndian(request.AsSpan(2)),count=BinaryPrimitives.ReadUInt16BigEndian(request.AsSpan(4));
  if(request[1]!=3){Writes++;throw new Exception("读取日志不应写寄存器");}
  MaxCount=Math.Max(MaxCount,count);var words=new ushort[count];
  if(address==0x3800){
   InfoReads++;if(InfoReads%2==0 && (mode=="always" || (mode=="once"&&InfoReads==2)))sequence++;
   words[0]=0x4C47;words[1]=(ushort)(mode=="bad-version"?2:1);words[2]=16;words[3]=100;words[4]=100;
   words[5]=(ushort)(mode=="reset"?4:0);words[6]=(ushort)(sequence>>16);words[7]=(ushort)sequence;
  } else {
   int start=(address-0x3900)/16;
   for(int i=0;i<count;i+=16){uint seq=sequence-(uint)(start+i/16);if(mode=="bad-order")seq=sequence;
    words[i]=(ushort)(seq>>16);words[i+1]=(ushort)seq;
    uint utc=(uint)new DateTimeOffset(2026,11,3,12,34,56,TimeSpan.Zero).ToUnixTimeSeconds();
    words[i+2]=(ushort)(utc>>16);words[i+3]=(ushort)utc;words[i+5]=42;
    words[i+6]=(ushort)(6<<8|1|4|48|(mode=="unknown-time"?0:64));
    words[i+7]=3500;words[i+8]=3550;words[i+9]=unchecked((ushort)-123);words[i+10]=unchecked((ushort)-25);words[i+11]=600;words[i+12]=3<<8;
    if(mode=="bad-record")words[i+15]=1;
   }
  }
  byte[] body=new byte[3+count*2];body[0]=1;body[1]=3;body[2]=(byte)(count*2);
  for(int i=0;i<count;i++)BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(3+i*2),words[i]);
  byte[] reply=ModbusRtu.Frame(body);
  for(int i=0;i<reply.Length;i+=7)DataReceived?.Invoke(reply.AsMemory(i,Math.Min(7,reply.Length-i)));
  return Task.CompletedTask;
 }
 public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
}
