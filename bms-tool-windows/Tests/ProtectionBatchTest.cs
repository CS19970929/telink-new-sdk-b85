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
  var peer=new Peer{Protection=a};await using var b=new BmsClient(peer);int verified=0;await b.WriteProtectionChangesAsync(changes,(_,_)=>verified++);
  Check(peer.Writes==2&&verified==2&&peer.WriteLengths.Max()==19,"multiple groups within MTU23");
  var bad=new Dictionary<ushort,ushort>{{0x2100,900}};peer.Writes=0;
  try{await b.WriteProtectionChangesAsync(bad,(_,_)=>{});throw new Exception("invalid accepted");}catch(ArgumentException){}
  Check(peer.Writes==0,"invalid candidate no writes");
  peer.Protection=Initial();peer.FailWriteAddress=0x2105;
  try{await b.WriteProtectionChangesAsync(changes,(_,_)=>{});throw new Exception("failure accepted");}catch(System.IO.IOException e){Check(e.Message.Contains("此前1组")&&e.Message.Contains("0x2105"),"partial progress");}
  Check(peer.Protection[0]==400&&peer.Protection[5]==300,"completed group retained");
  var all=new Dictionary<ushort,ushort>();for(int g=0;g<13;g++)all[(ushort)(0x2104+5*g)]=20;
  Check(ProtectionBatch.Plan(Initial(),all).Count==13,"all groups");
  var d015=Initial();
  Check(ProtectionBatch.Plan(d015,new Dictionary<ushort,ushort>{{0x2101,0}},true).Count==1,"D015 per-level disable");
  Console.WriteLine("PASS batch: transient-invalid sequence, 13 groups, 19-byte frames, preflight, partial failure");
 }
}
