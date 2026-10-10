using BmsTool.Windows;
using System.Buffers.Binary;
static class Test {
 static void Check(bool ok,string why){if(!ok)throw new Exception(why);}
 static async Task Main(){
  var t=new BmsBleTransport(); await using var b=new BmsClient(t);
  var words=Enumerable.Range(0,35).Select(i=>(ushort)(i+100)).ToArray();
  await b.WriteAfeProfileAsync(words,new(7,60,2,0x1124));
  Check(t.Stages==8&&t.Commits==1&&t.MaxLength<=20,"MTU23 staged commit");
  var raw=new byte[70];for(int i=0;i<35;i++)BinaryPrimitives.WriteUInt16BigEndian(raw.AsSpan(i*2),words[i]);
  Check(t.Frame.SequenceEqual(ModbusRtu.WriteMultiple(0x2500,raw)),"reassembled original frame");
  t.Reset();try{await b.WriteAfeProfileAsync(words,new(7,60,1,0x1124));throw new Exception("old accepted");}catch(IOException){}
  Check(t.Stages==0&&t.Commits==0,"old firmware untouched");
  t.BadAck=true;try{await b.WriteAfeProfileAsync(words,new(7,60,2,0x1124));throw new Exception("bad ACK accepted");}catch(IOException){}
  Check(t.Commits==0,"bad ACK no commit");t.Reset();t.BadAck=false;t.CancelAfterStage=true;
  using(var stop=new CancellationTokenSource()){t.Stop=stop;try{await b.WriteAfeProfileAsync(words,new(7,60,2,0x1124),stop.Token);throw new Exception("cancel accepted");}catch(OperationCanceledException){}}
  Check(t.Commits==0,"cancel no commit");t.CancelAfterStage=false;t.Reset();t.Serial=true;t.DropReads=3;
  await b.ProbeAsync();Check(t.Reads==4&&t.Reconnects==0,"serial wake retries without reopen");
  t.DropReads=100;using(var stop=new CancellationTokenSource(100)){try{await b.ProbeAsync(stop.Token);throw new Exception("probe cancellation ignored");}catch(OperationCanceledException){}}
  Check(t.Reconnects==0,"serial cancel no reopen");
  // 通过实际 ReadBatteryAsync 和 Modbus 大端编码验证跨 16 位边界的逐串位图。
  t.DropReads=0;t.BatteryRead=true;
  var battery=await b.ReadBatteryAsync();
  Check(battery.BalanceCellMask==0x818005u,"D03D/D03E low-word-first, upper reserved bits masked");
  Check(battery.IsCellBalancing(0)&&battery.IsCellBalancing(15)&&battery.IsCellBalancing(16)&&battery.IsCellBalancing(23),"cells 1/16/17/24 map correctly");
  Check(!battery.IsCellBalancing(2)&&!battery.IsCellBalancing(1)&&!battery.IsCellBalancing(24)&&!battery.IsCellBalancing(-1),"missing 61001 slot, unselected cell and invalid indexes excluded");
  t.BalanceOff=true;battery=await b.ReadBatteryAsync();
  Check(battery.BalanceCellMask==0&&!battery.IsCellBalancing(0),"next snapshot clears stopped balance");
  t.BatteryRead=false;
  var m=new AfeHardwareParameterModel();var p=new ushort[35];p[0]=1;p[1]=0x1124;
  m.Load(new(p,p,new(0x1124,64,true,100,16,4,false,1,0,2)));
  m.Rows.Single(x=>x.WireIndex==34).EditValue="1";
  Check(!m.TryBuildCandidate(out _,out _),"zero SCD cannot enable");
  m.Rows.Single(x=>x.WireIndex==22).EditValue="100";
  m.Rows.Single(x=>x.WireIndex==23).EditValue="100";
  Check(m.TryBuildCandidate(out var c,out var error)&&c[34]==64,"SCD valid enable: "+error);
  Console.WriteLine("PASS AFE MTU23 roundtrip, old firmware, wrong ACK, cancel, SCD, serial delayed response/cancel and per-cell balance mask");
 }
}
namespace BmsTool.Windows {
 sealed class BmsBleTransport:IBmsTransport,IBmsMtuTransport {
  public int? NegotiatedMtu=>23;public bool IsConnected=>true;public bool RequiresSerialWakeup=>Serial;
  public string DiscoveryDescription=>"host";public bool Serial,BadAck,CancelAfterStage,BatteryRead,BalanceOff;public CancellationTokenSource? Stop;
  public int Stages,Commits,MaxLength,Reads,DropReads,Reconnects;public List<byte> Frame=new();
  public event Action<ReadOnlyMemory<byte>>? DataReceived;public event Action<string>? ConnectionProgress{add{}remove{}}
  public Task ReconnectAsync(CancellationToken ct=default){Reconnects++;return Task.CompletedTask;}
  public void Reset(){Stages=Commits=MaxLength=0;Frame.Clear();}
  public Task WriteAsync(ReadOnlyMemory<byte> data,CancellationToken ct=default){
   ct.ThrowIfCancellationRequested();var q=data.ToArray();MaxLength=Math.Max(MaxLength,q.Length);byte[] body;
   if(q[1]==3 && BatteryRead){
    ushort address=BinaryPrimitives.ReadUInt16BigEndian(q.AsSpan(2));
    ushort count=BinaryPrimitives.ReadUInt16BigEndian(q.AsSpan(4));
    var values=new ushort[count];
    if(address==BmsRegisters.Legacy){
     CheckBatteryCount(count);Array.Fill(values,(ushort)3300,0,32);values[2]=BmsRegisters.MissingCellVoltageMv;
     values[61]=BalanceOff?(ushort)0:(ushort)0x8005;values[62]=BalanceOff?(ushort)0:(ushort)0xFF81;
    }
    else if(address==BmsRegisters.Realtime)values[0]=BmsRegisters.RealtimeMagic;
    body=new byte[3+2*count];body[0]=1;body[1]=3;body[2]=(byte)(2*count);
    for(int i=0;i<count;i++)BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(3+2*i),values[i]);
   }
   else if(q[1]==3){Reads++;if(Reads<=DropReads)return Task.CompletedTask;body=new byte[]{1,3,4,(byte)(BmsRegisters.RealtimeMagic>>8),(byte)(BmsRegisters.RealtimeMagic&255),0,2};}
   else if(q[2]==5){Stages++;Frame.AddRange(q.Skip(7).Take(q[6]));body=new byte[]{1,0x42,5,0,q[3],q[4],(byte)(BadAck?0:Frame.Count)};if(CancelAfterStage)Stop!.Cancel();}
   else{Commits++;body=new byte[]{1,0x42,6,0};}
   DataReceived?.Invoke(ModbusRtu.Frame(body));return Task.CompletedTask;
  }
  private static void CheckBatteryCount(int count){if(count!=63)throw new Exception("battery read window changed");}
  public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
 }
}
