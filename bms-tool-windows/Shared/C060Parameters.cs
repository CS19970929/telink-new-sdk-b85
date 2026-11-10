using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.Json;

namespace BmsTool.Windows;

public sealed record C060ParameterGroup(ushort Address, ushort Count, string Name);
public sealed record C060ParameterField(ushort Address, ushort WriteAddress, string Group, string Name,
    string Unit, decimal Scale, decimal Offset, int Minimum, int Maximum,
    bool Editable = true, bool Compatibility = false, bool FactoryOnly = false, bool Signed = false, string Hint = "")
{
    public string Format(ushort raw) => (((Signed ? unchecked((short)raw) : raw) - Offset) / Scale).ToString("0.##########", CultureInfo.InvariantCulture);
    public ushort Parse(string text)
    {
        if(!decimal.TryParse(text,NumberStyles.Float,CultureInfo.InvariantCulture,out decimal value) &&
           !decimal.TryParse(text,NumberStyles.Float,CultureInfo.CurrentCulture,out value))
            throw new ArgumentException(Name+"：请输入有效数值。");
        decimal raw=value*Scale+Offset;
        if(raw!=decimal.Truncate(raw) || raw<Minimum || raw>Maximum)
            throw new ArgumentException($"{Name}：须符合{Unit}精度，编码范围{Minimum}～{Maximum}。");
        return Signed?unchecked((ushort)(short)raw):(ushort)raw;
    }
}
public sealed class C060ParameterRow : INotifyPropertyChanged
{
    public required C060ParameterField Field {get;init;}
    public string Group=>Field.Group;public string Name=>Field.Name;public string Unit=>Field.Unit;
    public string AddressText=>$"0x{Field.Address:X4}";public string Hint=>Field.Hint;
    public ushort? DeviceRaw {get;private set;}
    public string DeviceValue=>DeviceRaw is ushort value?Field.Format(value):"—";
    private string _edit="";
    public string EditValue {get=>_edit;set{_edit=value;Changed(nameof(EditValue));}}
    public event PropertyChangedEventHandler? PropertyChanged;
    void Changed(string name)=>PropertyChanged?.Invoke(this,new PropertyChangedEventArgs(name));
    public void Load(ushort value){DeviceRaw=value;_edit=Field.Format(value);Changed(nameof(DeviceValue));Changed(nameof(EditValue));}
    public void Clear(){DeviceRaw=null;_edit="";Changed(nameof(DeviceValue));Changed(nameof(EditValue));}
}
public sealed class C060ParameterCapture
{
    public int Schema {get;set;}=1;public string Product {get;set;}="C060";
    public DateTimeOffset CapturedUtc {get;set;}=DateTimeOffset.UtcNow;
    public Dictionary<string,ushort[]> Blocks {get;set;}=new();
    public List<string> Errors {get;set;}=new();
    public string[] Incomplete {get;set;}={"分段电流校准未提供全部K/B和端点；此包不是整机无损克隆。"};
}
public static class C060Parameters
{
    public static readonly C060ParameterGroup[] Groups={new(0x2300,8,"均衡"),new(0x2308,8,"电流与SOC表"),new(0x2310,8,"兼容休眠"),new(0x2318,4,"容量与SOC"),new(0x231C,4,"系统"),new(0x2320,24,"兼容热冷")};
    public static readonly IReadOnlyList<C060ParameterField> Fields=CreateFields();
    public static IReadOnlyList<C060ParameterField> FieldsFor(bool c077) => !c077 ? Fields :
        Fields.Select(f=>f.Address switch {
            >=0x2020 and <=0x203F or
            >=0x204E and <=0x204F or >=0x2054 and <=0x205B =>
                f with {Editable=false,Hint="C077未使用该通道；电流仅采用AFE1开机RAM零偏与固定分流换算，缺失串位不参与采样。"},
            0x231C => f with {Hint="C077固定16串，两颗309各采完整16串。"},
            0x230A => f with {Minimum=640,Maximum=8960,Hint="309按64μs向上量化；AFE2再高一级。保存后需明确工厂配置。"},
            0x230B => f with {Maximum=16000,Hint="按0.5mΩ和309短路表量化；AFE2再高一级。保存后需明确工厂配置。"},
            0x231D or 0x231E => f with {Editable=false,Hint="每颗309独立4颗2mΩ并联，固定0.5mΩ。"},
            _ => f
        }).ToArray();
    static IReadOnlyList<C060ParameterField> CreateFields()
    {
        var fields=new List<C060ParameterField>();
        void Add(int a,string name,string unit="原始值",decimal scale=1,decimal offset=0,int min=0,int max=65535,
            bool editable=true,bool compatibility=false,bool factory=false,bool signed=false,string hint="") {
            ushort address=(ushort)a;var group=Groups.Single(g=>address>=g.Address&&address<g.Address+g.Count);
            fields.Add(new(address,group.Address,group.Name,name,unit,scale,offset,min,max,editable,compatibility,factory,signed,hint));
        }
        Add(0x2300,"均衡起始单体电压","mV",min:1000,max:5000);
        Add(0x2301,"均衡启动压差","mV",min:1,max:2000);
        Add(0x2302,"旧均衡关闭压差","mV",max:2000,compatibility:true,hint:"兼容字段；当前充电均衡未使用此停止阈值。");
        for(int a=0x2303;a<=0x2307;a++)Add(a,"均衡保留字段",editable:false,hint:"未编辑值随整组原样保留。");
        Add(0x2308,"计算充电电流上限","A",10,editable:false,hint:"由固件按采样电阻与并联数重算。");
        Add(0x2309,"计算放电电流上限","A",10,editable:false,hint:"由固件按采样电阻与并联数重算。");
        Add(0x230A,"硬件短路延时","μs",10,min:1,max:4000,factory:true,hint:"按50/100/200/400μs档位向上取整；参数回读不代表AFE写入确认。");
        Add(0x230B,"硬件短路电流","A",10,min:1,max:50000,factory:true,hint:"按电阻与AFE硬件档位取整；界面保存前显示有效值。");
        Add(0x230C,"SOC表编号",min:1,max:2,factory:true,hint:"仅切换已有SOC表；不等同于完整化学体系切换。");
        Add(0x230D,"旧密码保留",editable:false);Add(0x230E,"旧限流压差","mV",editable:false);Add(0x230F,"旧限流电流","A",10,editable:false);
        string[] sleepNames={"普通休眠电压","普通休眠时间","低压休眠电压","低压休眠时间","虚拟充电电流","虚拟放电电流","旧RTC唤醒周期","旧RTC时间"};
        for(int i=0;i<8;i++)Add(0x2310+i,sleepNames[i],i is 0 or 2?"mV":i is 4 or 5?"A":"min",i is 4 or 5?10:1,
            compatibility:true,hint:"旧协议兼容字段；当前RTC策略由固件宏和状态机控制。");
        Add(0x2318,"额定容量","Ah",10,min:1,max:65000,hint:"容量组保存会更新SOC参数及学习状态，保留当前SOC百分比。");
        Add(0x2319,"循环次数","次",max:65535);
        Add(0x231A,"SOC 100%单体端点","mV",min:1000,max:5000);
        Add(0x231B,"SOC 0%单体端点","mV",min:1000,max:5000);
        Add(0x231C,"串数","串",editable:false,hint:"当前板采样映射固定20串；不开放改成其它串数。");
        Add(0x231D,"单颗采样电阻","mΩ",min:1,max:65000,factory:true,hint:"需与实际物料一致，影响电流测量和保护。");
        Add(0x231E,"采样电阻并联数","颗",min:1,max:10000,factory:true);
        Add(0x231F,"旧预充时间","s",compatibility:true,hint:"不控制当前最终MOS仲裁的100ms预充。");
        Add(0x2320,"加热开启温度","℃",10,400,max:2000,compatibility:true);
        Add(0x2321,"加热关闭温度","℃",10,400,max:2000,compatibility:true);
        Add(0x2322,"加热开启电流","A",10,max:2000,compatibility:true);
        Add(0x2323,"制冷开启温度","℃",10,400,max:2000,compatibility:true);
        Add(0x2324,"制冷关闭温度","℃",10,400,max:2000,compatibility:true);
        for(int a=0x2325;a<0x2338;a++)Add(a,"热冷保留字段",editable:false,hint:"保留；当前无活跃热冷控制。");
        for(int i=0;i<47;i++) {
            string channel=i<32?$"单体{i+1}":new[]{"AFE1","AFE2","总压","充电电流","放电电流","温度1","温度2","温度3","温度4","温度5","温度6","环境温度1","环境温度2","环境温度3","MOS温度"}[i-32];
            fields.Add(new((ushort)(0x2000+2*i),(ushort)(0x2000+2*i),"通道系数",channel+" K","原始值/1024",1,0,512,1536,FactoryOnly:true,Hint:"1024代表1倍；通道可保存不代表当前固件实际消费该校准系数。"));
            fields.Add(new((ushort)(0x2001+2*i),(ushort)(0x2000+2*i),"通道系数",channel+" B","有符号原始值",1,0,-30000,30000,FactoryOnly:true,Signed:true,Hint:"读取补码，写入符号位+幅值，由客户端转换。"));
        }
        return fields;
    }
    public static bool TryValue(C060ParameterCapture capture,C060ParameterField field,out ushort value)
    {
        string key=field.Address<0x2100?"Calibration":field.WriteAddress.ToString("X4");
        int offset=field.Address-(field.Address<0x2100?0x2000:field.WriteAddress);
        if(capture.Blocks.TryGetValue(key,out var words)&&offset>=0&&offset<words.Length){value=words[offset];return true;}
        value=0;return false;
    }
    public static Dictionary<ushort,ushort> CollectChanges(C060ParameterCapture current,IReadOnlyDictionary<ushort,string> edits,
        bool allowCompatibility=false,bool allowFactory=false)
    {
        var changes=new Dictionary<ushort,ushort>();
        foreach(var edit in edits) {
            var field=FieldsFor(current.Product=="C077").SingleOrDefault(f=>f.Address==edit.Key)??throw new ArgumentException($"未知 C060 参数 0x{edit.Key:X4}");
            ushort raw=field.Parse(edit.Value);
            if(!TryValue(current,field,out ushort old))throw new InvalidDataException(field.Name+"尚未读取，不能保存。");
            if(raw==old)continue;
            if(!field.Editable||field.Compatibility&&!allowCompatibility||field.FactoryOnly&&!allowFactory)
                throw new ArgumentException(field.Name+"在当前权限或兼容设置下不可修改。");
            changes.Add(edit.Key,raw);
        }
        return changes;
    }
    public static void Export(string path,C060ParameterCapture capture)=>File.WriteAllText(path,JsonSerializer.Serialize(capture,new JsonSerializerOptions{WriteIndented=true}),new UTF8Encoding(false));
    public static C060ParameterCapture Import(string path,string expectedProduct="C060")
    {
        if(expectedProduct is not ("C060" or "C077"))throw new ArgumentException("未知参数产品。");
        if(new FileInfo(path).Length>1024*1024)throw new InvalidDataException("参数包超过1MiB。");
        string json=File.ReadAllText(path);
        using var document=JsonDocument.Parse(json);
        if(!document.RootElement.TryGetProperty("Schema",out var schema)||!schema.TryGetInt32(out int version)||version!=1||
           !document.RootElement.TryGetProperty("Product",out var product)||product.ValueKind!=JsonValueKind.String||product.GetString()!=expectedProduct)
            throw new InvalidDataException($"参数包必须明确声明{expectedProduct}及受支持的Schema。");
        var package=JsonSerializer.Deserialize<C060ParameterCapture>(json)??throw new InvalidDataException("参数包为空。");
        if(package.Schema!=1||package.Product!=expectedProduct||package.Blocks is null||package.Blocks.Any(b=>b.Value is null||b.Value.Length>125))throw new InvalidDataException($"参数包产品、版本或块长度不匹配{expectedProduct}。");
        foreach(var group in Groups)if(package.Blocks.TryGetValue(group.Address.ToString("X4"),out var words)&&words.Length!=group.Count)throw new InvalidDataException($"{group.Name}长度错误。");
        if(package.Blocks.TryGetValue("Calibration",out var calibration)&&calibration.Length!=94)throw new InvalidDataException("校准块长度错误。");
        if(package.Blocks.TryGetValue("Protection",out var protection)){if(protection.Length!=65)throw new InvalidDataException("保护块长度错误。");if(expectedProduct=="C077")C077Parameters.ValidateProtection(protection);else ProtectionBatch.ValidateC060(protection);}
        return package;
    }
    // Same units and rounding as ShortCur_Normalize; boundary vectors live in
    // the firmware tests/c060_short_current_vectors.csv (both runners read it).
    public static ushort[] NormalizeCurrentGroup(ushort[] words, ushort resistance, ushort parallel)
    {
        if (words.Length != 8 || resistance == 0 || parallel == 0)
            throw new ArgumentException("短路组/采样参数无效。");
        uint limit = 2000u * parallel / resistance;
        if (limit == 0 || limit > ushort.MaxValue)
            throw new ArgumentException("采样参数导致电流上限不可表示。");

        ushort[] delays = { 700, 1000, 2000, 4000 }; // 0.1 us
        ushort[] thresholds = { 22, 33, 44, 67, 89, 111, 133, 155, 178, 200 }; // mV
        ushort delay = delays.FirstOrDefault(x => x >= words[2], (ushort)4000);
        ushort threshold = thresholds.FirstOrDefault(x =>
            (ulong)words[3] * resistance <= (ulong)x * parallel * 10u, (ushort)200);
        uint actual = (uint)threshold * parallel * 10u / resistance;
        if (actual == 0 || actual > ushort.MaxValue)
            throw new ArgumentException("短路电流有效值不可表示。");

        var result = (ushort[])words.Clone();
        result[0] = result[1] = (ushort)limit;
        result[2] = delay;
        result[3] = (ushort)actual;
        return result;
    }
}

