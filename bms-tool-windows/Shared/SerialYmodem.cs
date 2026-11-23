using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading.Channels;
namespace BmsTool.Windows;

public sealed record SerialUpgradeProgress(double Percent,int SentBytes,int TotalBytes,string Stage);
public sealed record SerialUpgradeReadback(DeviceIdentity? Identity,BatterySnapshot? Battery,uint? BuildId,
    uint Board,uint Version,uint PayloadCrc,int PayloadLength)
{
    public bool RuntimeDataAvailable => Identity is not null && Battery is not null;
}
public sealed class SerialYmodemClient
{
    private const int ConservativeBlePayload = 20;
    private const int ConservativeBleDelayMs = 15;
    private const int PacketAttempts = 10;
    private const int SerialPacketResponseTimeoutMs = 2000;
    // The Boot can wait 2 s after the last UART byte before rejecting a short frame.
    private const int BlePacketResponseTimeoutMs = 3000;

    private readonly IBmsTransport _transport;
    private readonly bool _ble;
    private readonly Channel<byte> _rx=Channel.CreateBounded<byte>(8192);
    private volatile bool _overflow;
    private bool _useConservativeBle;
    public SerialUpgradeReadback? Verified { get; private set; }
    public event Action<string>? Log;
    public event Action<SerialUpgradeProgress>? Progress;
    public SerialYmodemClient(IBmsTransport transport,bool chunkForBle=false) { _transport=transport;_ble=chunkForBle; }
    private void Receive(ReadOnlyMemory<byte> data)
    {
        foreach(byte b in data.Span) if(!_rx.Writer.TryWrite(b)) _overflow=true;
    }
    private void Drain() { while(_rx.Reader.TryRead(out _)) {} _overflow=false; }
    private int BlePayloadSize
    {
        get
        {
            if (!_useConservativeBle && _transport is IBmsMtuTransport link)
                return Math.Clamp(Math.Min((link.NegotiatedMtu ?? 23) - 3, link.YmodemBlePayloadLimit),
                    ConservativeBlePayload, 120);
            return ConservativeBlePayload;
        }
    }

