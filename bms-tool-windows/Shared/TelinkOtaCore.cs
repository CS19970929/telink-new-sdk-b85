using System.Buffers.Binary;
using System.Diagnostics;
using System.IO;

namespace BmsTool.Windows;

public enum OtaTransferMode { Auto, LegacyFast, Extend64 }
public enum OtaTargetKind { Auto, Telink, Stm32SerialIap }
public sealed record OtaProgress(double Percent, int SentBytes, int TotalBytes, double BytesPerSecond, TimeSpan? Eta, OtaTransferMode Mode);
public sealed record OtaResult(byte Code, string Name) { public bool IsSuccess => Code == 0; }

public sealed record OtaTransportPolicy(
    TimeSpan StartPreparationDelay,
    TimeSpan LegacyPacketPacingDelay,
    int LegacyAckBarrierPackets,
    TimeSpan EndDrainDelay,
    TimeSpan ResultTimeout)
{
    public static OtaTransportPolicy WindowsWinRt { get; } = new(
        TimeSpan.FromMilliseconds(500),
        TimeSpan.FromMilliseconds(5),
        512,
        TimeSpan.FromMilliseconds(50),
        TimeSpan.FromSeconds(5));

    public static OtaTransportPolicy AndroidGatt { get; } = new(
        TimeSpan.FromMilliseconds(50),
        TimeSpan.Zero,
        0,
        TimeSpan.FromMilliseconds(50),
        TimeSpan.FromSeconds(8));
}

public interface ITelinkOtaTransport : IAsyncDisposable
{
    bool IsConnected { get; }
    bool NotificationsEnabled { get; }
    int? NegotiatedMtu { get; }
    OtaTransportPolicy Policy { get; }
    event Action<ReadOnlyMemory<byte>>? NotificationReceived;
    Task WriteAsync(ReadOnlyMemory<byte> data, CancellationToken ct);
    Task WriteWithResponseAsync(ReadOnlyMemory<byte> data, CancellationToken ct);
}

public sealed record FirmwareInspection(string FileName, int FileSize, bool LooksLikeTelink, bool FitsStm32App)
{
    public string Description => LooksLikeTelink && FitsStm32App
        ? $"{FileName} · {FileSize:N0} bytes · 可用于 Telink 或 STM32（按目标架构选择）"
        : LooksLikeTelink
            ? $"{FileName} · {FileSize:N0} bytes · Telink"
            : FitsStm32App
                ? $"{FileName} · {FileSize:N0} bytes · STM32 APP BIN（向量表有效）"
                : $"{FileName} · {FileSize:N0} bytes · 不满足已知 OTA 边界";
}

public sealed class FirmwareImage
{
    public const int Stm32AppCapacity = 55 * 1024;
    public const uint Stm32AppStartAddress = 0x08001C00;
    public const uint Stm32AppEndAddress = 0x0800F800;
    private const uint Stm32SramStartAddress = 0x20000000;
    private const uint Stm32SramEndAddress = 0x20002000;

    public string FileName { get; }
    public byte[] Bytes { get; }
    public OtaTargetKind TargetKind { get; }
    public int ImageSize => Bytes.Length;
    public int LegacyPacketCount => (ImageSize + 15) / 16;
    public bool HasD008TlnkStartupMarker => TargetKind == OtaTargetKind.Telink && TelinkOtaProtocol.HasD008TlnkStartupMarker(Bytes);
    public bool HasValidTelinkCrcTrailer => TargetKind == OtaTargetKind.Telink && TelinkOtaProtocol.HasValidTelinkCrcTrailer(Bytes);

    private FirmwareImage(string fileName, byte[] bytes, OtaTargetKind targetKind)
    {
        FileName = fileName;
        Bytes = bytes;
        TargetKind = targetKind;
    }

    public static FirmwareInspection Inspect(string path)
    {
        byte[] all = File.ReadAllBytes(path);
        bool telink = all.Length >= 0x1C &&
                      BinaryPrimitives.ReadUInt32LittleEndian(all.AsSpan(0x18, 4)) is uint declared &&
                      declared >= 0x20 && declared <= all.Length;
        bool stm32 = all.Length >= 8 && all.Length <= Stm32AppCapacity && IsValidStm32Vector(all);
        return new FirmwareInspection(Path.GetFileName(path), all.Length, telink, stm32);
    }

