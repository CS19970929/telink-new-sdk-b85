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
  Check(t.Commits==0,"cancel no commit");t.CancelAfterStage=false;t.Reset();t.Serial=true;int wakeStart=t.Reads;t.DropReads=wakeStart+3;
  await b.ProbeAsync();Check(t.Reads==wakeStart+4&&t.Reconnects==0,"serial wake retries without reopen");
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
  // 2026-10-20 现场：持久请求正常，effective 整组 0xFFFF，不能显示读取成功。
  t.HardwareRead=true;t.EffectiveUnavailable=true;
  try { await new AfeHardwareClient(b).ReadAllAsync();throw new Exception("无效 AFE effective 被当成读取成功"); }
  catch(IOException ex) { Check(ex.Message.Contains("AFE") && ex.Message.Contains("有效"),"有效值不可用必须给出明确提示"); }
  t.EffectiveUnavailable=false;
  var hardware=await new AfeHardwareClient(b).ReadAllAsync();
  Check(hardware.Effective[0]==1 && hardware.Effective[1]==0x3510,"恢复后可重新读取有效配置");
  t.HardwareRead=false;
  var m=new AfeHardwareParameterModel();var p=new ushort[35];p[0]=1;p[1]=0x1124;
  m.Load(new(p,p,new(0x1124,64,true,100,16,4,false,1,0,2)));
  m.Rows.Single(x=>x.WireIndex==34 && x.CapabilityMask==AfeHardwareProtocolMap.CapSc).EditValue="1";
  Check(!m.TryBuildCandidate(out _,out _),"zero SCD cannot enable");
  m.Rows.Single(x=>x.WireIndex==22).EditValue="100";
  m.Rows.Single(x=>x.WireIndex==23).EditValue="100";
  Check(m.TryBuildCandidate(out var c,out var error)&&c[34]==64,"SCD valid enable: "+error);
  foreach(var pair in new[]{(Command:(byte)0x10,Length:18),(Command:(byte)0x11,Length:8),
      (Command:(byte)0x13,Length:8),(Command:(byte)0x12,Length:6),(Command:(byte)0x14,Length:6)}) {
   var response=new byte[pair.Length-2];response[0]=1;response[1]=0x42;response[2]=pair.Command;
   byte[] frame=ModbusRtu.Frame(response);
   Check(ModbusRtu.InferExpectedLength(frame.Take(3).ToArray()) is null,"register fragmented status incomplete");
   Check(ModbusRtu.InferExpectedLength(frame.Take(4).ToArray())==pair.Length,"register response must not truncate at 6 bytes");
   response[3]=5;frame=ModbusRtu.Frame(response.Take(4).ToArray());
   Check(ModbusRtu.InferExpectedLength(frame)==6,"register error response remains six bytes");
  }
  var sh=new ushort[35];sh[0]=1;sh[1]=0x3510;sh[10]=100;sh[12]=150;sh[14]=100;
  sh[16]=100;sh[20]=100;sh[34]=4|8|16;sh[11]=140;sh[13]=25;sh[17]=140;
  var shInfo=new AfeHardwareDeviceInfo(0x3510,4|8|16,true,667,8,32,false,1,0,2) {
   ShuntElementMicroOhm=2000,ShuntParallelCount=3
  };
  Check(shInfo.EffectiveShCurrent(150,10000,15)==150,"D014 exact OCD2 boundary must remain 15A");
  m.Load(new(sh,sh,shInfo));
  Check(m.TryBuildCandidate(out _,out error),"SH recovery is below effective despite equal request: "+error);
  m.Rows.Single(x=>x.WireIndex==14).EditValue="15.0";
  Check(!m.TryBuildCandidate(out _,out _),"SH recovery equal to actual threshold rejected");
  // 实际客户短路写入链：会话内重读、完整35word分片提交及读回；不访问硬件。
  ushort[] customer={1,0x3510,3760,1000,3500,1000,2500,3000,2800,1000,200,140,450,50,10,2000,100,140,0,0,100,100,900,256,2000,1050,850,350,400,1150,1050,0,100,1000,0xdf};
  t.Serial=false;t.HardwareRead=true;t.HardwareRequested=customer.ToArray();t.HardwareEffective=customer.ToArray();
  var client=new AfeHardwareClient(b);var before=await client.ReadAllAsync();t.Reset();
  var after=await client.WriteShortCircuitAsync(before,1350,64);
  var wanted=customer.ToArray();wanted[22]=1350;wanted[23]=64;
  Check(t.Opens==1&&t.Closes==1&&t.Stages==8&&t.Commits==1,"客户写入沿用唯一授权、8分片、1提交和finally关闭");
  Check(after.Requested.SequenceEqual(wanted)&&after.Effective[12]==before.Effective[12],"客户写入保持OCD2及其它33word");
  before=after;t.Reset();t.ChangeOnOpen=true;
  try{await client.WriteShortCircuitAsync(before,1800,96);throw new Exception("过期快照仍提交");}
  catch(IOException ex){Check(ex.Message.Contains("已改变"),"过期快照明确要求重新读取");}
  Check(t.Stages==0&&t.Commits==0&&t.Closes==1,"并发修改后不暂存、不提交，仍关闭会话");t.ChangeOnOpen=false;
  before=await client.ReadAllAsync();t.Reset();t.HardwareEffective![12]++;
  try{await client.WriteShortCircuitAsync(before,1800,96);throw new Exception("OCD2有效基准变化仍提交");}catch(IOException){}
  Check(t.Stages==0&&t.Commits==0&&t.Closes==1,"预读OCD2有效基准变化时未提交");t.HardwareEffective[12]--;
  before=await client.ReadAllAsync();t.Reset();t.ChangeShuntOnOpen=true;
  try{await client.WriteShortCircuitAsync(before,1800,96);throw new Exception("分流器变化仍提交");}catch(IOException){}
  Check(t.Stages==0&&t.Commits==0&&t.Closes==1,"分流器变化必须重读档位");t.ChangeShuntOnOpen=false;t.ShuntElement=2000;
  before=await client.ReadAllAsync();t.Reset();t.EffectiveUnavailable=true;
  try{await client.WriteShortCircuitAsync(before,1800,96);throw new Exception("无效effective仍提交");}catch(IOException){}
  Check(t.Stages==0&&t.Commits==0&&t.Closes==1,"预读有效值失效不得提交");t.EffectiveUnavailable=false;
  before=await client.ReadAllAsync();t.Reset();t.ChangeEffectiveOnCommit=true;
  try{await client.WriteShortCircuitAsync(before,1800,96);throw new Exception("未发现OCD2有效值变化");}
  catch(IOException ex){Check(ex.Message.Contains("OCD2"),"保存后OCD2实际变化应报告未确认");}
  Check(t.Commits==1&&t.Closes==1,"读回不一致不得自动重复提交");t.ChangeEffectiveOnCommit=false;
  t.Reset();t.CommitStatus=4;t.ApplyState=0;t.LastError=2;t.EffectiveUnavailable=true;
  int effectiveReads=t.EffectiveReads;
  try{await client.WriteAllAsync(customer);throw new Exception("提交拒绝未报告");}
  catch(IOException ex){Check(ex.Message.Contains("Validation (2)")&&ex.Message.Contains("Idle (0)"),"0x04 后须读回真实校验错误与状态");}
  Check(t.Commits==1&&t.Closes==1&&t.EffectiveReads==effectiveReads,"失败诊断只读元数据，不依赖有效值且不重复提交");
  t.CommitStatus=0;t.ApplyState=1;t.LastError=0;t.EffectiveUnavailable=false;
  t.Reset();t.MetadataCapabilities=0x1bdf;t.HardwareRequested=customer.ToArray();t.HardwareEffective=customer.ToArray();
  var channels=customer.ToArray();channels[34]=0x18df; // 仅 TS4；保留其它硬件保护。
  after=await client.WriteAllAsync(channels);
  Check(after.Effective[34]==0x18df&&t.Commits==1,"通道开关沿用完整35-word事务并核对有效位");
  t.Reset();t.ChangeTemperatureOnCommit=true;
  try{await client.WriteAllAsync(channels);throw new Exception("通道有效位不一致未报告");}
  catch(IOException ex){Check(ex.Message.Contains("温度通道"),"通道有效位必须独立核对");}
  Check(t.Commits==1,"通道核对失败不得自动重复提交");t.ChangeTemperatureOnCommit=false;
  t.HardwareRequested=customer.ToArray();t.HardwareEffective=customer.ToArray();t.Reset();
  before=await client.ReadAllAsync();t.ChangeOnOpen=true;
  try{await client.WriteAllAsync(before,customer);throw new Exception("完整窗口过期快照仍提交");}
  catch(AfeWriteException ex){Check(ex.Outcome==AfeWriteOutcome.NotApplied,"冲突发生在提交前");}
  Check(t.Commits==0&&t.Stages==0,"完整窗口也必须进行冲突检查");t.ChangeOnOpen=false;
  foreach(var state in new[]{(Apply:0,Error:2,Outcome:AfeWriteOutcome.NotApplied),
      (Apply:2,Error:4,Outcome:AfeWriteOutcome.RollbackSucceeded),(Apply:3,Error:5,Outcome:AfeWriteOutcome.ConfigurationInconsistent)}) {
    t.Reset();t.CommitStatus=4;t.ApplyState=(ushort)state.Apply;t.LastError=(ushort)state.Error;
    try{await client.WriteAllAsync(customer);throw new Exception("失败未报告");}
    catch(AfeWriteException ex){Check(ex.Outcome==state.Outcome,"按明确拒绝和元数据区分结果");}
    Check(t.Commits==1,"失败无自动重发");
  }
  t.Reset();t.CommitStatus=4;t.ApplyState=0;t.LastError=2;t.HardwareFeatures=2;
  try{await client.WriteAllAsync(customer);throw new Exception("明细拒绝未报告");}
  catch(AfeWriteException ex){Check(ex.Message.Contains("word[27]")&&ex.Message.Contains("充电低温"),"读取并显示具体字段");}
  t.HardwareFeatures=0;
  string registerDetail=AfeHardwareClient.DescribeFailureDetail(new ushort[]{1,8,65535,0x45,0x3f,0x7f,3,0x44,0x3c,65535,2});
  Check(registerDetail.Contains("0x45")&&registerDetail.Contains("0x44")&&registerDetail.Contains("未读到"),"分开显示应用与回滚错误");
  t.Reset();t.CommitStatus=0;t.ApplyState=0;t.LastError=2;t.DropCommitAck=true;
  try{await client.WriteAllAsync(customer);throw new Exception("丢失COMMIT确认未报告");}
  catch(AfeWriteException ex){Check(ex.Outcome==AfeWriteOutcome.Unconfirmed,"超时不能把旧Validation元数据当作本次拒绝");}
  Check(t.Commits==1,"结果未确认不重发");t.DropCommitAck=false;t.ApplyState=1;t.LastError=0;
  var off=customer.ToArray();off[34]&=unchecked((ushort)~64);t.Reset();
  try{await client.WriteShortCircuitAsync(new(off,off,shInfo with {Capabilities=0xdf}),1350,64);throw new Exception("客户隐式开启短路");}catch(InvalidOperationException){}
  Check(t.Opens==0&&t.Stages==0&&t.Commits==0,"本地非法候选不得开启会话");
  Check(D008Parameters.FeatureStatusText(new ushort[]{1,2,16,1,0,0}).Contains("温度限制"),"温度阻止原因");
  Check(D008Parameters.FeatureStatusText(new ushort[]{1,2,0,1,0,0}).Contains("运行中"),"实际活动通道");
  Check(D008Parameters.FeatureStatusText(new ushort[]{0,2,0,1,0,0}).Contains("未确认"),"未知状态版本不误报");
  Console.WriteLine("PASS AFE MTU23 roundtrip, old firmware, wrong ACK, cancel, SCD, serial response and balance mask; unavailable effective rejected; customer short complete transaction, stale snapshot, invalid effective and OCD2 readback guards");
 }
}
namespace BmsTool.Windows {
 sealed class BmsBleTransport:IBmsTransport,IBmsMtuTransport {
  public int? NegotiatedMtu=>23;public bool IsConnected=>true;public bool RequiresSerialWakeup=>Serial;
  public string DiscoveryDescription=>"host";public bool Serial,BadAck,CancelAfterStage,BatteryRead,BalanceOff,HardwareRead,EffectiveUnavailable;public CancellationTokenSource? Stop;
  public ushort[]? HardwareRequested,HardwareEffective;
  public bool ChangeOnOpen,ChangeEffectiveOnCommit,ChangeShuntOnOpen;public ushort ShuntElement=2000;
  public byte CommitStatus;public ushort ApplyState=1,LastError,MetadataCapabilities=0xdf,HardwareFeatures;public int EffectiveReads;public bool ChangeTemperatureOnCommit,DropCommitAck;
  public int Opens,Closes,Stages,Commits,MaxLength,Reads,DropReads,Reconnects;public List<byte> Frame=new();
  public event Action<ReadOnlyMemory<byte>>? DataReceived;public event Action<string>? ConnectionProgress{add{}remove{}}
  public Task ReconnectAsync(CancellationToken ct=default){Reconnects++;return Task.CompletedTask;}
  public void Reset(){Opens=Closes=Stages=Commits=MaxLength=0;Frame.Clear();}
  public Task WriteAsync(ReadOnlyMemory<byte> data,CancellationToken ct=default){
   ct.ThrowIfCancellationRequested();var q=data.ToArray();MaxLength=Math.Max(MaxLength,q.Length);byte[] body;
   if(q[1]==3 && HardwareRead){
    ushort address=BinaryPrimitives.ReadUInt16BigEndian(q.AsSpan(2));
    ushort count=BinaryPrimitives.ReadUInt16BigEndian(q.AsSpan(4));
    var values=new ushort[count];
    if(address is 0x2500 or 0x2540) { values[0]=1;values[1]=0x3510;
     if(address==0x2540)EffectiveReads++;
     var source=address==0x2500?HardwareRequested:HardwareEffective;if(source is not null)Array.Copy(source,values,count);
     if(address==0x2540 && EffectiveUnavailable)Array.Fill(values,ushort.MaxValue); }
    else if(address==0x2523) { values[0]=MetadataCapabilities;values[1]=1;values[2]=667;values[3]=8;values[4]=32;values[6]=ApplyState;values[7]=LastError;values[8]=2; }
    else if(address==0x252e) { values[0]=HardwareFeatures; }
    else if(address==0x252f) { values[0]=1;values[1]=5;values[2]=27;values[3]=ushort.MaxValue;values[7]=ushort.MaxValue; }
    else if(address==0x252c) { values[0]=ShuntElement;values[1]=3; }
    else throw new Exception("Unexpected AFE read address");
    body=new byte[3+2*count];body[0]=1;body[1]=3;body[2]=(byte)(2*count);
    for(int i=0;i<count;i++)BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(3+2*i),values[i]);
   }
   else if(q[1]==3 && BatteryRead){
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
   else if(q[1]==3){
    Reads++;if(Reads<=DropReads)return Task.CompletedTask;
    ushort count=BinaryPrimitives.ReadUInt16BigEndian(q.AsSpan(4));
    body=new byte[3+count*2];body[0]=1;body[1]=3;body[2]=(byte)(count*2);
    BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(3),BmsRegisters.RealtimeMagic);
    BinaryPrimitives.WriteUInt16BigEndian(body.AsSpan(5),2);
   }
   else if(q[2]==1){Opens++;if(ChangeOnOpen)HardwareRequested![12]++;if(ChangeShuntOnOpen)ShuntElement++;body=new byte[]{1,0x42,1,0,0,7,0,60,2,0x35,0x10};}
   else if(q[2]==3){Closes++;body=new byte[]{1,0x42,3,0};}
   else if(q[2]==5){Stages++;Frame.AddRange(q.Skip(7).Take(q[6]));body=new byte[]{1,0x42,5,0,q[3],q[4],(byte)(BadAck?0:Frame.Count)};if(CancelAfterStage)Stop!.Cancel();}
   else{Commits++;body=new byte[]{1,0x42,6,CommitStatus};
    if(CommitStatus==0 && HardwareRequested is not null){for(int i=0;i<35;i++)HardwareRequested[i]=BinaryPrimitives.ReadUInt16BigEndian(Frame.ToArray().AsSpan(7+2*i));
     HardwareEffective![3]=2030;HardwareEffective[7]=3010;HardwareEffective[10]=225;HardwareEffective[16]=104;
     HardwareEffective[12]=HardwareRequested[12];HardwareEffective[22]=HardwareRequested[22];HardwareEffective[23]=HardwareRequested[23];
     HardwareEffective[34]=HardwareRequested[34];if(ChangeTemperatureOnCommit)HardwareEffective[34]^=0x100;
     if(ChangeEffectiveOnCommit)HardwareEffective[12]++;}}
   if(q[1]==0x42 && q[2]==6 && DropCommitAck)return Task.CompletedTask;
   DataReceived?.Invoke(ModbusRtu.Frame(body));return Task.CompletedTask;
  }
  private static void CheckBatteryCount(int count){if(count!=63)throw new Exception("battery read window changed");}
  public ValueTask DisposeAsync()=>ValueTask.CompletedTask;
 }
}