    private async Task WriteAsync(byte[] data, CancellationToken ct, int? blePayload = null)
    {
        if (!_ble)
        {
            await _transport.WriteAsync(data, ct);
            return;
        }

        int chunk = blePayload ?? BlePayloadSize;
        for (int offset = 0; offset < data.Length; offset += chunk)
        {
            int length = Math.Min(chunk, data.Length - offset);
            var elapsed = Stopwatch.StartNew();
            await _transport.WriteAsync(data.AsMemory(offset, length), ct);
            // At 19200 8N1 the UART drains while GATT waits for its response.
            int delay = chunk > ConservativeBlePayload
                ? (length * 10000 + 19199) / 19200 + 4 - (int)elapsed.ElapsedMilliseconds
                : ConservativeBleDelayMs;
            if (delay > 0) await Task.Delay(delay, ct);
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
    private async Task SendPacketAsync(byte[] packet, bool expectC, CancellationToken ct, bool finalCommit = false)
    {
        int timeoutMs = _ble ? BlePacketResponseTimeoutMs : SerialPacketResponseTimeoutMs;
        string phase = finalCommit ? "commit" : expectC ? "header" : "data";
        for (int attempt = 1; attempt <= PacketAttempts; attempt++)
        {
            ct.ThrowIfCancellationRequested();
            Drain();
            int? payload = _ble ? BlePayloadSize : null;
            var elapsed = Stopwatch.StartNew();
            string reason;
            try
            {
                await WriteAsync(packet, ct, payload);
                // A repeated CRC request may already be queued while a header is being sent.
                var wait = Stopwatch.StartNew();
                byte response;
                do { response = await ControlAsync(Math.Max(1, timeoutMs - (int)wait.ElapsedMilliseconds), ct); }
                while (response == 0x43 && wait.ElapsedMilliseconds < timeoutMs);

                if (response == 0x06)
                {
                    if (expectC) await ExpectAsync(0x43, timeoutMs, ct);
                    return;
                }
                reason = response == 0x15 ? "NAK" : $"unexpected 0x{response:X2}";
            }
            catch (TimeoutException ex)
            {
                reason = ex.Message;
            }

            Log?.Invoke($"YMODEM phase={phase}; block={packet[1]}; attempt={attempt}/{PacketAttempts}; reply={reason}; BLE payload={payload?.ToString() ?? "-"}; elapsed={elapsed.ElapsedMilliseconds}ms");
            if (!finalCommit && payload > ConservativeBlePayload)
            {
                _useConservativeBle = true;
                Log?.Invoke($"BLE 透传未确认，自动切换到每片 {ConservativeBlePayload} 字节、片间等待 {ConservativeBleDelayMs} ms，重发当前块。");
            }
        }
        throw new TimeoutException($"YMODEM 块 {packet[1]}（{phase}）连续未获确认。请在同一设备上重新开始升级。");
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
    private async Task<SerialUpgradeDevice> EnterBootAsync(SerialUpgradeDevice initial,SerialFirmwarePackage package,CancellationToken ct)
    {
        using var deadline=CancellationTokenSource.CreateLinkedTokenSource(ct);deadline.CancelAfter(20000);
        var token=deadline.Token;var current=initial;
        try {
            for(int sent=0;sent<=3;sent++) {
                UpgradeDetection.CheckPackage(current,package);
                if(current.Capacity!=initial.Capacity)throw new InvalidDataException("设备重连后的 APP 容量发生变化，停止升级。");
                if(current.Mode==2)throw new IOException("Boot 安全关断失败，禁止擦写。排除硬件或供电故障后复位再试。");
                if(current.Mode==1)return current;
                if(sent==3)break;
                // Retry ENTER only after fresh, matching APP/window discovery.
                // An absent ACK can mean a dropped request or a completed reset.
                await Task.Delay(UpgradeDetection.ResponseTurnaround,token);
                await using(var client=new BmsClient(_transport,responseTurnaround:UpgradeDetection.ResponseTurnaround)) {
                    try { await client.WriteSingleRegisterAsync(0x3500,0xB007,token,TimeSpan.FromSeconds(6)); }
                    catch(TimeoutException) { Log?.Invoke("ENTER 应答超时，只读重新确认 APP/Boot 后再决定；不切换升级协议。"); }
                    catch(IOException ex) when(ex is not BmsModbusException) { Log?.Invoke("ENTER 链路状态待确认："+ex.Message); }
                }
                await Task.Delay(800,token);
                try { await _transport.ReconnectAsync(token); }
                catch(IOException ex) { Log?.Invoke("等待原设备串口/蓝牙模块恢复："+ex.Message); }
                current=await UpgradeDetection.WaitForYmodemAsync(_transport,token,package);
            }
        } catch(OperationCanceledException) when(!ct.IsCancellationRequested) {
            throw new TimeoutException("20 秒内未确认原设备已进入安全 Boot；停止升级，未发送固件数据。");
        }
        throw new TimeoutException("ENTER 已尝试 3 次，原设备仍未进入安全 Boot；停止升级，未发送固件数据。");
    }
    private async Task CheckRunningImageAsync(BmsClient client,SerialFirmwarePackage package,CancellationToken ct)
    {
        bool d015Legacy=false;ushort[]? info=null;
        try { info=await client.ReadRegistersAsync(0xD17C,8,ct); }
        catch(BmsModbusException ex) when(package.BoardId==D015FirmwarePackage.BoardId && ex.Code is 1 or 2) { d015Legacy=true; }
        catch(TimeoutException) when(package.BoardId==D015FirmwarePackage.BoardId) { d015Legacy=true; }
        // Early D015 answered reserved windows with zero. Its established
        // D130/D13D evidence remains a compatibility path, never a write route.
        if(package.BoardId==D015FirmwarePackage.BoardId && info is not null && info.All(w=>w==0))d015Legacy=true;
        if(d015Legacy) {
            var id=await client.ReadRegistersAsync(0xD130,4,ct);var crc=await client.ReadRegistersAsync(0xD13D,3,ct);
            if((((uint)id[0]<<16)|id[1])!=package.BoardId || (((uint)id[2]<<16)|id[3])!=package.Version ||
               (((uint)crc[0]<<16)|crc[1])!=package.PayloadCrc || crc[2]!=package.PayloadLength)
                throw new InvalidDataException("升级后 D015 兼容身份、版本、CRC 或长度不匹配。");
            return;
        }
        if(info is not { Length:8 } || (((uint)info[0]<<16)|info[1])!=package.Version ||
           (((uint)info[2]<<16)|info[3])!=package.PayloadLength ||
           (((uint)info[4]<<16)|info[5])!=package.PayloadCrc || info[6]!=1 || info[7]!=0)
            throw new InvalidDataException("升级后 APP 的版本、长度、CRC 或有效状态与所选包不一致。");
    }
    private async Task VerifyAppAsync(SerialFirmwarePackage package,SerialUpgradeDevice expected,CancellationToken ct)
    {
        Exception? last=null;
        for(int attempt=0;attempt<10;attempt++) {
            await Task.Delay(attempt==0?5500:700,ct);
            try {
                await _transport.ReconnectAsync(ct);await Task.Delay(UpgradeDetection.ResponseTurnaround,ct);
                await using var client=new BmsClient(_transport,responseTurnaround:UpgradeDetection.ResponseTurnaround);
                var words=await client.ReadRegistersAsync(0xD170,12,ct);
                var device=UpgradeDetection.ParseCapabilities(words) ?? throw new InvalidDataException("重启后未公布有效升级能力。");
                UpgradeDetection.CheckPackage(device,package);
                if(device.Capacity!=expected.Capacity)throw new InvalidDataException("升级后 APP 公布的分区容量与已确认 Boot 不一致。");
                if(device.Mode!=0)throw new IOException("设备尚未返回正常 APP。");
                await CheckRunningImageAsync(client,package,ct);
                DeviceIdentity? identity=null;BatterySnapshot? battery=null;uint? build=null;
                try {
                    identity=await client.ReadIdentityAsync(ct);
                    battery=client.IsC077 ? await client.ReadC077ReadyBatteryAsync(ct) : await client.ReadBatteryAsync(ct);
                    if(package.BoardId==D015FirmwarePackage.BoardId)build=await client.TryReadFirmwareBuildIdAsync(ct);
                } catch(Exception ex) when(ex is IOException or TimeoutException or InvalidDataException) {
                    Log?.Invoke("APP 升级元数据已确认；当前项目的身份/实时视图未完整读取："+ex.Message);
                }
                Verified=new(identity,battery,build,package.BoardId,package.Version,package.PayloadCrc,package.PayloadLength);
                Log?.Invoke($"VERIFIED YMODEM board={package.BoardId:X8} version={package.Version:X8} CRC32={package.PayloadCrc:X8} bytes={package.PayloadLength}; runtime={Verified.RuntimeDataAvailable}");
                return;
            } catch(InvalidDataException) { throw; }
              catch(Exception ex) when(ex is IOException or TimeoutException) { last=ex; }
        }
        throw new IOException("固件发送结束，但未能确认新 APP 的身份和 CRC；不能判定升级成功。",last);
    }
    public async Task<bool> UpgradeAsync(FirmwareImage image,SerialUpgradeDevice device,CancellationToken ct)
    {
        Verified=null;
        _useConservativeBle = false;
        // Validate before ENTER even when this public sender is called directly.
        UpgradeDetection.CheckImage(device,image);
        byte[] file=image.Bytes.ToArray();
        var package=SerialFirmwarePackage.Parse(file);
        UpgradeDetection.CheckPackage(device,package);
        bool subscribed=false,finalBlockAttempted=false;
        try {
            Log?.Invoke($"识别：{device.Evidence}; mode={device.Mode}; package={package.BoardName}");
            Progress?.Invoke(new(0,0,file.Length,"准备安全关断"));
            var boot = await EnterBootAsync(device, package, ct);
            Log?.Invoke($"已确认 Boot：board={boot.Board:X8}; mode={boot.Mode}");
            if (_ble)
                Log?.Invoke($"YMODEM BLE MTU={(_transport as IBmsMtuTransport)?.NegotiatedMtu}; payload={BlePayloadSize}");
            _transport.DataReceived+=Receive;subscribed=true;Drain();await ExpectAsync(0x43,8000,ct);
            byte[] header=new byte[128];Encoding.ASCII.GetBytes("app.update\0"+file.Length.ToString(System.Globalization.CultureInfo.InvariantCulture)).CopyTo(header,0);
            await SendPacketAsync(Packet(0,header,128),true,ct);byte seq=1;
            for(int offset=0;offset<file.Length;offset+=1024) {
                int n=Math.Min(1024,file.Length-offset);
                await SendPacketAsync(Packet(seq++,file.AsSpan(offset,n),1024),false,ct);
                Progress?.Invoke(new((offset+n)*95.0/file.Length,offset+n,file.Length,"传输并校验"));
            }
            await EndDataAsync(ct);ct.ThrowIfCancellationRequested();finalBlockAttempted=true;
            try { await SendPacketAsync(Packet(0,new byte[128],128),false,ct,finalCommit:true); }
            catch(Exception ex) when(ex is TimeoutException or IOException) { Log?.Invoke("最终提交结果待确认："+ex.Message+"；重连核对 APP，不重新擦写。"); }
            _transport.DataReceived-=Receive;subscribed=false;
            Progress?.Invoke(new(98,file.Length,file.Length,"等待重启并核对新 APP"));await VerifyAppAsync(package,device,ct);
            Progress?.Invoke(new(100,file.Length,file.Length,"升级成功，APP 身份与 CRC 已确认"));return true;
        } catch {
            if(subscribed && !finalBlockAttempted) {
                try { using var cancel=new CancellationTokenSource(1000);await WriteAsync(new byte[]{0x18,0x18},cancel.Token); }
                catch(Exception ex) when(ex is IOException or OperationCanceledException or TimeoutException) { }
            }
            if(finalBlockAttempted && Verified is null)Log?.Invoke("最终提交已尝试，当前结果待确认；恢复连接后核对 APP，不能把取消或断线等同于未提交。");
            throw;
        } finally { if(subscribed)_transport.DataReceived-=Receive;Drain(); }
    }
}
