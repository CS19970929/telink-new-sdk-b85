using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using Windows.Devices.Bluetooth.GenericAttributeProfile;

namespace BmsTool.Windows;

public sealed record Stm32OtaProgress(double Percent, int SentBytes, int TotalBytes, int PageIndex, int PageCount);

public sealed class Stm32SerialBleOtaClient
{
    private const ushort FlashConnect = 0xFFFD;
    private const ushort FlashUpgrade = 0xFFFE;
    private const ushort FlashComplete = 0xFFFF;
    // The supplied IAP interprets quantity as raw byte length when byte-count
    // is zero. A page is therefore acknowledged as quantity=1024, not 512.
    private const ushort PageTransferLength = 1024;
    private const int PageSize = 1024;
    private const int MaxPageCount = FirmwareImage.Stm32AppCapacity / PageSize;
    // The BLE MTU is not the same as the transparent UART buffer capacity.
    // The supplied IAP UART is 19200 8N1 without flow control, so use the
    // conservative default ATT payload and allow the module to drain it.
    private const int SerialBleChunkSize = 20;
    private const int SerialBleInterChunkDelayMs = 20;

    private readonly BmsBleTransport _transport;
    private readonly object _rxLock = new();
    private readonly List<byte> _rx = new();
    private TaskCompletionSource<byte[]>? _pending;

    public event Action<string>? Log;
    public event Action<Stm32OtaProgress>? Progress;

    public Stm32SerialBleOtaClient(BmsBleTransport transport) => _transport = transport;

    public async Task<bool> UpgradeAsync(FirmwareImage image, CancellationToken ct)
    {
        if (image.TargetKind != OtaTargetKind.Stm32SerialIap)
            throw new InvalidDataException("STM32 串口 IAP OTA 只能使用 STM32 APP BIN。");
        if (image.ImageSize is <= 0 or > FirmwareImage.Stm32AppCapacity)
            throw new InvalidDataException($"STM32 APP BIN 超出 55 KB APP 区：{image.ImageSize} bytes。");

        int pageCount = (image.ImageSize + PageSize - 1) / PageSize;
        if (pageCount is <= 0 or > MaxPageCount)
            throw new InvalidDataException($"STM32 APP 页数非法：{pageCount}，最大 {MaxPageCount} 页。");

        _transport.DataReceived += OnDataReceived;
        try
        {
            Log?.Invoke($"STM32 Serial IAP OTA; MTU={_transport.NegotiatedMtu?.ToString() ?? "unknown"}; image={image.ImageSize}; pages={pageCount}; page=1024");

            byte[] enter = ModbusRtu.WriteMultiple(FlashConnect, new byte[] { 0x00, 0x01 });
            Log?.Invoke("TX STM32 IAP ENTER " + Convert.ToHexString(enter));
            await SendAndWaitAsync(enter, FlashConnect, 1, ct);
            Log?.Invoke("RX STM32 IAP ENTER ACK; waiting for BMS reset into IAP");
            await Task.Delay(800, ct);
            Log?.Invoke("Reconnecting STM32 serial GATT after BMS reset");
            await _transport.ReconnectAsync(ct);
            Log?.Invoke($"STM32 serial IAP GATT reconnected; MTU={_transport.NegotiatedMtu?.ToString() ?? "unknown"}; {_transport.DiscoveryDescription}");

            for (int page = 0; page < pageCount; page++)
            {
                ct.ThrowIfCancellationRequested();
                byte[] frame = BuildPageFrame(image.Bytes, page);
                Log?.Invoke($"TX STM32 IAP PAGE {page + 1}/{pageCount}; bytes={Math.Min((page + 1) * PageSize, image.ImageSize)}/{image.ImageSize}; frame={frame.Length}");
                await SendAndWaitAsync(frame, FlashUpgrade, PageTransferLength, ct);
                int sent = Math.Min((page + 1) * PageSize, image.ImageSize);
                Progress?.Invoke(new Stm32OtaProgress(sent * 100.0 / image.ImageSize, sent, image.ImageSize, page + 1, pageCount));
            }

            byte[] complete = ModbusRtu.WriteMultiple(FlashComplete, new byte[] { 0x00, 0x01 });
            Log?.Invoke("TX STM32 IAP COMPLETE " + Convert.ToHexString(complete));
            await SendAndWaitAsync(complete, FlashComplete, 1, ct);
            Log?.Invoke("RX STM32 IAP COMPLETE ACK; device will reset to APP");
            return true;
        }
        finally
        {
            _transport.DataReceived -= OnDataReceived;
            lock (_rxLock)
            {
                _pending?.TrySetCanceled();
                _pending = null;
                _rx.Clear();
            }
        }
    }

