using System.Buffers.Binary;
using System.Diagnostics;
using System.IO;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Security.Cryptography;

namespace BmsTool.Windows;

public enum OtaTransferMode { Auto, LegacyFast, Extend64 }
public sealed record OtaProgress(double Percent, int SentBytes, int TotalBytes, double BytesPerSecond, TimeSpan? Eta, OtaTransferMode Mode);
public sealed record OtaResult(byte Code, string Name) { public bool IsSuccess => Code == 0; }

public sealed class FirmwareImage
{
    public string FileName { get; }
    public byte[] Bytes { get; }
    public int ImageSize => Bytes.Length;
    public int LegacyPacketCount => (ImageSize + 15) / 16;
    private FirmwareImage(string fileName, byte[] bytes) { FileName = fileName; Bytes = bytes; }
    public static FirmwareImage Load(string path)
    {
        byte[] all = File.ReadAllBytes(path);
        if (all.Length < 0x1C) throw new InvalidDataException("Firmware is too small to contain Telink size field @0x18.");
        uint declared = BinaryPrimitives.ReadUInt32LittleEndian(all.AsSpan(0x18, 4));
        if (declared == 0 || declared > all.Length) throw new InvalidDataException($"Invalid Telink firmware size @0x18: {declared}, file={all.Length}.");
        return new FirmwareImage(Path.GetFileName(path), all.AsSpan(0, checked((int)declared)).ToArray());
    }
}

public sealed class OtaBleTransport : IAsyncDisposable
{
    public static readonly Guid ServiceUuid = Guid.Parse("00010203-0405-0607-0809-0a0b0c0d1912");
    public static readonly Guid CharacteristicUuid = Guid.Parse("00010203-0405-0607-0809-0a0b0c0d2b12");
    private BluetoothLEDevice? _device; private GattDeviceService? _service; private GattCharacteristic? _characteristic; private GattSession? _session;
    public bool IsConnected => _characteristic is not null;
    public bool NotificationsEnabled { get; private set; }
    public int? NegotiatedMtu => _session?.MaxPduSize;
    public event Action<ReadOnlyMemory<byte>>? NotificationReceived;

    public async Task ConnectAsync(ulong address)
    {
        await DisposeConnectionAsync();
        _device = await BluetoothLEDevice.FromBluetoothAddressAsync(address) ?? throw new IOException("Could not open BLE device for OTA.");
        var preferred = await _device.GetGattServicesForUuidAsync(ServiceUuid, BluetoothCacheMode.Uncached);
        if (preferred.Status == GattCommunicationStatus.Success)
        {
            foreach (var service in preferred.Services)
            {
                var c = await FindCharacteristicAsync(service);
                if (c is not null) { await SelectAsync(service, c); foreach (var x in preferred.Services) if (!ReferenceEquals(x, service)) x.Dispose(); return; }
                service.Dispose();
            }
        }
        var all = await _device.GetGattServicesAsync(BluetoothCacheMode.Uncached);
        if (all.Status == GattCommunicationStatus.Success)
        {
            foreach (var service in all.Services)
            {
                var c = await FindCharacteristicAsync(service);
                if (c is not null) { await SelectAsync(service, c); foreach (var x in all.Services) if (!ReferenceEquals(x, service)) x.Dispose(); return; }
                service.Dispose();
            }
        }
        throw new IOException("Telink OTA characteristic was not found.");
    }

