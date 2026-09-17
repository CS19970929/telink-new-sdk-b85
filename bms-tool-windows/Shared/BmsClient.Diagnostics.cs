using System.IO;
namespace BmsTool.Windows;
public sealed partial class BmsClient
{
    // Reuses TransactAsync/_gate. Frames are captured explicitly here so an
    // unrelated write/authorization transaction can never enter the bundle.
    public async Task<DiagnosticCapture> ReadDiagnosticsAsync(bool includeTrace, string endpoint,
        CancellationToken ct=default)
    {
        var c=new DiagnosticCapture {Endpoint=endpoint};
        async Task<ushort[]> Read(ushort address,ushort count) {
            ct.ThrowIfCancellationRequested();
            byte[] req=ModbusRtu.ReadHolding(address,count);
            c.Frames.Add(new(DateTimeOffset.UtcNow,"TX",Convert.ToHexString(req)));
            byte[] rsp=await TransactAsync(req,ct);
            c.Frames.Add(new(DateTimeOffset.UtcNow,"RX",Convert.ToHexString(rsp)));
            return ModbusRtu.ParseRead(rsp,count);
        }
        try {
            ushort[] header;
            try { header=await Read(BmsDiagnostics.Base,16); }
            catch(BmsModbusException ex) when(ex.Function==0x03 && ex.Code is 1 or 2) {
                c.Status="固件不支持诊断";return c;
            }
            if(header[0]!=BmsDiagnostics.Magic) {c.Status="固件不支持诊断（magic 不匹配）";return c;}
            if(header[1]!=BmsDiagnostics.Schema) {c.Status=$"未知诊断版本 {header[1]}";return c;}
            c.Supported=true;
            // The frozen boot block is read separately from the small runtime
            // block: BLE pagination must not require a 200ms sample to stop.
            var words=new ushort[256];
            Array.Copy(header,words,16);
            Array.Copy(await Read(0x2A10,112),0,words,16,112);
            Array.Copy(await Read(0x2A80,64),0,words,128,64);
            ushort[] tail=await Read(BmsDiagnostics.Base,16);
            Array.Copy(tail,words,16);
            BmsDiagnostics.Decode(c,words);
            c.SnapshotConsistent=(words[3]&1)!=0 && unchecked(BmsDiagnostics.U32(tail,6)-BmsDiagnostics.U32(header,6))<=60u*32000u;
            if(!c.SnapshotConsistent)c.Errors.Add("启动快照尚未冻结或采集期间设备重启；请重新采集");
            if(includeTrace && (header[2]&2)!=0) {
                for(int attempt=0;attempt<2;attempt++) {
                    var before=await Read(BmsDiagnostics.Base,16);
                    var trace=new ushort[768];
                    for(int offset=0;offset<768;offset+=120) {
                        ushort n=(ushort)Math.Min(120,768-offset);
                        Array.Copy(await Read((ushort)(BmsDiagnostics.TraceBase+offset),n),0,trace,offset,n);
                    }
                    var after=await Read(BmsDiagnostics.Base,16);
                    c.TraceConsistent=BmsDiagnostics.U32(before,8)==BmsDiagnostics.U32(after,8) &&
                        unchecked(BmsDiagnostics.U32(after,6)-BmsDiagnostics.U32(before,6))<=60u*32000u;
                    c.Trace.Clear();c.Trace.AddRange(BmsDiagnostics.DecodeTrace(trace,BmsDiagnostics.U32(before,8),before[12]));
                    if(c.TraceConsistent)break;
                }
                if(!c.TraceConsistent)c.Errors.Add("Trace 在分页期间改变；已重试一次，本次记录可能缺失");
            }
            c.Status=c.Errors.Count==0?"诊断读取完成":"部分诊断可用";
        }
        catch(OperationCanceledException) {c.Status="采集已取消";c.Errors.Add("取消/连接改变；保留已采集证据");}
        catch(Exception ex) {c.Status="诊断通信/解码失败";c.Errors.Add(ex.GetType().Name+": "+ex.Message);}
        finally {c.FinishedUtc=DateTimeOffset.UtcNow;}
        // Optional evidence is isolated: an unsupported identity field or event
        // read must not discard the already collected fault snapshot.
        if(includeTrace && !ct.IsCancellationRequested) {
            foreach(var item in new[]{("Serial",BmsRegisters.Serial),("Hardware",BmsRegisters.Hardware),("Software",BmsRegisters.Software)}) {
                try {c.Identity[item.Item1]=ModbusRtu.DecodeAscii(await Read(item.Item2,16));}
                catch(Exception ex) {c.Errors.Add(item.Item1+": "+ex.Message);if(ct.IsCancellationRequested)break;}
            }
            if(!ct.IsCancellationRequested)try {c.SoftwareProtectionWords=await Read(0x2100,65);}
            catch(Exception ex){c.Errors.Add("SoftwareProtection: "+ex.Message);}
            if(!ct.IsCancellationRequested)try {c.Events=await Read(0xC008,100);}catch(Exception ex){c.Errors.Add("Events: "+ex.Message);}
        }
        if(c.Errors.Count!=0 && c.Status=="诊断读取完成")c.Status="部分诊断可用";
        c.FinishedUtc=DateTimeOffset.UtcNow;
        return c;
    }
}
