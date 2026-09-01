using System.Diagnostics;
using Bms.Ota.Core.Firmware;
using Bms.Ota.Core.Transport;

namespace Bms.Ota.Core.Telink;

public sealed class TelinkOtaClient
{
    private readonly IOtaTransport _transport;
    private TaskCompletionSource<OtaResult>? _resultTcs;

    public OtaState State { get; private set; } = OtaState.Idle;
    public OtaTransferMode ActiveMode { get; private set; } = OtaTransferMode.LegacyFast;
    public event Action<OtaState>? StateChanged;
    public event Action<string>? Log;
    public event Action<OtaProgress>? Progress;

    public TelinkOtaClient(IOtaTransport transport) => _transport = transport;

    public async Task UpgradeAsync(
        FirmwareImage image,
        OtaTransferMode requestedMode = OtaTransferMode.Auto,
        int packetDelayMs = 0,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(image);
        if (!_transport.IsConnected)
            throw new InvalidOperationException("BLE OTA transport is not connected.");
        if (packetDelayMs < 0 || packetDelayMs > 1000)
            throw new ArgumentOutOfRangeException(nameof(packetDelayMs), "Packet delay must be 0..1000 ms.");

        ActiveMode = ResolveMode(requestedMode);
        int payloadSize = ActiveMode == OtaTransferMode.Extend64 ? ExtendedPacketBuilder.PayloadSize64 : LegacyPacketBuilder.PayloadSize;
        int packetCount = ActiveMode == OtaTransferMode.Extend64
            ? ExtendedPacketBuilder.GetPacketCount(image.ImageSize, payloadSize)
            : image.PacketCount;

        _resultTcs = new(TaskCreationOptions.RunContinuationsAsynchronously);
        _transport.NotificationReceived += OnNotification;

        try
        {
            Emit($"Mode={ActiveMode}; MTU={_transport.NegotiatedMtu?.ToString() ?? "unknown"}; notifications={_transport.NotificationsEnabled}; payload={payloadSize} bytes; packetDelay={packetDelayMs} ms");

            SetState(OtaState.Starting);
            byte[] start = ActiveMode == OtaTransferMode.Extend64
                ? ExtendedPacketBuilder.BuildStart(payloadSize, versionCompare: false)
                : LegacyPacketBuilder.BuildStart();
            Emit($"TX OTA START: {Convert.ToHexString(start)}");
            await _transport.WriteAsync(start, cancellationToken);

            if (ActiveMode == OtaTransferMode.Extend64 && _transport.NotificationsEnabled)
            {
                OtaResult? early = await TryWaitResultAsync(TimeSpan.FromMilliseconds(250), cancellationToken);
                if (early is not null)
                    EnsureSuccess(early, "START_EXT");
            }

            await Delay(packetDelayMs, cancellationToken);

            SetState(OtaState.Sending);
            var sw = Stopwatch.StartNew();
            long lastProgressTicks = 0;

            for (int i = 0; i < packetCount; i++)
            {
                cancellationToken.ThrowIfCancellationRequested();
                ThrowIfFailureResultAlreadyReceived();

                byte[] packet = ActiveMode == OtaTransferMode.Extend64
                    ? ExtendedPacketBuilder.BuildData(image, i, payloadSize)
                    : LegacyPacketBuilder.BuildData(image, i);

                await _transport.WriteAsync(packet, cancellationToken);

                int sent = Math.Min((i + 1) * payloadSize, image.ImageSize);
                long nowTicks = sw.ElapsedTicks;
                bool report = i == packetCount - 1 ||
                              lastProgressTicks == 0 ||
                              (nowTicks - lastProgressTicks) >= Stopwatch.Frequency / 10;
                if (report)
                {
                    lastProgressTicks = nowTicks;
                    double rate = sw.Elapsed.TotalSeconds > 0 ? sent / sw.Elapsed.TotalSeconds : 0;
                    Progress?.Invoke(new OtaProgress(i + 1, packetCount, sent, image.ImageSize, sw.Elapsed, rate, ActiveMode));
                }

                if (i == 0 || i == packetCount - 1 || (i + 1) % 256 == 0)
                    Emit($"TX DATA index={(ActiveMode == OtaTransferMode.Extend64 ? i + 1 : i)} imageBytes={sent}/{image.ImageSize}");

                if (packetDelayMs > 0)
                    await Task.Delay(packetDelayMs, cancellationToken);
                else if ((i & 0x3F) == 0x3F)
                    await Task.Yield();
            }

            ThrowIfFailureResultAlreadyReceived();
            SetState(OtaState.Ending);
            ushort lastIndex = ActiveMode == OtaTransferMode.Extend64
                ? checked((ushort)packetCount)
                : checked((ushort)(packetCount - 1));
            byte[] end = LegacyPacketBuilder.BuildEnd(lastIndex);
            Emit($"TX OTA END: lastIndex={lastIndex}, bytes={Convert.ToHexString(end)}");
            await _transport.WriteAsync(end, cancellationToken);

            SetState(OtaState.WaitingForReboot);
            if (_transport.NotificationsEnabled)
            {
                OtaResult? finalResult = await TryWaitResultAsync(TimeSpan.FromSeconds(2), cancellationToken);
                if (finalResult is not null)
                {
                    EnsureSuccess(finalResult, "OTA_END");
                    Emit($"RX OTA_RESULT: 0x{finalResult.Code:X2} {finalResult.Name}");
                }
                else
                {
                    Emit("OTA_RESULT was not received before timeout; transfer finished but final server result is unconfirmed.");
                }
            }
            else
            {
                Emit("OTA notifications are unavailable; transfer finished without OTA_RESULT confirmation.");
            }

            SetState(OtaState.TransferComplete);
        }
        catch (OperationCanceledException)
        {
            SetState(OtaState.Cancelled);
            Emit("OTA cancelled. Restart a new OTA session from index 0 before retrying.");
            throw;
        }
        catch (Exception ex)
        {
            SetState(OtaState.Failed);
            Emit($"OTA failed: {ex.Message}. Reconnect and restart from index 0.");
            throw;
        }
        finally
        {
            _transport.NotificationReceived -= OnNotification;
            _resultTcs = null;
        }
    }