public sealed partial class BmsClient
{
    private readonly SemaphoreSlim _c060ParameterGate=new(1,1);
    public async Task SelectC060ParametersAsync(CancellationToken ct=default)
    {
        var system=await ReadRegistersAsync(0x231C,4,ct);var soc=await ReadRegistersAsync(0x2318,4,ct);
        if(system[0]!=20||system[1]==0||system[2]==0||soc[0]==0)throw new NotSupportedException("读取结果不符合当前C060参数结构；保留连接，请选择匹配项目。");
        IsC060=true;IsC077=false;IsD015=IsSh3520=false;_identityConflict=false;_realtimeAvailable=false;DeviceModel="C060 · 手动选择旧协议";
        DeviceDetectionEvidence="用户明确选择，231C/4与2318/4结构核对通过";
    }
    public void ResetParameterProfile()
    {
        IsC060=IsC077=IsD015=IsSh3520=false;_identityConflict=false;_realtimeAvailable=null;_legacyUnavailable=false;
        _unavailableReads.Clear();DeviceModel="自动识别中";
    }
    public Task<C060ParameterCapture> ReadC060ParametersAsync(CancellationToken ct=default) => ReadC060ParametersAsync(true,ct);
    public async Task<C060ParameterCapture> ReadC060ParametersAsync(bool includeCalibration,CancellationToken ct=default)
    {
        if(!UsesC060Protocol)throw new NotSupportedException("尚未识别为C060；连接保持，可在参数页手动选择项目。");
        var capture=new C060ParameterCapture{Product=ParameterProduct};
        if(IsC077)capture.Incomplete=new[]{"不包含运行快照/日志写回；309 EEPROM仅由显式专用维修事务处理。"};
        foreach(var block in C060Parameters.Groups.Select(g=>(Key:g.Address.ToString("X4"),Address:g.Address,Count:g.Count)).Concat(new (string Key,ushort Address,ushort Count)[]{
            ("Calibration",(ushort)0x2000,(ushort)94),("Protection",(ushort)0x2100,(ushort)65),
            ("Identity",(ushort)0xC002,(ushort)48),("Realtime",(ushort)0xD000,(ushort)63),("SocTable",(ushort)0x2200,(ushort)86),
            ("DischargeSummary",(ushort)0x3000,(ushort)21),("ChargeSummary",(ushort)0x3050,(ushort)21)}).Where(b=>(includeCalibration||b.Key is not ("Calibration" or "DischargeSummary" or "ChargeSummary"))&&(!IsC077||b.Key is not ("DischargeSummary" or "ChargeSummary")))) {
            var words=await TryReadOptionalRegistersAsync(block.Address,block.Count,ct);
            if(words is null)capture.Errors.Add($"{block.Key} 未提供或读取未确认");else capture.Blocks[block.Key]=words;
        }
        if(IsC077) {
            var afe=await TryReadOptionalRegistersAsync(C077Parameters.DiagnosticBase,64,ct);
            if(afe is null)capture.Errors.Add("C077双309诊断未确认");
            else {C077AfeSnapshot.Parse(afe);capture.Blocks["C077Afe"]=afe;}
        }
        return capture;
    }
    public async Task WriteC060ParameterChangesAsync(IReadOnlyDictionary<ushort,ushort> changes,
        Action<ushort,ushort[]> verified,bool allowCompatibility=false,CancellationToken ct=default)
    {
        if(!UsesC060Protocol)throw new NotSupportedException("请先识别或明确选择C060参数协议。");
        await _c060ParameterGate.WaitAsync(ct);
        try {
            var fields=changes.Select(change=>C060Parameters.FieldsFor(IsC077).SingleOrDefault(f=>f.Address==change.Key)??throw new ArgumentException($"不支持的C060参数地址0x{change.Key:X4}")).ToArray();
            foreach(var field in fields) {
                if(!field.Editable||field.Compatibility&&!allowCompatibility)throw new ArgumentException(field.Name+"是保留/兼容字段，当前不允许修改。");
                field.Parse(field.Format(changes[field.Address]));
            }
            var plan=new List<(ushort Address,ushort[] Expected,ushort[] Wire)>();
            foreach(var group in fields.GroupBy(f=>f.WriteAddress)) {
                ushort count=group.Key<0x2100?(ushort)2:C060Parameters.Groups.Single(g=>g.Address==group.Key).Count;
                ushort[] current=await ReadRegistersAsync(group.Key,count,ct),candidate=(ushort[])current.Clone();
                foreach(var field in group)candidate[field.Address-group.Key]=changes[field.Address];
                if(group.Key<0x2100)
                    foreach(var pairField in C060Parameters.Fields.Where(f=>f.WriteAddress==group.Key))pairField.Parse(pairField.Format(candidate[pairField.Address-group.Key]));
                if(group.Key==0x2318&&candidate[3]>=candidate[2])throw new ArgumentException("SOC 0%端点必须低于100%端点。");
                if(group.Key==0x231C&&(candidate[0]!=ProductCellCount||candidate[1]==0||candidate[2]==0||2000u*candidate[2]/candidate[1]>65535))throw new ArgumentException($"{ParameterProduct}固定{ProductCellCount}串，采样电阻/并联数须非零且不能产生溢出。");
                if(group.Key==0x2308) {
                    var system=await ReadRegistersAsync(0x231C,4,ct);
                    for(int i=0;i<4;i++)if(changes.TryGetValue((ushort)(0x231C+i),out ushort value))system[i]=value;
                    if(IsC077) {
                        if(system[0]!=16||system[1]!=2||system[2]!=4)throw new ArgumentException("C077固定16串/0.5mΩ。");
                        candidate=C077Parameters.NormalizeCurrentGroup(candidate,system[1],system[2]);
                        if(candidate[2]!=current[2]||candidate[3]!=current[3])await RequireC077OutputsOffAsync(ct);
                    } else candidate=C060Parameters.NormalizeCurrentGroup(candidate,system[1],system[2]);
                    var roundtrip=IsC077?C077Parameters.NormalizeCurrentGroup(candidate):C060Parameters.NormalizeCurrentGroup(candidate,system[1],system[2]);
                    if(!roundtrip.SequenceEqual(candidate))throw new ArgumentException("该短路档位不能稳定往返，请核对实际采样电阻。");
                }
                if(candidate.SequenceEqual(current))continue;
                if(_transport is IBmsMtuTransport mtu&&(mtu.NegotiatedMtu??23)<9+2*count+3)
                    throw new IOException($"{group.First().Group}整组帧需要MTU≥{12+2*count}；当前通道不足，所有本次参数均未发送。请使用直连串口。");
                var wire=(ushort[])candidate.Clone();
                if(group.Key<0x2100){short offset=unchecked((short)wire[1]);wire[1]=(ushort)(offset<0?0x8000|-offset:offset);}
                plan.Add((group.Key,candidate,wire));
            }
            int completed=0;
            foreach(var group in plan.OrderBy(g=>g.Address==0x231C?0:1)) {
                try {
                    await WriteRegistersAsync(group.Address,group.Wire,ct);
                    var actual=await ReadRegistersAsync(group.Address,(ushort)group.Expected.Length,ct);
                    if(!actual.SequenceEqual(group.Expected))throw new IOException("保存后回读与预计有效参数不一致。");
                    verified(group.Address,actual);completed++;
                } catch(OperationCanceledException ex){throw new OperationCanceledException($"已取消；此前{completed}组确认，当前组可能已保存，请回读。",ex,ct);}
                  catch(Exception ex){throw new IOException($"C060 0x{group.Address:X4}写入/回读失败；此前{completed}组已确认。当前组结果未知，未自动重发。{ex.Message}",ex);}
            }
        } finally {_c060ParameterGate.Release();}
    }
    public async Task WriteC060IdentityAsync(ushort address,string text,CancellationToken ct=default)
    {
        if(!UsesC060Protocol||address is <0xFFF0 or >0xFFF2)throw new ArgumentException("C060生产信息地址无效。");
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
        if(text.Any(char.IsControl))throw new ArgumentException("生产信息不能包含换行或控制字符。");
        byte[] encoded=Encoding.GetEncoding(936,EncoderFallback.ExceptionFallback,DecoderFallback.ExceptionFallback).GetBytes(text);
        if(encoded.Length>32)throw new ArgumentException("生产信息按GBK编码最多32字节。");
        byte[] padded=new byte[32];encoded.CopyTo(padded,0);
        ushort[] words=Enumerable.Range(0,16).Select(i=>(ushort)(padded[2*i]*256+padded[2*i+1])).ToArray();
        int start=(address-0xFFF0)*16;
        var before=await ReadRegistersAsync(0xC002,48,ct);
        if(before.AsSpan(start,16).SequenceEqual(words)){Log?.Invoke("[C060] identity unchanged; no Flash write");return;}
        await WriteRegistersAsync(address,words,ct);
        var actual=await ReadRegistersAsync(0xC002,48,ct);
        if(!actual.AsSpan(start,16).SequenceEqual(words))throw new IOException("生产信息回读不一致；未自动重发。");
    }
}