    private static async Task<GattCharacteristic?> FindCharacteristicAsync(GattDeviceService service)
    {
        var r = await service.GetCharacteristicsForUuidAsync(CharacteristicUuid, BluetoothCacheMode.Uncached);
        return r.Status == GattCommunicationStatus.Success ? r.Characteristics.FirstOrDefault(c => c.CharacteristicProperties.HasFlag(GattCharacteristicProperties.WriteWithoutResponse) || c.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Write)) : null;
    }

    private async Task SelectAsync(GattDeviceService service, GattCharacteristic c)
    {
        _service = service; _characteristic = c;
        try { _session = await GattSession.FromDeviceIdAsync(_device!.BluetoothDeviceId); if (_session is not null && _session.CanMaintainConnection) _session.MaintainConnection = true; } catch { _session = null; }
        NotificationsEnabled = false;
        if (c.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Notify))
        {
            c.ValueChanged += OnValueChanged;
            var status = await c.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue.Notify);
            NotificationsEnabled = status == GattCommunicationStatus.Success;
            if (!NotificationsEnabled) c.ValueChanged -= OnValueChanged;
        }
    }

    private void OnValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args) { CryptographicBuffer.CopyToByteArray(args.CharacteristicValue, out byte[] bytes); NotificationReceived?.Invoke(bytes); }

    public async Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested(); var c = _characteristic ?? throw new IOException("OTA characteristic is not connected.");
        var option = c.CharacteristicProperties.HasFlag(GattCharacteristicProperties.WriteWithoutResponse) ? GattWriteOption.WriteWithoutResponse : GattWriteOption.WriteWithResponse;
        var status = await c.WriteValueAsync(CryptographicBuffer.CreateFromByteArray(data.ToArray()), option);
        if (status != GattCommunicationStatus.Success) throw new IOException($"OTA BLE write failed: {status}");
    }

    public async ValueTask DisposeAsync() => await DisposeConnectionAsync();
    private Task DisposeConnectionAsync()
    {
        if (_characteristic is not null) _characteristic.ValueChanged -= OnValueChanged;
        _characteristic = null; NotificationsEnabled = false; _session?.Dispose(); _session = null; _service?.Dispose(); _service = null; _device?.Dispose(); _device = null; return Task.CompletedTask;
    }
}

public sealed class TelinkOtaClient
{
    private readonly OtaBleTransport _transport; private TaskCompletionSource<OtaResult>? _resultTcs; private OtaResult? _latestResult;
    public event Action<string>? Log; public event Action<OtaProgress>? Progress;
    public TelinkOtaClient(OtaBleTransport transport) => _transport = transport;

    public async Task<bool> UpgradeAsync(FirmwareImage image, OtaTransferMode requestedMode, CancellationToken ct)
    {
        if (!_transport.IsConnected) throw new IOException("OTA transport is not connected.");
        OtaTransferMode mode = ResolveMode(requestedMode); int payload = mode == OtaTransferMode.Extend64 ? 64 : 16;
        int count = (image.ImageSize + payload - 1) / payload; _latestResult = null; _resultTcs = new(TaskCreationOptions.RunContinuationsAsynchronously); _transport.NotificationReceived += OnNotification;
        try
        {
            Log?.Invoke($"Mode={mode}; MTU={_transport.NegotiatedMtu?.ToString() ?? "unknown"}; notify={_transport.NotificationsEnabled}; payload={payload}; delay=0ms");
            byte[] start = mode == OtaTransferMode.Extend64 ? new byte[]{0x03,0xFF,64,0x00} : new byte[]{0x01,0xFF};
            Log?.Invoke("TX OTA START " + Convert.ToHexString(start)); await _transport.WriteAsync(start, ct);
            if (mode == OtaTransferMode.Extend64 && _transport.NotificationsEnabled)
            {
                var early = await WaitResultAsync(TimeSpan.FromMilliseconds(250), ct); if (early is not null && !early.IsSuccess) throw ResultException(early, "START_EXT");
            }
            var sw = Stopwatch.StartNew(); long lastTicks = 0;
            for (int i=0;i<count;i++)
            {
                ct.ThrowIfCancellationRequested(); ThrowIfRejected(); byte[] packet = BuildData(image, i, payload, mode == OtaTransferMode.Extend64); await _transport.WriteAsync(packet, ct);
                int sent = Math.Min((i+1)*payload, image.ImageSize); long now=sw.ElapsedTicks;
                if (i==count-1 || lastTicks==0 || now-lastTicks >= Stopwatch.Frequency/10)
                {
                    lastTicks=now; double rate=sw.Elapsed.TotalSeconds>0?sent/sw.Elapsed.TotalSeconds:0; TimeSpan? eta=rate>0?TimeSpan.FromSeconds((image.ImageSize-sent)/rate):null; Progress?.Invoke(new OtaProgress(sent*100.0/image.ImageSize,sent,image.ImageSize,rate,eta,mode));
                }
                if (i==0 || i==count-1 || (i+1)%256==0) Log?.Invoke($"TX DATA index={(mode==OtaTransferMode.Extend64?i+1:i)} bytes={sent}/{image.ImageSize}");
                if ((i & 0x3F)==0x3F) await Task.Yield();
            }
            ThrowIfRejected(); ushort lastIndex = mode == OtaTransferMode.Extend64 ? checked((ushort)count) : checked((ushort)(count-1)); byte[] end=BuildEnd(lastIndex);
            Log?.Invoke($"TX OTA END index={lastIndex}"); await _transport.WriteAsync(end,ct);
            if (!_transport.NotificationsEnabled) { Log?.Invoke("OTA_RESULT unavailable; transfer complete but server result unconfirmed."); return false; }
            var final=await WaitResultAsync(TimeSpan.FromSeconds(2),ct); if(final is null){Log?.Invoke("OTA_RESULT timeout; transfer complete but server result unconfirmed.");return false;} if(!final.IsSuccess) throw ResultException(final,"OTA_END"); Log?.Invoke("RX OTA_RESULT 0x00 OTA_SUCCESS"); return true;
        }
        finally { _transport.NotificationReceived -= OnNotification; _resultTcs=null; }
    }

