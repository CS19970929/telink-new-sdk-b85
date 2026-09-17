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
  var m=new AfeHardwareParameterModel();var p=new ushort[35];p[0]=1;p[1]=0x1124;
  m.Load(new(p,p,new(0x1124,64,true,100,16,4,false,1,0,2)));
  m.Rows.Single(x=>x.WireIndex==34).EditValue="1";
  Check(!m.TryBuildCandidate(out _,out _),"zero SCD cannot enable");
  m.Rows.Single(x=>x.WireIndex==22).EditValue="100";
  m.Rows.Single(x=>x.WireIndex==23).EditValue="100";
  Check(m.TryBuildCandidate(out var c,out var error)&&c[34]==64,"SCD valid enable: "+error);
  Console.WriteLine("PASS AFE MTU23 roundtrip, old firmware, wrong ACK, cancel, SCD and serial delayed response/cancel");
 }
}
namespace BmsTool.Windows {
 sealed class BmsBleTransport:IBmsTransport {
  public int? NegotiatedMtu=>23;public bool IsConnected=>true;public bool RequiresSerialWakeup=>Serial;
  public string DiscoveryDescription=>"host";public bool Serial,BadAck,CancelAfterStage;public CancellationTokenSource? Stop;
  public int Stages,Commits,MaxLength,Reads,DropReads,Reconnects;public List<byte> Frame=new();
  public event Action<ReadOnlyMemory<byte>>? DataReceived;public event Action<string>? ConnectionProgress{add{}remove{}}
  public Task ReconnectAsync(CancellationToken ct=default){Reconnects++;return Task.CompletedTask;}
  public void Reset(){Stages=Commits=MaxLength=0;Frame.Clear();}
  public Task WriteAsync(ReadOnlyMemory<byte> data,CancellationToken ct=default){
   ct.ThrowIfCancellationRequested();var q=data.ToArray();MaxLength=Math.Max(MaxLength,q.Length);byte[] body;
   if(q[1]==3){Reads++;if(Reads<=DropReads)return Task.CompletedTask;body=new byte[]{1,3,4,(byte)(BmsRegisters.RealtimeMagic>>8),(byte)(BmsRegisters.RealtimeMagic&255),0,2};}
   else if(q[2]==5){Stages++;Frame.AddRange(q.Skip(7).Take(q[6]));body=new byte[]{1,0x42,5,0,q[3],q[4],(byte)(BadAck?0:Frame.Count)};if(CancelAfterStage)Stop!.Cancel();}
   else{Commits++;body=new byte[]{1,0x42,6,0};}
   DataReceived?.Invoke(ModbusRtu.Frame(body));return Task.CompletedTask;
  }
  public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
 }
}
