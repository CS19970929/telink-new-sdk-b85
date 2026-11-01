using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading.Channels;
namespace BmsTool.Windows;

public sealed record SerialUpgradeProgress(double Percent,int SentBytes,int TotalBytes,string Stage);
public sealed record D015UpgradeReadback(DeviceIdentity Identity, BatterySnapshot Battery, uint? BuildId);
public sealed class D015YmodemClient
{
    private readonly IBmsTransport _transport;
    private readonly bool _ble;
    private readonly Channel<byte> _rx=Channel.CreateBounded<byte>(8192);
    private volatile bool _overflow;
    public D015UpgradeReadback? Verified { get; private set; }
    public event Action<string>? Log;
    public event Action<SerialUpgradeProgress>? Progress;
    public D015YmodemClient(IBmsTransport transport,bool chunkForBle=false) { _transport=transport;_ble=chunkForBle; }
    private void Receive(ReadOnlyMemory<byte> data)
    {
        foreach(byte b in data.Span) if(!_rx.Writer.TryWrite(b)) _overflow=true;
    }
    private void Drain() { while(_rx.Reader.TryRead(out _)) {} _overflow=false; }
    private async Task WriteAsync(byte[] data,CancellationToken ct)
    {
        if(!_ble) { await _transport.WriteAsync(data,ct);return; }
        // UART transparent modules cannot accept a whole 1K block at ATT link speed.
        for(int i=0;i<data.Length;i+=20) {
            int n=Math.Min(20,data.Length-i);
            await _transport.WriteAsync(data.AsMemory(i,n),ct);
            await Task.Delay(15,ct); // 19200 8N1 + module drain margin.
        }
    }
    private async Task<byte> ControlAsync(int timeoutMs,CancellationToken ct)
    {
        using var timeout=CancellationTokenSource.CreateLinkedTokenSource(ct);timeout.CancelAfter(timeoutMs);
        bool can=false;
        try {
            while(true) {
                if(_overflow) throw new IOException("升级接收缓存溢出，停止本次传输。");
                byte b=await _rx.Reader.ReadAsync(timeout.Token);
                if(b==0x18) { if(can)throw new IOException("设备取消升级：请检查升级包、供电及安全关断状态。");can=true;continue; }
                can=false;
                if(b is 0x06 or 0x15 or 0x43)return b;
            }
        } catch(OperationCanceledException) when(!ct.IsCancellationRequested) { throw new TimeoutException("YMODEM 响应超时。"); }
    }
    private async Task ExpectAsync(byte expected,int timeoutMs,CancellationToken ct)
    {
        var watch=Stopwatch.StartNew();
        while(watch.ElapsedMilliseconds<timeoutMs)
            if(await ControlAsync(Math.Max(1,timeoutMs-(int)watch.ElapsedMilliseconds),ct)==expected)return;
        throw new TimeoutException("YMODEM 握手超时。");
    }
    public static byte[] Packet(byte sequence,ReadOnlySpan<byte> payload,int blockBytes)
    {
        if(blockBytes is not (128 or 1024) || payload.Length>blockBytes)throw new ArgumentOutOfRangeException(nameof(blockBytes));
        byte[] packet=new byte[blockBytes+5];packet.AsSpan(3,blockBytes).Fill(0x1A);
        packet[0]=(byte)(blockBytes==128?1:2);packet[1]=sequence;packet[2]=(byte)~sequence;payload.CopyTo(packet.AsSpan(3));
        ushort crc=0;
        foreach(byte b in packet.AsSpan(3,blockBytes)) {
            crc^=(ushort)(b<<8);
            for(int bit=0;bit<8;bit++)crc=(ushort)((crc<<1)^((crc&0x8000)!=0?0x1021:0));
        }
        packet[^2]=(byte)(crc>>8);packet[^1]=(byte)crc;return packet;
    }
    private async Task SendPacketAsync(byte[] packet,bool expectC,CancellationToken ct)
    {
        for(int attempt=0;attempt<10;attempt++) {
            ct.ThrowIfCancellationRequested();Drain();
            try {
                await WriteAsync(packet,ct);
                byte response=await ControlAsync(2000,ct);
                if(response==6) {
                    if(expectC)await ExpectAsync(0x43,2000,ct);
                    return;
                }
            } catch(TimeoutException) { }
            Log?.Invoke($"YMODEM block={packet[1]} retry={attempt+1}/10");
        }
        throw new TimeoutException("YMODEM 数据包连续重试失败。重连后再次开始会从文件起点重新传输。");
    }
    private async Task EndDataAsync(CancellationToken ct)
    {
        for(int attempt=0;attempt<10;attempt++) {
            Drain();await WriteAsync(new byte[]{4},ct);
            try {
                byte reply=await ControlAsync(2000,ct);
                if(reply==0x15) { await WriteAsync(new byte[]{4},ct);reply=await ControlAsync(2000,ct); }
                if(reply==6) { await ExpectAsync(0x43,2000,ct);return; }
            } catch(TimeoutException) { }
        }
        throw new TimeoutException("YMODEM 文件结束握手失败。");
    }
    private async Task VerifyAppAsync(D015FirmwarePackage package,CancellationToken ct)
    {
        await using var client=new BmsClient(_transport);
        Exception? last=null;
        for(int attempt=0;attempt<10;attempt++) {
            await Task.Delay(attempt==0 ? 5500 : 700,ct);
            try {
                await _transport.ReconnectAsync(ct);
                ushort[] id=await client.ReadRegistersAsync(0xD130,4,ct);
                ushort[] crc=await client.ReadRegistersAsync(0xD13D,3,ct);
                if((((uint)id[0]<<16)|id[1])!=D015FirmwarePackage.BoardId ||
                   (((uint)id[2]<<16)|id[3])!=package.Version ||
                   (((uint)crc[0]<<16)|crc[1])!=package.PayloadCrc || crc[2]!=package.PayloadLength)
                    throw new InvalidDataException("升级后板型、版本、整包 CRC 或长度与所选固件不一致。");
                var battery=await client.ReadBatteryAsync(ct);
                var identity=await client.ReadIdentityAsync(ct);
                var build=await client.TryReadFirmwareBuildIdAsync(ct);
                Verified=new(identity,battery,build);
                Log?.Invoke($"VERIFIED D015 version={package.Version:X8} CRC32={package.PayloadCrc:X8} bytes={package.PayloadLength}; BMS readback OK");
                return;
            } catch(InvalidDataException) { throw; }
              catch(Exception ex) when(ex is IOException or TimeoutException) { last=ex; }
        }
        throw new IOException("固件发送结束，但未能回读确认新 APP；不能判定升级成功。请重连后核对设备。",last);
    }
    public async Task<bool> UpgradeAsync(FirmwareImage image,SerialUpgradeDevice device,CancellationToken ct)
    {
        Verified=null;
        if(!image.IsD015Package) throw new InvalidDataException("新 D015 Boot 要求 .d015 升级包。");
        if(device.Protocol!=SerialUpgradeProtocol.D015Ymodem)throw new ArgumentException("D015 YMODEM device required");
        byte[] file=image.Bytes.ToArray();var package=D015FirmwarePackage.Parse(file);
        bool subscribed=false, finalBlockAttempted=false;
        try {
            Log?.Invoke($"识别：{device.Evidence}; mode={device.Mode}; board={device.Board:X8}");
            Progress?.Invoke(new(0,0,file.Length,"准备安全关断"));
            if(device.Mode==2) throw new IOException("Boot 安全关断失败，禁止擦写。排除硬件或供电故障后复位再试。");
            if(device.Mode!=1) {
                await using var client=new BmsClient(_transport);
                try { await client.WriteSingleRegisterAsync(0x3500,0xB007,ct,TimeSpan.FromSeconds(6)); }
                catch(TimeoutException) { Log?.Invoke("ENTER ACK 超时，等待已确认设备的 Boot 握手；不改用其他协议。"); }
                // Reset can cycle the UART bridge supply; keep the same device, rebuild its link.
                await Task.Delay(800,ct);
                try { await _transport.ReconnectAsync(ct); }
                catch(IOException ex) { Log?.Invoke("等待原设备串口桥恢复："+ex.Message); }
            }
            var boot=await UpgradeDetection.WaitForD015Async(_transport,ct);
            if(boot.Board!=device.Board || boot.Address!=device.Address || boot.Capacity!=device.Capacity || boot.Mode!=1)
                throw new IOException("重连后不是已确认且允许写入的 D015 Boot；停止升级。");
            _transport.DataReceived+=Receive;subscribed=true;Drain();
            await ExpectAsync(0x43,8000,ct);
            byte[] header=new byte[128];Encoding.ASCII.GetBytes("app.d015\0"+file.Length.ToString(System.Globalization.CultureInfo.InvariantCulture)).CopyTo(header,0);
            await SendPacketAsync(Packet(0,header,128),true,ct);
            byte seq=1;
            for(int offset=0;offset<file.Length;offset+=1024) {
                int n=Math.Min(1024,file.Length-offset);
                await SendPacketAsync(Packet(seq++,file.AsSpan(offset,n),1024),false,ct);
                Progress?.Invoke(new((offset+n)*95.0/file.Length,offset+n,file.Length,"传输并校验"));
            }
            await EndDataAsync(ct);
            ct.ThrowIfCancellationRequested();
            finalBlockAttempted=true;
            try { await SendPacketAsync(Packet(0,new byte[128],128),false,ct); }
            catch(Exception ex) when(ex is TimeoutException or IOException) { Log?.Invoke("最终提交结果待确认："+ex.Message+"；重连回读 APP，不重新擦写。"); }
            _transport.DataReceived-=Receive;subscribed=false;
            Progress?.Invoke(new(98,file.Length,file.Length,"等待重启并核对新 APP"));
            await VerifyAppAsync(package,ct);
            Progress?.Invoke(new(100,file.Length,file.Length,"升级成功，身份和运行数据已核对"));
            return true;
        } catch {
            if(subscribed && !finalBlockAttempted) {
                try { using var cancel=new CancellationTokenSource(1000);await WriteAsync(new byte[]{0x18,0x18},cancel.Token); }
                catch(Exception ex) when(ex is IOException or OperationCanceledException or TimeoutException) { }
            }
            if(finalBlockAttempted && Verified is null)
                Log?.Invoke("最终提交已尝试，当前结果待确认；恢复连接后核对 APP，不能把取消或断线等同于未提交。");
            throw;
        } finally { if(subscribed)_transport.DataReceived-=Receive;Drain(); }
    }
}