    private OtaTransferMode ResolveMode(OtaTransferMode requested)
    {
        if(requested==OtaTransferMode.LegacyFast)return requested; bool extend=_transport.NegotiatedMtu is >=71;
        if(requested==OtaTransferMode.Extend64 && !extend) throw new IOException($"Extend64 requires MTU >= 71; current={_transport.NegotiatedMtu?.ToString()??"unknown"}.");
        return requested==OtaTransferMode.Extend64 || extend ? OtaTransferMode.Extend64 : OtaTransferMode.LegacyFast;
    }

    private static byte[] BuildData(FirmwareImage image,int index,int payload,bool extended)
    {
        int offset=index*payload, actual=Math.Min(payload,image.ImageSize-offset), padded=extended?Math.Max(16,((actual+15)/16)*16):16; byte[] packet=new byte[2+padded+2];
        ushort wireIndex=checked((ushort)(extended?index+1:index)); BinaryPrimitives.WriteUInt16LittleEndian(packet.AsSpan(0,2),wireIndex); packet.AsSpan(2,padded).Fill(0xFF); image.Bytes.AsSpan(offset,actual).CopyTo(packet.AsSpan(2,actual));
        ushort crc=ModbusRtu.Crc16(packet.AsSpan(0,2+padded)); BinaryPrimitives.WriteUInt16LittleEndian(packet.AsSpan(2+padded,2),crc); return packet;
    }
    private static byte[] BuildEnd(ushort last){ushort inv=(ushort)~last;return new[]{(byte)0x02,(byte)0xFF,(byte)last,(byte)(last>>8),(byte)inv,(byte)(inv>>8)};}
    private void OnNotification(ReadOnlyMemory<byte> data){var s=data.Span;if(s.Length<3||s[0]!=0x06||s[1]!=0xFF)return;var r=new OtaResult(s[2],ResultName(s[2]));_latestResult=r;Log?.Invoke($"RX OTA_RESULT 0x{r.Code:X2} {r.Name}");_resultTcs?.TrySetResult(r);}
    private void ThrowIfRejected(){if(_latestResult is { IsSuccess:false } r)throw ResultException(r,"DATA");}
    private async Task<OtaResult?> WaitResultAsync(TimeSpan timeout,CancellationToken ct){var t=_resultTcs;if(t is null)return null;if(t.Task.IsCompleted)return await t.Task;var delay=Task.Delay(timeout,ct);var winner=await Task.WhenAny(t.Task,delay);if(winner==t.Task)return await t.Task;ct.ThrowIfCancellationRequested();return null;}
    private static IOException ResultException(OtaResult r,string phase)=>new($"{phase} rejected: OTA_RESULT=0x{r.Code:X2} {r.Name}");
    private static string ResultName(byte c)=>c switch{0x00=>"OTA_SUCCESS",0x01=>"OTA_DATA_PACKET_SEQ_ERR",0x02=>"OTA_PACKET_INVALID",0x03=>"OTA_DATA_CRC_ERR",0x04=>"OTA_WRITE_FLASH_ERR",0x05=>"OTA_DATA_INCOMPLETE",0x06=>"OTA_FLOW_ERR",0x07=>"OTA_FW_CHECK_ERR",0x08=>"OTA_VERSION_COMPARE_ERR",0x09=>"OTA_PDU_LEN_ERR",0x0A=>"OTA_FIRMWARE_MARK_ERR",0x0B=>"OTA_FW_SIZE_ERR",0x0C=>"OTA_DATA_PACKET_TIMEOUT",0x0D=>"OTA_TIMEOUT",0x0E=>"OTA_CONNECTION_TERMINATE",0x0F=>"OTA_MCU_NOT_SUPPORTED",0x10=>"OTA_LOGIC_ERR",_=>$"OTA_RESULT_0x{c:X2}"};
}
