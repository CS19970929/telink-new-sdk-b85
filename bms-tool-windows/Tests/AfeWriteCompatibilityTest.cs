using System.IO;
using BmsTool.Windows;

static class AfeWriteCompatibilityTest
{
    static void Check(bool ok, string message) { if (!ok) throw new Exception(message); }

    static void Main()
    {
        // 2026-10-20 D014 现场 35-word 读回；不包含身份或授权数据，不访问设备。
        ushort[] requested = { 1,0x3510,3760,1000,3500,1000,2500,3000,2800,1000,
            250,140,450,50,100,2000,100,100,0,0,100,100,900,256,2000,
            1050,850,350,400,1150,1050,0,100,1000,0xdf };
        ushort[] effective = { 1,0x3510,3760,2030,3500,1000,2500,3010,2800,1000,
            300,140,450,50,100,2000,104,140,0,0,100,100,900,256,2000,
            1050,850,350,400,1150,1050,0,100,1000,0xdf };
        var info = new AfeHardwareDeviceInfo(0x3510,0xdf,true,667,8,32,false,0,2,2) {
            ShuntElementMicroOhm=2000, ShuntParallelCount=3
        };
        var model = new AfeHardwareParameterModel();
        model.Load(new(requested,effective,info));
        model.Rows.Single(r=>r.WireIndex==10).EditValue="10";
        model.Rows.Single(r=>r.WireIndex==14).EditValue="1";
        Check(model.TryBuildCandidate(out var candidate,out var error),error);
        Check(candidate[10]==100 && candidate[14]==10,"10 A / 1 A 必须按原协议编码");
        Check(candidate[17]==140,"历史 OCC1=100ms 须按已读回的 140ms 规范化，否则固件整组拒绝");
        string corrections=model.DescribeLegacyDelayCorrections(candidate);
        Check(corrections.Contains("充电过流") && corrections.Contains("100 → 140 ms"),
            "保存确认必须能列出实际携带的历史延时规范化项");
        var expected=requested.ToArray(); expected[10]=100;expected[14]=10;expected[17]=140;
        Check(candidate.SequenceEqual(expected),"只修改用户选择的电流及等效历史延时");
        Check(requested[17]==100 && model.Rows.Single(r=>r.WireIndex==17).EditValue=="100",
            "组装候选不得修改已读值和本地编辑值，取消确认不会留下隐式修改");

        var limits=new[]{(Word:3,Min:(ushort)140),(Word:7,Min:(ushort)490),
            (Word:11,Min:(ushort)140),(Word:13,Min:(ushort)25),(Word:17,Min:(ushort)140)};
        foreach(var limit in limits) {
            var legacy=requested.ToArray();var actual=effective.ToArray();
            legacy[limit.Word]=(ushort)(limit.Min-1);actual[limit.Word]=limit.Min;
            model.Load(new(legacy,actual,info));
            Check(model.TryBuildCandidate(out candidate,out error),error);
            Check(candidate[limit.Word]==limit.Min,"未编辑旧延时可按已确认有效值规范化");

            actual[limit.Word]=0;
            model.Load(new(legacy,actual,info));
            Check(!model.TryBuildCandidate(out _,out error) && error.Contains("ms"),
                "没有相同有效值证据时，不能自动改写旧延时");

            legacy[limit.Word]=limit.Min;actual[limit.Word]=limit.Min;
            model.Load(new(legacy,actual,info));
            model.Rows.Single(r=>r.WireIndex==limit.Word).EditValue=(limit.Min-1).ToString();
            Check(!model.TryBuildCandidate(out _,out error) && error.Contains("ms"),
                "主动输入低于最小范围的延时必须明确拒绝，不能自动纠正");
        }
        var disabled=requested.ToArray();disabled[34]&=unchecked((ushort)~AfeHardwareProtocolMap.CapOcc1);
        model.Load(new(disabled,effective,info));
        Check(model.TryBuildCandidate(out candidate,out error) && candidate[17]==100,
            "关闭的保护不应被附带规范化："+error);
        Check(info.EffectiveShCurrent(100,5000,15)==150,"D014 请求 10A 的 OCD1 实际量化值仍为 15A");
        Console.WriteLine("PASS D014 AFE write compatibility: captured 10A/1A, unchanged effective-equivalent legacy delays, explicit invalid edit rejection, unrelated words and cancel preserved");
    }
}