    public static FirmwareImage Load(string path)
    {
        byte[] all = File.ReadAllBytes(path);
        if (all.Length < 0x1C) throw new InvalidDataException("Firmware is too small to contain Telink size field @0x18.");
        uint declared = BinaryPrimitives.ReadUInt32LittleEndian(all.AsSpan(0x18, 4));
        if (declared < 0x20 || declared > all.Length) throw new InvalidDataException($"Invalid Telink firmware size @0x18: {declared}, file={all.Length}.");
        return new FirmwareImage(Path.GetFileName(path), all.AsSpan(0, checked((int)declared)).ToArray(), OtaTargetKind.Telink);
    }

    public static FirmwareImage LoadStrictTelink(string path)
    {
        FirmwareImage image = Load(path);
        if (!image.HasD008TlnkStartupMarker)
            throw new InvalidDataException("Telink firmware is missing the TLNK startup marker at offset 0x08.");
        if (image.ImageSize > TelinkOtaProtocol.D008DefaultMaxFirmwareBytes)
            throw new InvalidDataException($"Telink firmware is too large: {image.ImageSize} > {TelinkOtaProtocol.D008DefaultMaxFirmwareBytes} bytes.");
        if (Path.GetFileName(path).EndsWith(".raw.bin", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(Path.GetFileName(path), "raw.bin", StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("*.raw.bin is not an allowed OTA image; use the tl_check_fw2 post-build application BIN.");
        if (!image.HasValidTelinkCrcTrailer)
            throw new InvalidDataException("Telink firmware CRC trailer is missing or invalid; use the tl_check_fw2 post-build application BIN.");
        return image;
    }

    public static FirmwareImage LoadForTarget(string path, OtaTargetKind target) => target switch
    {
        OtaTargetKind.Telink => LoadStrictTelink(path),
        OtaTargetKind.Stm32SerialIap => LoadStm32App(path),
        _ => throw new ArgumentOutOfRangeException(nameof(target), target, "OTA target must be resolved before loading firmware.")
    };

    private static FirmwareImage LoadStm32App(string path)
    {
        byte[] all = File.ReadAllBytes(path);
        FirmwareInspection inspection = Inspect(path);
        if (all.Length == 0 || all.Length > Stm32AppCapacity || !inspection.FitsStm32App)
            throw new InvalidDataException($"STM32 APP BIN 必须为有效向量表且不超过 {Stm32AppCapacity} bytes；actual={all.Length}，请确认 BIN 是从 0x{Stm32AppStartAddress:X8} 链接生成的 APP 镜像。");
        if (inspection.LooksLikeTelink)
            throw new InvalidDataException("文件包含 Telink 固件头部，不能作为 STM32 APP BIN 发送。");
        return new FirmwareImage(Path.GetFileName(path), all, OtaTargetKind.Stm32SerialIap);
    }

    private static bool IsValidStm32Vector(ReadOnlySpan<byte> image)
    {
        uint stack = BinaryPrimitives.ReadUInt32LittleEndian(image[..4]);
        uint reset = BinaryPrimitives.ReadUInt32LittleEndian(image.Slice(4, 4));
        uint resetAddress = reset & ~1u;
        return stack >= Stm32SramStartAddress && stack < Stm32SramEndAddress &&
               (reset & 1u) != 0 && resetAddress >= Stm32AppStartAddress && resetAddress < Stm32AppEndAddress;
    }
}

public sealed class TelinkOtaClient
{
    private readonly ITelinkOtaTransport _transport;
    private TaskCompletionSource<OtaResult>? _resultTcs;
    private OtaResult? _latestResult;

    public event Action<string>? Log;
    public event Action<OtaProgress>? Progress;

    public TelinkOtaClient(ITelinkOtaTransport transport) => _transport = transport;

    public async Task<bool> UpgradeAsync(FirmwareImage image, OtaTransferMode requestedMode, CancellationToken ct)
    {
        if (!_transport.IsConnected) throw new IOException("OTA transport is not connected.");
        OtaTransferMode mode = ResolveMode(requestedMode);
        int payload = mode == OtaTransferMode.Extend64 ? 64 : 16;
        int count = (image.ImageSize + payload - 1) / payload;
        OtaTransportPolicy policy = _transport.Policy;
        _latestResult = null;
        _resultTcs = new(TaskCreationOptions.RunContinuationsAsynchronously);
        _transport.NotificationReceived += OnNotification;
        try
        {
            Log?.Invoke($"Mode={mode}; MTU={_transport.NegotiatedMtu?.ToString() ?? "unknown"}; notify={_transport.NotificationsEnabled}; payload={payload}; startDelay={policy.StartPreparationDelay.TotalMilliseconds:F0}ms; packetDelay={(mode == OtaTransferMode.LegacyFast ? policy.LegacyPacketPacingDelay.TotalMilliseconds : 0):F0}ms; ackBarrierPackets={(mode == OtaTransferMode.LegacyFast ? policy.LegacyAckBarrierPackets : 0)}");
            byte[] start = mode == OtaTransferMode.Extend64
                ? TelinkOtaProtocol.BuildExtendedStart(payload, versionCompare: false)
                : TelinkOtaProtocol.BuildLegacyStart();
            Log?.Invoke("TX OTA START " + Convert.ToHexString(start));
            await _transport.WriteWithResponseAsync(start, ct);
            if (mode == OtaTransferMode.Extend64 && _transport.NotificationsEnabled)
            {
                OtaResult? early = await WaitResultAsync(TimeSpan.FromMilliseconds(250), ct);
                if (early is not null && !early.IsSuccess) throw ResultException(early, "START_EXT");
            }
            else
            {
                await Task.Delay(policy.StartPreparationDelay, ct);
            }

            var sw = Stopwatch.StartNew();
            long lastTicks = 0;
            for (int i = 0; i < count; i++)
            {
                ct.ThrowIfCancellationRequested();
                ThrowIfRejected();
                byte[] packet = TelinkOtaProtocol.BuildData(image.Bytes, i, payload, mode == OtaTransferMode.Extend64);
                bool ackBarrier = policy.LegacyAckBarrierPackets > 0 &&
                                  (i == count - 1 || (mode == OtaTransferMode.LegacyFast && (i + 1) % policy.LegacyAckBarrierPackets == 0));
                if (ackBarrier)
                {
                    var barrierTimer = Stopwatch.StartNew();
                    await _transport.WriteWithResponseAsync(packet, ct);
                    Log?.Invoke($"RX DATA ACK barrier index={(mode == OtaTransferMode.Extend64 ? i + 1 : i)} elapsed={barrierTimer.ElapsedMilliseconds}ms");
                }
                else
                {
                    await _transport.WriteAsync(packet, ct);
                }

                int sent = Math.Min((i + 1) * payload, image.ImageSize);
                long now = sw.ElapsedTicks;
                if (i == count - 1 || lastTicks == 0 || now - lastTicks >= Stopwatch.Frequency / 10)
                {
                    lastTicks = now;
                    double rate = sw.Elapsed.TotalSeconds > 0 ? sent / sw.Elapsed.TotalSeconds : 0;
                    TimeSpan? eta = rate > 0 ? TimeSpan.FromSeconds((image.ImageSize - sent) / rate) : null;
                    Progress?.Invoke(new OtaProgress(sent * 100.0 / image.ImageSize, sent, image.ImageSize, rate, eta, mode));
                }

                if (i == 0 || i == count - 1 || (i + 1) % 256 == 0)
                    Log?.Invoke($"TX DATA index={(mode == OtaTransferMode.Extend64 ? i + 1 : i)} bytes={sent}/{image.ImageSize}");

                if (mode == OtaTransferMode.LegacyFast && policy.LegacyPacketPacingDelay > TimeSpan.Zero)
                    await Task.Delay(policy.LegacyPacketPacingDelay, ct);
                else if (mode == OtaTransferMode.Extend64 && (i & 0x1F) == 0x1F)
                    await Task.Delay(1, ct);
            }

            ThrowIfRejected();
            await Task.Delay(policy.EndDrainDelay, ct);
            ushort lastIndex = mode == OtaTransferMode.Extend64 ? checked((ushort)count) : checked((ushort)(count - 1));
            byte[] end = TelinkOtaProtocol.BuildEnd(lastIndex);
            Log?.Invoke($"TX OTA END index={lastIndex}");
            await _transport.WriteAsync(end, ct);
            if (!_transport.NotificationsEnabled)
            {
                Log?.Invoke("OTA_RESULT unavailable; transfer complete but server result unconfirmed.");
                return false;
            }

            OtaResult? final = await WaitResultAsync(policy.ResultTimeout, ct);
            if (final is null)
            {
                Log?.Invoke("OTA_RESULT timeout; transfer complete but server result unconfirmed.");
                return false;
            }
            if (!final.IsSuccess) throw ResultException(final, "OTA_END");
            Log?.Invoke("RX OTA_RESULT 0x00 OTA_SUCCESS");
            return true;
        }
        finally
        {
            _transport.NotificationReceived -= OnNotification;
            _resultTcs = null;
        }
    }

    private OtaTransferMode ResolveMode(OtaTransferMode requested)
    {
        if (requested == OtaTransferMode.LegacyFast) return requested;
        bool extend = _transport.NegotiatedMtu is >= 71;
        if (requested == OtaTransferMode.Extend64 && !extend)
            throw new IOException($"Extend64 requires MTU >= 71; current={_transport.NegotiatedMtu?.ToString() ?? "unknown"}.");
        return requested == OtaTransferMode.Extend64 || extend ? OtaTransferMode.Extend64 : OtaTransferMode.LegacyFast;
    }

    private void OnNotification(ReadOnlyMemory<byte> data)
    {
        ReadOnlySpan<byte> s = data.Span;
        if (s.Length < 3 || s[0] != 0x06 || s[1] != 0xFF) return;
        var result = new OtaResult(s[2], ResultName(s[2]));
        _latestResult = result;
        Log?.Invoke($"RX OTA_RESULT 0x{result.Code:X2} {result.Name}");
        _resultTcs?.TrySetResult(result);
    }

    private void ThrowIfRejected()
    {
        if (_latestResult is { IsSuccess: false } result) throw ResultException(result, "DATA");
    }

    private async Task<OtaResult?> WaitResultAsync(TimeSpan timeout, CancellationToken ct)
    {
        TaskCompletionSource<OtaResult>? tcs = _resultTcs;
        if (tcs is null) return null;
        if (tcs.Task.IsCompleted) return await tcs.Task;
        Task delay = Task.Delay(timeout, ct);
        Task winner = await Task.WhenAny(tcs.Task, delay);
        if (winner == tcs.Task) return await tcs.Task;
        ct.ThrowIfCancellationRequested();
        return null;
    }

    private static IOException ResultException(OtaResult result, string phase) =>
        new($"{phase} rejected: OTA_RESULT=0x{result.Code:X2} {result.Name}");

    private static string ResultName(byte code) => code switch
    {
        0x00 => "OTA_SUCCESS",
        0x01 => "OTA_DATA_PACKET_SEQ_ERR",
        0x02 => "OTA_PACKET_INVALID",
        0x03 => "OTA_DATA_CRC_ERR",
        0x04 => "OTA_WRITE_FLASH_ERR",
        0x05 => "OTA_DATA_INCOMPLETE",
        0x06 => "OTA_FLOW_ERR",
        0x07 => "OTA_FW_CHECK_ERR",
        0x08 => "OTA_VERSION_COMPARE_ERR",
        0x09 => "OTA_PDU_LEN_ERR",
        0x0A => "OTA_FIRMWARE_MARK_ERR",
        0x0B => "OTA_FW_SIZE_ERR",
        0x0C => "OTA_DATA_PACKET_TIMEOUT",
        0x0D => "OTA_TIMEOUT",
        0x0E => "OTA_CONNECTION_TERMINATE",
        0x0F => "OTA_MCU_NOT_SUPPORTED",
        0x10 => "OTA_LOGIC_ERR",
        _ => $"OTA_RESULT_0x{code:X2}"
    };
}
