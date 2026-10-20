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
        // 第二次现场失败：旧延时已合法，但 OCD1=5 A 低于 D014 的首档 7.5 A。
        var currentProfile=requested.ToArray();currentProfile[10]=200;currentProfile[14]=10;currentProfile[17]=140;
        model.Load(new(currentProfile,effective,info));
        model.Rows.Single(r=>r.WireIndex==10).EditValue="5";
        Check(!model.TryBuildCandidate(out _,out error) && error.Contains("7.5") && error.Contains("A"),
            "D014 OCD1=5 A 必须在发送前说明最低 7.5 A，不能交给设备返回 0x04："+error);
        Check(model.Rows.Single(r=>r.WireIndex==10).EditValue=="5" && currentProfile[10]==200,
            "非法电流只提示，不自动调整用户输入或已读值");
        Check(model.Rows.Single(r=>r.WireIndex==10).Hint.Contains("7.5"),"选中阈值行应显示当前分流器对应的范围");
        Check(model.Rows.Single(r=>r.WireIndex==10).AllowedCurrentRange=="7.5～120.0 A","表格必须直接显示 D014 OCD1 的硬件允许范围");
        Check(model.Rows.Single(r=>r.WireIndex==14).AllowedCurrentRange=="由设备校验","恢复电流不能误用 OCD1 阈值范围");
        foreach(var limit in new[]{(Word:10,Min:75,Max:1200),(Word:12,Min:150,Max:2400),(Word:16,Min:21,Max:660)}) {
            foreach(int value in new[]{limit.Min-1,limit.Min,limit.Max,limit.Max+1}) {
                model.Load(new(currentProfile,effective,info));
                model.Rows.Single(r=>r.WireIndex==20).EditValue="0";
                model.Rows.Single(r=>r.WireIndex==limit.Word).EditValue=(value/10m).ToString(System.Globalization.CultureInfo.InvariantCulture);
                bool valid=value>=limit.Min && value<=limit.Max;
                if (valid && limit.Word == 12) model.Rows.Single(r=>r.WireIndex==22).EditValue=(info.EffectiveShCurrent((ushort)value,10000,15)*2/10m).ToString(System.Globalization.CultureInfo.InvariantCulture);
                Check(model.TryBuildCandidate(out candidate,out error)==valid,
                    $"SH word {limit.Word} 电流 {value/10m} A 边界判断错误：{error}");
                if(valid) Check(candidate[limit.Word]==value,"合法电流保持用户请求，不自动量化为有效值");
            }
        }
        var anotherShunt=info with { ShuntElementMicroOhm=250, ShuntParallelCount=1 };
        model.Load(new(currentProfile,effective,anotherShunt));
        Check(model.Rows.Single(r=>r.WireIndex==10).Hint.Contains("20.0"),"分流器改变时必须刷新范围，不能固定 D014 的 7.5 A");
        Check(model.Rows.Single(r=>r.WireIndex==10).AllowedCurrentRange=="20.0～320.0 A","更换设备后表格范围必须同步刷新");
        model.Rows.Single(r=>r.WireIndex==22).EditValue="160";
        model.Rows.Single(r=>r.WireIndex==10).EditValue="19.9";
        Check(!model.TryBuildCandidate(out _,out error) && error.Contains("20.0"),"250µΩ 的 OCD1 最小值为 20 A");
        model.Rows.Single(r=>r.WireIndex==10).EditValue="20";
        Check(model.TryBuildCandidate(out _,out error),error);
        var unknownShunt=info with { ShuntElementMicroOhm=0, ShuntParallelCount=0 };
        model.Load(new(currentProfile,effective,unknownShunt));
        model.Rows.Single(r=>r.WireIndex==10).EditValue="5";
        Check(model.TryBuildCandidate(out _,out error),"无精确分流器比例时保留固件判断，不能按667µΩ猜测范围："+error);
        Check(!model.Rows.Single(r=>r.WireIndex==10).Hint.Contains("7.5"),"不能遗留上一设备的范围说明");
        Check(model.Rows.Single(r=>r.WireIndex==10).AllowedCurrentRange=="由设备校验","未知精确分流器时不得显示猜测的范围");
        var singleOcd=currentProfile.ToArray();singleOcd[34]&=unchecked((ushort)~AfeHardwareProtocolMap.CapOcd1);
        model.Load(new(singleOcd,effective,info));
        Check(!model.TryBuildCandidate(out _,out error)&&error.Contains("同时"),"SH 不能只开启一级 OCD");
        var disabledOcd=currentProfile.ToArray();disabledOcd[34]&=unchecked((ushort)~(AfeHardwareProtocolMap.CapOcd1|AfeHardwareProtocolMap.CapOcd2));
        model.Load(new(disabledOcd,effective,info));model.Rows.Single(r=>r.WireIndex==10).EditValue="5";
        Check(model.TryBuildCandidate(out _,out error),"关闭的保护不参与电流档位检查："+error);
        model.Load(new(currentProfile,effective,info));
        var shortRow=model.Rows.Single(r=>r.WireIndex==22);
        Check(shortRow.AllowedCurrentRange=="90.0～270.0 A" && shortRow.Hint.Contains("135.0"),"D014 短路范围由 OCD2=45 A 的硬件基准计算");
        Check(!model.Rows.Any(r=>r.WireIndex==24),"未使用的短路恢复确认不能继续提供编辑项");
        foreach(decimal value in new[]{89.9m,90m,91m,135m,180m,270m,270.1m}) {
            shortRow.EditValue=value.ToString(System.Globalization.CultureInfo.InvariantCulture);
            Check(model.TryBuildCandidate(out candidate,out error)==(value>=90m && value<=270m),"SH 短路边界校验："+error);
            if(value==91m)Check(candidate[22]==910,"允许请求保留原值，由固件向上选档，不私自修改用户输入");
        }
        model.Rows.Single(r=>r.WireIndex==12).EditValue="46";
        Check(shortRow.AllowedCurrentRange=="120.0～360.0 A","短路范围必须随 OCD2 正在编辑的值更新，不能沿用保存值或旧有效值");
        shortRow.EditValue="90";Check(!model.TryBuildCandidate(out _,out error)&&error.Contains("120.0"),"改变 OCD2 后重新检查原短路输入");
        shortRow.EditValue="120";Check(model.TryBuildCandidate(out candidate,out error)&&candidate[12]==460&&candidate[22]==1200,error);
        model.Rows.Single(r=>r.WireIndex==12).EditValue="NaN";Check(shortRow.AllowedCurrentRange=="OCD2 输入无效","编辑未完成或非法字符时不得崩溃或遗留旧范围");
        model.ResetEditsToCurrent();Check(shortRow.AllowedCurrentRange=="90.0～270.0 A","撤销编辑须同步恢复短路范围");
        var integerRatio=info with {ShuntElementMicroOhm=667,ShuntParallelCount=1};
        model.Load(new(currentProfile,effective,integerRatio));shortRow=model.Rows.Single(r=>r.WireIndex==22);
        Check(shortRow.AllowedCurrentRange=="120.0～359.8 A","不能从已取整为60A的OCD2有效报告反推短路上界");
        shortRow.EditValue="359.8";Check(model.TryBuildCandidate(out _,out error),error);
        shortRow.EditValue="359.9";Check(!model.TryBuildCandidate(out _,out error),"请求上界按精确感测压降向下取整");
        var onlyShort=currentProfile.ToArray();onlyShort[34]=AfeHardwareProtocolMap.CapSc;onlyShort[12]=20000;onlyShort[22]=60000;
        var tinyShunt=info with {ShuntElementMicroOhm=10,ShuntParallelCount=1};
        model.Load(new(onlyShort,effective,tinyShunt));Check(model.Rows.Single(r=>r.WireIndex==22).AllowedCurrentRange=="4000.0～6000.0 A","短路范围须排除超过16位0.1A报告位宽的硬件档位");
        Check(model.TryBuildCandidate(out _,out error),error);
        var disabledShort=currentProfile.ToArray();disabledShort[34]&=unchecked((ushort)~AfeHardwareProtocolMap.CapSc);
        model.Load(new(disabledShort,effective,info));model.Rows.Single(r=>r.WireIndex==22).EditValue="0";
        Check(model.TryBuildCandidate(out candidate,out error)&&candidate[24]==currentProfile[24],"关闭短路时不强制阈值，隐藏字段保持原协议值："+error);
        var dvcInfo=info with {BackendModel=0x1124,Capabilities=0x7f,ShuntMicroOhm=200,ShuntElementMicroOhm=0,ShuntParallelCount=0};
        var dvc=currentProfile.ToArray();dvc[1]=0x1124;dvc[34]=0x5f;dvc[20]=10;var dvcEffective=effective.ToArray();dvcEffective[1]=0x1124;
        model.Load(new(dvc,dvcEffective,dvcInfo));shortRow=model.Rows.Single(r=>r.WireIndex==22);
        Check(shortRow.AllowedCurrentRange=="50.0～3150.0 A","DVC 短路阈值由10～630mV和200µΩ换算");
        Check(!model.Rows.Any(r=>new[]{14,15,20,21,24}.Contains(r.WireIndex)),"DVC 当前不用的过流/短路恢复参数不提供误导性编辑项");
        shortRow.EditValue="50";Check(model.TryBuildCandidate(out candidate,out error)&&candidate[14]==dvc[14]&&candidate[15]==dvc[15]&&candidate[20]==dvc[20]&&candidate[21]==dvc[21]&&candidate[24]==dvc[24],"隐藏字段必须原样保留在完整35word事务："+error);
        shortRow.EditValue="49.9";Check(!model.TryBuildCandidate(out _,out error),"DVC 短路低于10mV对应电流时拒绝");
        shortRow.EditValue="3150";Check(model.TryBuildCandidate(out _,out error),error);
        shortRow.EditValue="3150.1";Check(!model.TryBuildCandidate(out _,out error),"DVC 短路超过630mV对应电流时拒绝");
        model.Load(new(dvc,dvcEffective,dvcInfo with {ShuntMicroOhm=400}));
        Check(model.Rows.Single(r=>r.WireIndex==22).AllowedCurrentRange=="25.0～1575.0 A","DVC 分流器变化须刷新短路范围");
        foreach(var limit in new[]{(Word:10,Min:13,Max:3187),(Word:12,Min:200,Max:12800),
            (Word:16,Min:13,Max:3187),(Word:18,Min:200,Max:12800)}) {
            var profile=dvc.ToArray();profile[34]=0x3c;profile[18]=450;profile[19]=4;profile[20]=0;profile[14]=0;
            model.Load(new(profile,dvcEffective,dvcInfo));
            Check(model.Rows.Single(r=>r.WireIndex==limit.Word).AllowedCurrentRange==
                $"{limit.Min/10m:0.0}～{limit.Max/10m:0.0} A","DVC 四种过流范围均须可见");
            foreach(int value in new[]{limit.Min-1,limit.Min,limit.Max,limit.Max+1}) {
                model.Rows.Single(r=>r.WireIndex==limit.Word).EditValue=(value/10m).ToString(System.Globalization.CultureInfo.InvariantCulture);
                Check(model.TryBuildCandidate(out _,out error)==(value>=limit.Min&&value<=limit.Max),"DVC 电流边界："+error);
            }
        }
        model.Load(new(dvc,dvcEffective,dvcInfo with {ShuntMicroOhm=400}));
        Check(model.Rows.Single(r=>r.WireIndex==10).AllowedCurrentRange=="0.7～159.3 A","DVC 范围不能固定为默认200µΩ");
        model.Load(new(dvc,dvcEffective,dvcInfo with {ShuntMicroOhm=0}));
        Check(model.Rows.Single(r=>r.WireIndex==10).AllowedCurrentRange=="由设备校验"&&model.GetShortCircuitChoices().Count==0,"未知DVC分流器不得猜测档位");
        var scProfile=currentProfile.ToArray();scProfile[17]=140;
        model.Load(new(scProfile,effective,info));
        var choices=model.GetShortCircuitChoices();
        Check(choices.Select(c=>c.EffectiveA10).SequenceEqual(new ushort[]{900,1350,1800,2700}),"45A OCD2应提供90/135/180/270A四档");
        foreach(var choice in choices) {
            Check(model.TryBuildShortCircuitCandidate(choice.RequestedA10,64,out candidate,out error),error);
            var expectedShort=scProfile.ToArray();expectedShort[22]=choice.RequestedA10;expectedShort[23]=64;
            Check(candidate.SequenceEqual(expectedShort),"客户短路候选只能修改22/23，OCD2/使能/隐藏字段均保持");
        }
        Check(scProfile[22]==900&&model.Rows.Single(r=>r.WireIndex==22).EditValue=="90.0","客户组装候选不修改源快照或编辑值");
        Check(!model.TryBuildShortCircuitCandidate(1350,577,out _,out _),"SH短路延时上限576µs");
        model.Load(new(currentProfile.Select((v,i)=>i==17?(ushort)100:v).ToArray(),effective,info));
        Check(!model.TryBuildShortCircuitCandidate(1350,64,out _,out error)&&error.Contains("内部工具"),"客户窗口不能夹带历史OCC延时规范化");
        model.Load(new(disabledShort,effective,info));
        Check(!model.TryBuildShortCircuitCandidate(1350,64,out _,out _),"客户短路编辑不能开启原来关闭的保护");
        model.Load(new(scProfile,effective,unknownShunt));
        Check(!model.TryBuildShortCircuitCandidate(1350,64,out _,out _),"未知SH精确比例不能提供客户档位");
        model.Load(new(scProfile,effective,integerRatio));choices=model.GetShortCircuitChoices();
        Check(choices.Count==3&&choices[0].EffectiveA10==1800,"667µΩ下最低2倍档不能由有效0.1A请求选中，应隐藏而非跳到3倍");
        foreach(var choice in choices) {
            ulong baseSense=400000;uint[] multiples={2,3,4,6};
            uint selected=multiples.First(k=>(ulong)choice.RequestedA10*667<=baseSense*k);
            Check(choice.EffectiveA10==(baseSense*selected+666)/667,"SH选项必须与固件向上选档一致");
        }
        model.Load(new(dvc,dvcEffective,dvcInfo));choices=model.GetShortCircuitChoices();
        Check(choices.Count==63&&choices[0].EffectiveA10==500&&choices[62].EffectiveA10==31500,"DVC200µΩ应提供63个独立短路档位");
        Check(model.TryBuildShortCircuitCandidate(1000,1992,out candidate,out error)&&candidate[12]==dvc[12],error);
        Check(!model.TryBuildShortCircuitCandidate(1000,1993,out _,out _),"DVC延时上限1992µs");
        model.Load(new(dvc,dvcEffective,dvcInfo with {ShuntMicroOhm=667}));
        foreach(var choice in model.GetShortCircuitChoices()) {
            uint code=(uint)choice.RequestedA10*667/100000;
            Check(choice.EffectiveA10==code*100000/667,"DVC选项须按现有固件向下量化，而非套用SH向上规则");
        }
        // 本次截图：欠压 2400→2500 合法，整组携带 CHG_UT=-25℃仍应在本地拒绝。
        var latest=currentProfile.ToArray();latest[6]=2400;latest[27]=150;latest[28]=300;
        model.Load(new(latest,effective,info));model.Rows.Single(r=>r.WireIndex==6).EditValue="2500";
        Check(!model.TryBuildCandidate(out _,out error)&&error.Contains("充电温度")&&error.Contains("-20")&&error.Contains("word[27]"),error);
        Check(latest[27]==150&&model.Rows.Single(r=>r.WireIndex==27).EditValue=="-25.0","不能按克隆的温度有效值自动修正阈值");
        model.Rows.Single(r=>r.WireIndex==27).EditValue="-20";
        Check(model.TryBuildCandidate(out candidate,out error)&&candidate[6]==2500&&candidate[27]==200,error);
        latest[25]=700;model.Load(new(latest,effective,info));
        Check(!model.TryBuildCandidate(out _,out error)&&error.Contains("word[25]")&&error.Contains("word[27]"),"一次列出多项跨组范围错误");

        var scalarLimits=new[]{(Word:2,Min:3000,Max:4500),(Word:6,Min:1000,Max:3500),
            (Word:3,Min:140,Max:10010),(Word:7,Min:490,Max:10010),(Word:11,Min:140,Max:10010),
            (Word:13,Min:25,Max:400),(Word:17,Min:140,Max:10010),(Word:23,Min:0,Max:576),
            (Word:25,Min:800,Max:1100),(Word:27,Min:200,Max:500),(Word:29,Min:850,Max:1200),(Word:31,Min:0,Max:500)};
        foreach(var limit in scalarLimits) foreach(int value in new[]{limit.Min-1,limit.Min,limit.Max,limit.Max+1}.Where(v=>v>=0)) {
            var baseline=currentProfile.ToArray();baseline[4]=0;baseline[8]=65535;
            baseline[26]=baseline[30]=0;baseline[28]=baseline[32]=1450;
            model.Load(new(baseline,effective,info));var row=model.Rows.Single(r=>r.WireIndex==limit.Word);
            row.EditValue=row.Decode((ushort)value);
            Check(model.TryBuildCandidate(out _,out error)==(value>=limit.Min&&value<=limit.Max),$"SH word[{limit.Word}]={value}: {error}");
        }
        foreach(int word in new[]{10,14,16,20,25,26,27,28,29,30,31,32}) foreach(string bad in new[]{"NaN","Infinity","1e999","1.001"}) {
            model.Load(new(currentProfile,effective,info));model.Rows.Single(r=>r.WireIndex==word).EditValue=bad;
            Check(!model.TryBuildCandidate(out _,out _),$"word[{word}] 非法数值 {bad} 应拒绝且不抛异常");
        }
        foreach(var limit in scalarLimits.Where(l=>l.Word is 3 or 7 or 11 or 13 or 17 or 23)) {
            var off=currentProfile.ToArray();off[34]=0;off[limit.Word]=(ushort)(limit.Max+1);
            model.Load(new(off,effective,info));Check(!model.TryBuildCandidate(out _,out _),"关闭保护后仍须检查固件无条件延时上限");
        }
        var reserved=dvc.ToArray();reserved[19]=1025;reserved[34]&=unchecked((ushort)~AfeHardwareProtocolMap.CapOcc2);
        model.Load(new(reserved,dvcEffective,dvcInfo with {Capabilities=0x5f}));
        Check(!model.TryBuildCandidate(out _,out error)&&error.Contains("word[19]"),"隐藏延时超限须显示错误，不得因找不到行而崩溃");
        model.Load(new(currentProfile,effective,info));
        foreach(ushort cap in new ushort[]{1,2,12,16,64}) {
            model.Load(new(currentProfile,effective,info));
            var toggle=model.Rows.Single(r=>r.IsSwitch && r.CapabilityMask==cap);
            toggle.SwitchValue=false;
            Check(model.TryBuildCandidate(out candidate,out error),error);
            Check(candidate[34]==(currentProfile[34]&~cap),"保护开关只改变所属位");
            Check(model.DescribeProtectionSwitchChanges(candidate).Contains("关闭"),"关闭须进入确认清单");
            toggle.SwitchValue=true;Check(model.TryBuildCandidate(out candidate,out error),error);
            Check(candidate.SequenceEqual(currentProfile),"再次开启恢复原完整候选");
        }
        var channelInfo=info with {Capabilities=0x1bdf}; // D014: TS1/TS2/TS4，TS3 NC。
        model.Load(new(currentProfile,effective,channelInfo));
        Check(model.Rows.Count(r=>r.IsTemperatureChannel)==4,"四个通道分别显示");
        Check(!model.Rows.Single(r=>r.CapabilityMask==AfeHardwareProtocolMap.CapTs3).CanEdit,"TS3 NC 开关只读");
        Check(model.TryBuildCandidate(out candidate,out error)&&candidate.SequenceEqual(currentProfile),"旧设备读出后不编辑通道，不隐式改写旧mask："+error);
        for(int channels=0;channels<16;channels++) {
            model.Load(new(currentProfile,effective,channelInfo));
            for(int i=0;i<4;i++) model.Rows.Single(r=>r.CapabilityMask==(1<<(8+i))).SwitchValue=(channels&(1<<i))!=0;
            model.Rows.Single(r=>r.WireIndex==34&&r.CapabilityMask==AfeHardwareProtocolMap.CapSc).SwitchValue=false;
            bool valid=(channels&4)==0;
            Check(model.TryBuildCandidate(out candidate,out error)==valid,$"通道组合 {channels}：{error}");
            if(!valid)continue;
            Check(AfeHardwareProtocolMap.TemperatureChannels(candidate[34])==(channels<<8),"通道位独立组合");
            Check((candidate[34]&AfeHardwareProtocolMap.CapSc)==0,"SCD 与通道同时编辑不能互相覆盖同一个word");
            Check(candidate.Take(34).SequenceEqual(currentProfile.Take(34)),"开关不改变其它参数及隐藏字段");
        }
        model.Load(new(currentProfile,effective,info));
        Check(model.Rows.Where(r=>r.IsTemperatureChannel).All(r=>!r.CanEdit),"旧固件显示原通道但禁止独立写入");
        model.Rows.Single(r=>r.CapabilityMask==AfeHardwareProtocolMap.CapTs1).SwitchValue=false;
        Check(!model.TryBuildCandidate(out _,out error)&&error.Contains("只读"),"旧固件不静默忽略用户通道修改");
        var onlyOff=currentProfile.ToArray();onlyOff[27]=150;
        model.Load(new(onlyOff,effective,channelInfo));
        foreach(var row in model.Rows.Where(r=>r.IsTemperatureChannel))row.SwitchValue=false;
        Check(model.TryBuildCandidate(out candidate,out error)&&candidate[27]==150&&candidate[34]==0x105f,error);
        Check(model.DescribeTemperatureChannelChanges(candidate).Contains("所有外部通道"),"确认框明确告知全关范围");
        Console.WriteLine("PASS AFE compatibility: complete scalar/current/short/temperature ranges, shared OCD enable, legacy delay evidence, finite decimal input and preserved hidden words");
    }
}
