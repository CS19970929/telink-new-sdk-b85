using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using BmsTool.Windows;

if (args.Length is <2 or >3) throw new ArgumentException("参数为板卡 MAC 和新建的 JSON 证据文件路径");
ulong mac = ulong.Parse(args[0].Replace(":", ""), NumberStyles.HexNumber);
using var output = new FileStream(args[1], FileMode.CreateNew, FileAccess.Write);
using var timeout = new CancellationTokenSource(TimeSpan.FromMinutes(6));
var ct = timeout.Token;
var ble = new BmsBleTransport();
await using var client = new BmsClient(ble);
var stages = new List<object>();
var records = new List<object>();
string? failure = null;
static uint Pair(ushort[] w,int i) => ((uint)w[i] << 16) | w[i+1];
static long Wide(ushort[] w,int i) => unchecked((long)(((ulong)Pair(w,i)<<32) | Pair(w,i+2)));
static void Check(bool good,string reason) { if (!good) throw new InvalidDataException(reason); }
async Task VerifyBoard()
{
    var battery = await client.ReadBatteryAsync(ct);
    var mos = await client.ReadRegistersAsync(0x2f10,8,ct);
    Check(battery.ValidCellCount == 16 && mos[0]==0x4d43 && mos[2]==0 && mos[3]==0 &&
          mos[4]==0 && mos[5]==1 && mos[7]==0, "实板 AFE 或 MOS 状态异常");
    records.Add(new {battery.PackVoltageV,battery.CurrentA,battery.CellMillivolts,battery.SocPercent,
                     battery.MosTempC,battery.MinTempC,battery.MaxTempC,battery.SohDisplay,mos});
}
var timer = Stopwatch.StartNew();
async Task<(ushort[] w,double midpoint,double rtt)> ReadClock()
{
    double sent=timer.Elapsed.TotalMilliseconds;
    var words=await client.ReadRegistersAsync(0x7c00,28,ct);
    double received=timer.Elapsed.TotalMilliseconds;
    return (words,(received+sent)/2,received-sent);
}
async Task Ride(bool awake,ushort scale)
{
    await client.WriteSingleRegisterAsync(0x7c20,(ushort)(awake?1:0),ct);
    await client.WriteSingleRegisterAsync(0x7c21,scale,ct);
    var session=await client.SocHilOpenAsync(75,false,ct);
    try {
        var start=await ReadClock();
        int previous=0;
        long numerator=0, lastTransition=0;
        decimal maxError=0;
        uint lastApplied=0;
        (string label,int ma,int hold)[] route = {
            ("静置",0,900),("起步",5200,1200),("加速",18500,1400),("爬坡",32000,1950),
            ("坡顶",14000,1000),("巡航",6500,1800),("二次加速",22000,1300),("陡坡",30000,1600),
            ("缓坡",9000,1000),("滑行",0,1100),("轻制动",-3500,950),("再生制动",-7500,1350),
            ("停车",0,1350),("再起步",15000,1100),("巡航",7500,1400),("到站",0,1200)
        };
        byte seq=0;
        foreach (var step in route) {
            ushort low=(ushort)(3300-Math.Max(0,step.ma)/230);
            var input=SocHilInput.Normal(step.ma,low,(ushort)(low+14),650,
                                         chargerPresent:false,loadPresent:step.ma>0);
            uint before=await client.SocHilSetSampleAsync(session.Token,++seq,input,ct);
            await Task.Delay(step.hold,ct);
            var state=await client.SocHilReadStatusAsync(session.Token,ct);
            Check(state.SampleReady && state.Sequence==seq && state.AppliedCount>before && state.CurrentMa==step.ma,
                  "注入样本未生效");
            var (w,_,_)=await ReadClock();
            long transition=checked(2*Wide(w,22)+Pair(w,26));
            long end=checked(2*Wide(w,10));
            Check(transition>=lastTransition && end>=transition && Pair(w,18)>lastApplied,"采样时刻未前进");
            numerator=checked(numerator+(transition-lastTransition)*previous);
            decimal reference=(numerator+(end-transition)*step.ma)/32000m;
            long measured=Wide(w,14);
            decimal error=Math.Abs(reference-measured);
            maxError=Math.Max(error,maxError);
            records.Add(new {awake,scale,step.label,step.ma,seq,reference_uAs=reference,measured_uAs=measured,
                             error_uAs=error,tick=end/2,transition,display=state.SocDisplay});
            Check(error<2,"与独立梯形积分差异 "+error+" µA·s");
            lastApplied=Pair(w,18); lastTransition=transition; previous=step.ma;
        }
        var finish=await ReadClock();
        uint exits=unchecked(Pair(finish.w,4)-Pair(start.w,4));
        uint missed=unchecked(Pair(finish.w,6)-Pair(start.w,6));
        double host=finish.midpoint-start.midpoint;
        uint board=unchecked(Pair(finish.w,0)-Pair(start.w,0));
        double uncertainty=200+(start.rtt+finish.rtt)/2;
        stages.Add(new {awake,scale,host_ms=host,board_ms=board,clock_uncertainty_ms=uncertainty,exits,missed,
                        maxError_uAs=maxError,net_mAh=Wide(finish.w,14)/3600000m,
                        sampleCount=Pair(finish.w,18),virtual_s=Wide(finish.w,10)/16000000m});
        Check(missed==0 && (awake?exits==0:exits>0),"采样漏期或 Suspend 对照未成立");
        Check(Math.Abs(host-board)<=uncertainty+host*0.005,"板端计时超出测量区间");
    } finally { await client.SocHilCloseAsync(session.Token,CancellationToken.None); }
}
bool control=false;
try {
    await ble.ConnectAsync(mac,ct);
    await VerifyBoard();
    if (args.Length==3) {
        var mode=args[2];
        if (mode!="journal-start" && mode!="journal-read" && mode!="journal-reboot")
            throw new ArgumentException("未知台架测试");
        var before=await client.ReadRegistersAsync(0x7c30,15,ct);
        Check(before[14]==0xd008 && Pair(before,12)==0x1360c8,"与已核对的台架 Flash 不符");
        if (mode!="journal-read") {
            Check(before[0]==0,"台架已启动");
            await client.WriteSingleRegisterAsync(0x7c22,0xd008,ct);
            await Task.Delay(2200,ct);
        }
        var w=await client.ReadRegistersAsync(0x7c30,15,ct);
        stages.Add(new {mode,state=w[0],cfg=w[1],soc=w[2],events=w[3],programs=w[4],erases=w[5],
                        retries=w[6],missed=w[7],maxOperation_ms=Pair(w,8)/16000m,
                        elapsed_ms=Pair(w,10),mid=Pair(w,12).ToString("X6")});
        Check(w[0] is >=1 and <=4,"台架拒绝或校验失败");
        if (mode=="journal-reboot") Check(w[0]==4 && w[4]==0 && w[5]==0,"复位后只读恢复失败");
        if (mode=="journal-read" && w[0]==4)
            Check(w[1]==33 && w[2]==129 && w[3]==257 && w[4]==838 && w[5]==3 && w[7]==0,
                  "实物轮转、磨损或采样漏期不符");
    } else {
        await client.ReadRegistersAsync(0x7c00,28,ct);
        control=true;
        await Ride(true,1);
        await Ride(false,1);
        await Ride(false,60);
        await VerifyBoard();
    }
} catch (Exception ex) { failure=ex.Message; }
finally {
    if (control) {
        try { await client.WriteSingleRegisterAsync(0x7c20,0,CancellationToken.None); }
        catch (Exception ex) { failure=(failure+" 恢复 Suspend 失败 "+ex.Message).Trim(); }
    }
    string json=JsonSerializer.Serialize(new {mac=args[0],stages,records,failure},new JsonSerializerOptions{WriteIndented=true});
    using var writer=new StreamWriter(output);
    writer.WriteLine(json);
    Console.WriteLine(JsonSerializer.Serialize(new {stages,failure}));
}
return failure is null?0:1;