    private OtaTransferMode ResolveMode(OtaTransferMode requested)
    {
        if (requested == OtaTransferMode.LegacyFast)
            return OtaTransferMode.LegacyFast;

        bool linkCanCarryExtend64 = _transport.NegotiatedMtu is >= 71;
        if (requested == OtaTransferMode.Extend64)
        {
            if (!linkCanCarryExtend64)
                throw new InvalidOperationException($"Extend64 requires negotiated MTU >= 71; current MTU={_transport.NegotiatedMtu?.ToString() ?? "unknown"}.");
            return OtaTransferMode.Extend64;
        }

        return linkCanCarryExtend64 ? OtaTransferMode.Extend64 : OtaTransferMode.LegacyFast;
    }

    private void OnNotification(ReadOnlyMemory<byte> data)
    {
        if (!OtaResult.TryParse(data.Span, out var result) || result is null)
            return;
        Emit($"RX OTA_RESULT: 0x{result.Code:X2} {result.Name} ({result.Description})");
        _resultTcs?.TrySetResult(result);
    }

    private void ThrowIfFailureResultAlreadyReceived()
    {
        if (_resultTcs?.Task.IsCompletedSuccessfully == true)
        {
            OtaResult result = _resultTcs.Task.Result;
            EnsureSuccess(result, "DATA");
        }
    }

    private async Task<OtaResult?> TryWaitResultAsync(TimeSpan timeout, CancellationToken ct)
    {
        var tcs = _resultTcs;
        if (tcs is null) return null;
        if (tcs.Task.IsCompleted) return await tcs.Task;

        Task delay = Task.Delay(timeout, ct);
        Task winner = await Task.WhenAny(tcs.Task, delay);
        if (winner == tcs.Task)
            return await tcs.Task;
        ct.ThrowIfCancellationRequested();
        return null;
    }

    private static void EnsureSuccess(OtaResult result, string phase)
    {
        if (!result.IsSuccess)
            throw new InvalidOperationException($"{phase} rejected: OTA_RESULT=0x{result.Code:X2} {result.Name} ({result.Description}).");
    }

    private static Task Delay(int packetDelayMs, CancellationToken ct) =>
        packetDelayMs == 0 ? Task.CompletedTask : Task.Delay(packetDelayMs, ct);

    private void SetState(OtaState state)
    {
        State = state;
        StateChanged?.Invoke(state);
    }

    private void Emit(string message) => Log?.Invoke(message);
}