    private async Task SendAndWaitAsync(byte[] request, ushort expectedRegister, ushort expectedQuantity, CancellationToken ct)
    {
        TaskCompletionSource<byte[]> pending = new(TaskCreationOptions.RunContinuationsAsynchronously);
        lock (_rxLock)
        {
            _rx.Clear();
            _pending = pending;
        }

        try
        {
            await WriteIapFrameChunkedAsync(request, ct);
            byte[] response = await pending.Task.WaitAsync(TimeSpan.FromSeconds(8), ct);
            ModbusRtu.ValidateWriteMultipleAck(response, expectedRegister, expectedQuantity);
        }
        catch (TimeoutException ex)
        {
            throw new IOException($"STM32 IAP ACK 超时，地址=0x{expectedRegister:X4}。为避免例程协议隐式页计数错位，已停止升级，不能自动重传当前页。", ex);
        }
        finally
        {
            lock (_rxLock)
            {
                if (ReferenceEquals(_pending, pending)) _pending = null;
            }
        }
    }

    private async Task WriteIapFrameChunkedAsync(ReadOnlyMemory<byte> frame, CancellationToken ct)
    {
        int chunkCount = (frame.Length + SerialBleChunkSize - 1) / SerialBleChunkSize;
        Log?.Invoke($"STM32 IAP frame chunking; frame={frame.Length}; chunks={chunkCount}; chunk={SerialBleChunkSize}; delay={SerialBleInterChunkDelayMs}ms; write=with-response");
        for (int offset = 0; offset < frame.Length; offset += SerialBleChunkSize)
        {
            int length = Math.Min(SerialBleChunkSize, frame.Length - offset);
            await _transport.WriteAsync(frame.Slice(offset, length), ct);
            if (offset + length < frame.Length)
                await Task.Delay(SerialBleInterChunkDelayMs, ct);
        }
    }

    private void OnDataReceived(ReadOnlyMemory<byte> data)
    {
        lock (_rxLock)
        {
            _rx.AddRange(data.ToArray());
            while (_pending is not null)
            {
                if (_rx.Count < 2) return;
                int? expected = ModbusRtu.InferExpectedLength(_rx);
                if (expected is null) { _rx.RemoveAt(0); continue; }
                if (_rx.Count < expected.Value) return;

                byte[] frame = _rx.Take(expected.Value).ToArray();
                _rx.RemoveRange(0, expected.Value);
                try
                {
                    ModbusRtu.ValidateFrame(frame);
                    _pending.TrySetResult(frame);
                }
                catch (IOException)
                {
                    // A malformed notification is discarded; the request remains
                    // pending until a valid frame or the hard timeout arrives.
                }
            }
        }
    }

    private static byte[] BuildPageFrame(byte[] image, int page)
    {
        byte[] pageData = new byte[PageSize];
        Array.Fill(pageData, (byte)0xFF);
        int offset = checked(page * PageSize);
        int count = Math.Min(PageSize, image.Length - offset);
        if (count > 0) Buffer.BlockCopy(image, offset, pageData, 0, count);
        return ModbusRtu.WriteLegacyByteCountZero(FlashUpgrade, pageData);
    }
}
