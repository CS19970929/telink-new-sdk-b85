using BmsTool.Windows;
class Program {
 static void Check(bool x,string why){if(!x)throw new Exception(why);}
 static ushort[] Initial(){var a=new ushort[65];for(int g=0;g<12;g++){bool low=g is 1 or 3 or 7 or 9;a[g*5]=(ushort)(low?300:100);a[g*5+1]=200;a[g*5+2]=(ushort)(low?100:300);a[g*5+3]=200;}return a;}
 static async Task Main(){
  var a=Initial();var changes=new Dictionary<ushort,ushort>{{0x2100,400},{0x2101,500},{0x2102,600},{0x2103,550}};
  Check(a[1]<changes[0x2100],"old single-write intermediate invalid");
  var plan=ProtectionBatch.Plan(a,changes);Check(plan.Count==1 && plan[0].Values.SequenceEqual(new ushort[]{400,500,600,550,0}),"atomic group");
  Check(a[0]==100,"no mutation");
  changes[0x2105]=600;changes[0x2106]=500;changes[0x2107]=400;changes[0x2108]=450;
  var b=new BmsClient{Current=a};int verified=0;await b.WriteProtectionChangesAsync(changes,(_,_)=>verified++);
  Check(b.Writes==2&&verified==2&&b.MaxFrame==19,"multiple groups within MTU23");
  var bad=new Dictionary<ushort,ushort>{{0x2100,900}};b.Writes=0;
  try{await b.WriteProtectionChangesAsync(bad,(_,_)=>{});throw new Exception("invalid accepted");}catch(ArgumentException){}
  Check(b.Writes==0,"invalid candidate no writes");
  b.Current=Initial();b.FailAddress=0x2105;
  try{await b.WriteProtectionChangesAsync(changes,(_,_)=>{});throw new Exception("failure accepted");}catch(System.IO.IOException e){Check(e.Message.Contains("此前1组")&&e.Message.Contains("0x2105"),"partial progress");}
  Check(b.Current[0]==400&&b.Current[5]==300,"completed group retained");
  var all=new Dictionary<ushort,ushort>();for(int g=0;g<13;g++)all[(ushort)(0x2104+5*g)]=20;
  Check(ProtectionBatch.Plan(Initial(),all).Count==13,"all groups");
  Console.WriteLine("PASS batch: transient-invalid sequence, 13 groups, 19-byte frames, preflight, partial failure");
 }
}
namespace BmsTool.Windows {
 public sealed partial class BmsClient {
  public ushort[] Current=new ushort[65];public int Writes,MaxFrame;public ushort FailAddress;
  public Task<ushort[]> ReadProtectionAllAsync(CancellationToken ct)=>Task.FromResult((ushort[])Current.Clone());
  public Task<ushort[]> ReadRegistersAsync(ushort a,ushort n,CancellationToken ct)=>Task.FromResult(Current.AsSpan(a-0x2100,n).ToArray());
  public Task WriteRegistersAsync(ushort a,ushort[] v,CancellationToken ct){
   if(a==FailAddress)throw new System.IO.IOException("simulated disconnect");
   var bytes=new byte[v.Length*2];for(int i=0;i<v.Length;i++)System.Buffers.Binary.BinaryPrimitives.WriteUInt16BigEndian(bytes.AsSpan(2*i),v[i]);
   MaxFrame=Math.Max(MaxFrame,ModbusRtu.WriteMultiple(a,bytes).Length);Writes++;Array.Copy(v,0,Current,a-0x2100,v.Length);return Task.CompletedTask;
  }
 }
}
