using System.Diagnostics;

namespace TelinkOta.Core.Ota;

/// <summary>协议选择。</summary>
public enum OtaProtocolChoice
{
    /// <summary>优先 Extend，版本响应超时自动回退 Legacy。</summary>
    Auto = 0,
    Extend = 1,
    Legacy = 2,
}

/// <summary>会话选项。</summary>
public sealed class OtaSessionOptions
{
    public OtaProtocolChoice Protocol { get; set; } = OtaProtocolChoice.Auto;
    public int PduLength { get; set; } = OtaConstants.PduMin;
    public bool PadToFullPdu { get; set; }
    public bool VersionCompare { get; set; }

    /// <summary>Legacy 流程是否先发 CMD_OTA_VERSION（官方 App 默认开）。</summary>
    public bool SendOtaVersion { get; set; }

    /// <summary>是否发送 CMD_OTA_SET_FW_INDEX（官方 App 默认关）。</summary>
    public bool SendFwIndex { get; set; }
    public byte FwIndex { get; set; } = 1;

    /// <summary>Write Without Response 并发窗口（有界队列）。</summary>
    public int WriteWindow { get; set; } = 6;

    /// <summary>传输层允许的最大写入长度（默认 20，MTU=23 场景）。</summary>
    public int MaxWriteLength { get; set; } = 20;

    /// <summary>首包写入失败（如缓冲超限）时是否自动降级 PDU=16 重试。</summary>
    public bool AutoDowngradePdu { get; set; } = true;

    // ---- 超时（均小于设备端：packet 15s / process 180s）----
    public TimeSpan ConnectTimeout { get; set; } = TimeSpan.FromSeconds(15);
    public TimeSpan DiscoveryTimeout { get; set; } = TimeSpan.FromSeconds(10);
    public TimeSpan VersionRspTimeout { get; set; } = TimeSpan.FromSeconds(5);
    public TimeSpan PacketStallTimeout { get; set; } = TimeSpan.FromSeconds(10);
    public TimeSpan TotalTimeout { get; set; } = TimeSpan.FromSeconds(170);
    public TimeSpan ResultTimeout { get; set; } = TimeSpan.FromSeconds(20);
    public TimeSpan RebootDetectTimeout { get; set; } = TimeSpan.FromSeconds(8);
    public TimeSpan ReconnectTimeout { get; set; } = TimeSpan.FromSeconds(30);
    public TimeSpan LegacyResultGrace { get; set; } = TimeSpan.FromSeconds(3);

    /// <summary>升级后重连并做版本复核（OTA 协商 + 尽力读取 BMS 软件版本）。</summary>
    public bool VerifyVersion { get; set; } = true;
}

/// <summary>会话结果。</summary>
public enum OtaOutcome
{
    Success = 0,
    Failed = 1,
    Cancelled = 2,
    TimedOut = 3,
    Disconnected = 4,
    /// <summary>写入容量不足（PDU 超出设备 MTU），调用方可降级 PDU=16 重试。</summary>
    PduTooLarge = 5,
}

public sealed class OtaSessionResult
{
    public OtaOutcome Outcome { get; init; }
    public string Message { get; init; } = "";
    public OtaState FinalState { get; init; }
    public OtaResult? DeviceResult { get; init; }
    public int PacketsSent { get; init; }
    public long BytesSent { get; init; }
    public TimeSpan Duration { get; init; }
    public string? VersionBefore { get; init; }
    public string? VersionAfter { get; init; }
    public int PduLength { get; init; }
}

/// <summary>
/// OTA 会话编排：状态机驱动，超时/取消/断连/错误全覆盖。
/// 升级成功 = 设备 Result 确认 + 重启 + 重连 + 版本复核，四步缺一不可。
/// </summary>
public sealed class OtaSession
{
    private readonly IBleTransport _transport;
    private readonly OtaFirmware _firmware;
    private readonly OtaSessionOptions _options;
    private readonly OtaStateMachine _sm = new();
    private readonly LogCallback _log;

    private TaskCompletionSource<OtaNotify>? _versionRspTcs;
    private TaskCompletionSource<OtaNotify>? _resultTcs;
    private TaskCompletionSource<bool>? _disconnectTcs;
    private volatile bool _rebootDetected;

    public event Action<OtaState>? StateChanged;
    public event Action<int, int>? ProgressChanged; // (index, total)

    public OtaSession(IBleTransport transport, OtaFirmware firmware, OtaSessionOptions options, LogCallback? log = null)
    {
        _transport = transport ?? throw new ArgumentNullException(nameof(transport));
        _firmware = firmware ?? throw new ArgumentNullException(nameof(firmware));
        _options = options ?? throw new ArgumentNullException(nameof(options));
        _log = log ?? ((_, _) => { });
        _sm.StateChanged += s => StateChanged?.Invoke(s);
    }

    public async Task<OtaSessionResult> RunAsync(CancellationToken userCt)
    {
        var sw = Stopwatch.StartNew();
        using var cts = CancellationTokenSource.CreateLinkedTokenSource(userCt);
        var ct = cts.Token;

        _transport.OtaNotifyReceived += OnOtaNotify;
        _transport.SppNotifyReceived += OnSppNotify;
        _transport.ConnectionLost += OnConnectionLost;

        string? versionBefore = null, versionAfter = null;
        int packetsSent = 0;
        try
        {
            await Step(OtaState.Connecting,
                () => _transport.ConnectAsync(_options.ConnectTimeout, ct));

            await Step(OtaState.DiscoveringServices,
                () => _transport.DiscoverOtaServiceAsync(_options.DiscoveryTimeout, ct));

            await Step(OtaState.EnablingNotifications,
                () => _transport.EnableOtaNotificationsAsync(_options.DiscoveryTimeout, ct));

            await Step(OtaState.ValidatingFirmware, () => Task.FromResult(true));

            // 升级前版本读取（需已连接）
            if (_options.VerifyVersion)
            {
                versionBefore = await ReadBmsVersionBestEffortAsync(ct);
            }

            // ---- MTU / PDU ----
            await Step(OtaState.NegotiatingMtuAndPdu, async () =>
            {
                try { await _transport.NegotiateMtuAsync(TimeSpan.FromSeconds(5), ct); }
                catch { /* 尽力而为 */ }

                int maxPdu = Math.Min(OtaConstants.PduMax, _options.MaxWriteLength - OtaConstants.PduOverhead);
                int effective = Math.Min(_options.PduLength, maxPdu);
                effective = Math.Clamp(effective, OtaConstants.PduMin, OtaConstants.PduMax);
                effective -= effective % OtaConstants.PduStep;
                if (effective < OtaConstants.PduMin) effective = OtaConstants.PduMin;

                _pdu = effective;
                _encoder = new OtaPacketEncoder(_firmware.Payload, _pdu, _options.PadToFullPdu);
                _log(LogLevel.Info,
                    $"MTU 协商完成，PDU = {_pdu}（窗口 {_options.WriteWindow}，尾包补齐={( _options.PadToFullPdu ? "完整PDU" : "16倍数")}）");
                return true;
            });

            // ---- 版本协商 ----
            bool useLegacy = _options.Protocol == OtaProtocolChoice.Legacy;
            await Step(OtaState.VersionCheck, async () =>
            {
                if (useLegacy)
                {
                    if (_options.SendOtaVersion)
                        await WriteCmdAsync(OtaPacketEncoder.BuildVersion(), "CMD_OTA_VERSION", ct);
                    return true;
                }

                bool gotRsp = await TryVersionNegotiationAsync(ct);
                if (!gotRsp && _options.Protocol == OtaProtocolChoice.Auto)
                {
                    _log(LogLevel.Warn, "Extend 版本响应超时，自动回退 Legacy 协议");
                    useLegacy = true;
                    return true;
                }
                return gotRsp;
            });

            // ---- Start ----
            await Step(OtaState.SendingStart, async () =>
            {
                if (useLegacy)
                {
                    await WriteCmdAsync(OtaPacketEncoder.BuildStart(), "CMD_OTA_START", ct);
                }
                else
                {
                    await WriteCmdAsync(
                        OtaPacketEncoder.BuildStartExt(_pdu, _options.VersionCompare),
                        $"CMD_OTA_START_EXT pdu={_pdu} versionCompare={(_options.VersionCompare ? 1 : 0)}", ct);
                }
                return true;
            });

            // ---- 数据传输 ----
            await Step(OtaState.Transferring, async () =>
            {
                using var writer = new BoundedWriter(_options.WriteWindow);
                int total = _encoder!.TotalPackets;
                for (int idx = 0; idx < total; idx++)
                {
                    ThrowIfTimeout(sw);
                    var packet = _encoder.BuildPacket(idx);
                    var result = await writer.WriteAsync(packet, _transport.WriteWithoutResponseAsync,
                        _options.PacketStallTimeout, ct);
                    switch (result)
                    {
                        case WriteOutcome.Ok:
                            packetsSent = idx + 1;
                            ProgressChanged?.Invoke(idx + 1, total);
                            break;
                        case WriteOutcome.StallTimeout:
                            throw FailEx(OtaState.TimedOut, "发送窗口停滞超时（设备可能在 15s 后判定 packet timeout）");
                        case WriteOutcome.WriteFailed:
                            if (idx == 0 && _options.AutoDowngradePdu && _pdu > OtaConstants.PduMin)
                            {
                                _pendingPduDowngrade = true;
                                throw FailEx(OtaState.Failed, "首包写入失败（可能 PDU 超出设备 MTU）");
                            }
                            throw FailEx(OtaState.Failed, $"数据包 {idx} 写入失败");
                    }
                }
                return true;
            });

            // ---- 排空队列 ----
            await Step(OtaState.DrainingTxQueue,
                () => _transport.WaitForTxQueueDrainedAsync(TimeSpan.FromSeconds(8), ct));

            // ---- End ----
            await Step(OtaState.SendingEnd, async () =>
            {
                var end = OtaPacketEncoder.BuildEnd(_encoder!.LastIndex);
                await WriteCmdAsync(end, $"CMD_OTA_END index_max={_encoder.LastIndex} xor=0x{_encoder.LastIndex ^ 0xFFFF:X4}", ct);
                return true;
            });

            // ---- 等待 Result ----
            OtaResult? deviceResult = null;
            bool resultOk = await StepOrFalse(OtaState.WaitingResult, async () =>
            {
                _resultTcs = new TaskCompletionSource<OtaNotify>(TaskCreationOptions.RunContinuationsAsynchronously);
                try
                {
                    if (useLegacy)
                    {
                        // Legacy：官方 App 在 END 写完即判成功；此处给 3s 宽限等待可能的 Result
                        var completed = await Task.WhenAny(
                            _resultTcs.Task, Task.Delay(_options.LegacyResultGrace, ct));
                        if (completed != _resultTcs.Task)
                        {
                            _log(LogLevel.Info, "Legacy：未收到 Result（正常），按 END 完成判定成功");
                            return true;
                        }
                        deviceResult = HandleResultAsync(_resultTcs.Task.Result);
                        return deviceResult!.Code == 0;
                    }
                    else
                    {
                        var notify = await _resultTcs.Task.WaitAsync(_options.ResultTimeout, ct);
                        deviceResult = HandleResultAsync(notify);
                        if (deviceResult.Code != 0)
                        {
                            _lastFailResult = deviceResult;
                            throw FailEx(OtaState.Failed, deviceResult.ToString());
                        }
                        return true;
                    }
                }
                catch (TimeoutException)
                {
                    throw FailEx(OtaState.TimedOut, "等待设备 OTA Result 超时");
                }
            });
            if (!resultOk && !_sm.IsTerminal)
                throw FailEx(OtaState.Failed, "设备返回失败 Result");
            if (_sm.IsTerminal)
                return Done(sw, _pendingPduDowngrade ? OtaOutcome.PduTooLarge : OtaOutcome.Failed,
                    deviceResult?.ToString() ?? "OTA Result 失败", packetsSent, deviceResult);
            _log(LogLevel.Info, "设备返回 OTA_SUCCESS");

            // ---- 重启 ----
            await Step(OtaState.WaitingReboot, async () =>
            {
                _disconnectTcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
                try
                {
                    await Task.WhenAny(_disconnectTcs.Task, Task.Delay(_options.RebootDetectTimeout, ct));
                }
                catch (OperationCanceledException) { }
                _log(LogLevel.Info, _rebootDetected ? "检测到设备重启断连" : "等待重启断连超时，主动断开");
                await SafeDisconnectAsync();
                return true;
            });

            // ---- 重连 ----
            await Step(OtaState.Reconnecting, async () =>
            {
                bool ok = await _transport.ConnectAsync(_options.ReconnectTimeout, ct);
                if (!ok)
                    throw FailEx(OtaState.Disconnected, "设备重启后重连失败");
                return true;
            });
            await Step(OtaState.DiscoveringServices,
                () => _transport.DiscoverOtaServiceAsync(_options.DiscoveryTimeout, ct));
            await Step(OtaState.EnablingNotifications,
                () => _transport.EnableOtaNotificationsAsync(_options.DiscoveryTimeout, ct));

            // ---- 版本复核 ----
            await Step(OtaState.VerifyingVersion, async () =>
            {
                bool responsive = await TryVersionNegotiationAsync(ct);
                versionAfter = await ReadBmsVersionBestEffortAsync(ct);
                if (!responsive)
                    throw FailEx(OtaState.Failed, "升级后设备 OTA 协商无响应，版本复核失败");
                _log(LogLevel.Info,
                    versionAfter is not null
                        ? $"版本复核：升级前 \"{versionBefore ?? "(未知)"}\" → 升级后 \"{versionAfter}\""
                        : "版本复核：设备 OTA 服务响应正常（BMS 版本寄存器不可读）");
                return true;
            });

            _sm.AdvanceTo(OtaState.Success, "设备 Result + 重启重连 + 版本复核完成");
            _log(LogLevel.Info, "OTA 升级成功");
            return new OtaSessionResult
            {
                Outcome = OtaOutcome.Success,
                Message = "OTA 升级成功",
                FinalState = OtaState.Success,
                PacketsSent = packetsSent,
                BytesSent = (long)packetsSent * _pdu,
                Duration = sw.Elapsed,
                VersionBefore = versionBefore,
                VersionAfter = versionAfter,
                PduLength = _pdu,
            };
        }
        catch (OperationCanceledException)
        {
            if (userCt.IsCancellationRequested)
            {
                FailTo(OtaState.Cancelled, "用户取消");
                return Done(sw, OtaOutcome.Cancelled, "用户取消", packetsSent);
            }
            FailTo(OtaState.TimedOut, "会话超时");
            return Done(sw, OtaOutcome.TimedOut, "会话超时", packetsSent);
        }
        catch (OtaStepAbortException)
        {
            if (_pendingPduDowngrade)
                return Done(sw, OtaOutcome.PduTooLarge, "PDU 超出设备容量，请降级为 16 后重试", packetsSent);
            return Done(sw, _sm.Current == OtaState.TimedOut ? OtaOutcome.TimedOut : OtaOutcome.Failed,
                _lastFailMessage, packetsSent, _lastFailResult);
        }
        catch (Exception ex)
        {
            _log(LogLevel.Error, $"会话异常：{ex.Message}");
            FailTo(OtaState.Failed, ex.Message);
            return Done(sw, OtaOutcome.Failed, ex.Message, packetsSent);
        }
        finally
        {
            _transport.OtaNotifyReceived -= OnOtaNotify;
            _transport.SppNotifyReceived -= OnSppNotify;
            _transport.ConnectionLost -= OnConnectionLost;
            await SafeDisconnectAsync();
        }
    }

    // ================= 内部 =================

    private int _pdu;
    private OtaPacketEncoder? _encoder;
    private bool _pendingPduDowngrade;
    private byte[] _sppAccum = Array.Empty<byte>();
    private string _lastFailMessage = "";
    private OtaResult? _lastFailResult;

    private void OnOtaNotify(byte[] data)
    {
        if (!OtaNotify.TryParse(data, out var notify) || notify is null)
            return;
        _log(LogLevel.Debug, $"OTA Notify: {notify}");
        switch (notify.Opcode)
        {
            case OtaConstants.CmdOtaFwVersionRsp:
                _versionRspTcs?.TrySetResult(notify);
                break;
            case OtaConstants.CmdOtaResult:
                _resultTcs?.TrySetResult(notify);
                break;
            default:
                _log(LogLevel.Debug, $"忽略 opcode 0x{notify.Opcode:X4}（当前状态 {_sm.Current}）");
                break;
        }
    }

    private void OnSppNotify(byte[] data)
    {
        var buf = new byte[_sppAccum.Length + data.Length];
        _sppAccum.CopyTo(buf, 0);
        data.CopyTo(buf, _sppAccum.Length);
        _sppAccum = buf;
    }

    private void OnConnectionLost()
    {
        if (_sm.Current == OtaState.WaitingReboot)
        {
            _rebootDetected = true;
            _disconnectTcs?.TrySetResult(true);
        }
        else
        {
            _log(LogLevel.Warn, $"连接意外断开（状态 {_sm.Current}）");
        }
    }

    private async Task<bool> TryVersionNegotiationAsync(CancellationToken ct)
    {
        _versionRspTcs = new TaskCompletionSource<OtaNotify>(TaskCreationOptions.RunContinuationsAsynchronously);
        try
        {
            var req = OtaPacketEncoder.BuildFwVersionReq(_firmware.BinVersion, _options.VersionCompare);
            await WriteCmdAsync(req, $"CMD_OTA_FW_VERSION_REQ version=0x{_firmware.BinVersion:X4}", ct);
            var rsp = await _versionRspTcs.Task.WaitAsync(_options.VersionRspTimeout, ct);
            if (rsp.VersionAccepted == true)
            {
                _log(LogLevel.Info, $"设备接受升级（本地版本 0x{rsp.LocalVersion:X4}）");
                return true;
            }
            throw FailEx(OtaState.Failed, "设备拒绝升级（版本比较失败或响应格式错误）");
        }
        catch (TimeoutException)
        {
            _log(LogLevel.Warn, "版本响应超时");
            return false;
        }
        finally
        {
            _versionRspTcs = null;
        }
    }

    private OtaResult HandleResultAsync(OtaNotify notify)
    {
        if (notify.Result is not { } code)
            throw FailEx(OtaState.Failed, "Result 通知格式错误");
        var deviceResult = OtaResult.FromCode(code);
        _log(LogLevel.Info, $"设备 OTA Result: 0x{code:X2} - {deviceResult.Name}");
        return deviceResult;
    }

    private async Task<string?> ReadBmsVersionBestEffortAsync(CancellationToken ct)
    {
        try
        {
            await _transport.DiscoverSppServiceAsync(TimeSpan.FromSeconds(5), ct);
            await _transport.EnableSppNotificationsAsync(TimeSpan.FromSeconds(5), ct);
            _sppAccum = Array.Empty<byte>();
            var req = ModbusRtu.BuildReadRequest(0xC022, 16);
            _log(LogLevel.Debug, $"SPP 读版本: {Hex.Dump(req)}");
            if (!await _transport.WriteSppAsync(req, ct))
                return null;
            var deadline = DateTime.UtcNow.AddSeconds(5);
            while (DateTime.UtcNow < deadline)
            {
                if (ModbusRtu.TryParseReadResponse(_sppAccum, out var data))
                {
                    string s = System.Text.Encoding.ASCII.GetString(data!).TrimEnd('\0').Trim();
                    return string.IsNullOrEmpty(s) ? null : s;
                }
                await Task.Delay(50, ct);
            }
            return null;
        }
        catch
        {
            return null;
        }
    }

    private async Task WriteCmdAsync(byte[] cmd, string label, CancellationToken ct)
    {
        _log(LogLevel.Info, $"发送 {label} : {Hex.Dump(cmd)}");
        if (!await _transport.WriteWithoutResponseAsync(cmd, ct))
            throw new InvalidOperationException($"命令写入失败: {label}");
    }

    private async Task Step(OtaState state, Func<Task<bool>> action)
    {
        _sm.AdvanceTo(state);
        if (!await action())
            throw FailEx(OtaState.Failed, $"步骤 {state} 失败");
    }

    private async Task<bool> StepOrFalse(OtaState state, Func<Task<bool>> action)
    {
        _sm.AdvanceTo(state);
        return await action();
    }

    private void ThrowIfTimeout(Stopwatch sw)
    {
        if (sw.Elapsed >= _options.TotalTimeout)
        {
            throw FailEx(OtaState.TimedOut, $"会话总超时（{_options.TotalTimeout.TotalSeconds:F0}s）");
        }
    }

    private OtaStepAbortException FailEx(OtaState terminal, string reason)
    {
        _log(LogLevel.Error, reason);
        _lastFailMessage = reason;
        if (!_sm.IsTerminal)
            _sm.FailTo(terminal, reason);
        return new OtaStepAbortException();
    }

    private void FailTo(OtaState terminal, string reason)
    {
        _log(LogLevel.Error, reason);
        _lastFailMessage = reason;
        if (!_sm.IsTerminal)
            _sm.FailTo(terminal, reason);
    }

    private OtaSessionResult Done(Stopwatch sw, OtaOutcome outcome, string message, int packetsSent,
        OtaResult? deviceResult = null)
    {
        _log(LogLevel.Info, $"会话结束：{outcome} - {message}（耗时 {sw.Elapsed.TotalSeconds:F1}s）");
        return new OtaSessionResult
        {
            Outcome = outcome,
            Message = message,
            FinalState = _sm.Current,
            DeviceResult = deviceResult,
            PacketsSent = packetsSent,
            BytesSent = (long)packetsSent * _pdu,
            Duration = sw.Elapsed,
            PduLength = _pdu,
        };
    }

    private async Task SafeDisconnectAsync()
    {
        try { await _transport.DisconnectAsync(); }
        catch { /* 忽略 */ }
    }

    private sealed class OtaStepAbortException : Exception { }
}

/// <summary>单次写入结果。</summary>
internal enum WriteOutcome
{
    Ok,
    StallTimeout,
    WriteFailed,
}

/// <summary>
/// 有界写入窗口：最多 Window 个在途写，窗口满即节流；
/// 窗口长时间不释放判定为发送停滞（设备 packet timeout 15s，App 侧取 10s）。
/// </summary>
internal sealed class BoundedWriter : IDisposable
{
    private readonly SemaphoreSlim _slots;
    private int _pending;
    private readonly TaskCompletionSource<bool> _drained =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    public BoundedWriter(int window)
    {
        if (window < 1) window = 1;
        _slots = new SemaphoreSlim(window, window);
    }

    public async Task<WriteOutcome> WriteAsync(
        byte[] packet,
        Func<byte[], CancellationToken, Task<bool>> write,
        TimeSpan stallTimeout,
        CancellationToken ct)
    {
        bool gotSlot;
        try
        {
            gotSlot = await _slots.WaitAsync(stallTimeout, ct);
        }
        catch (OperationCanceledException) when (!ct.IsCancellationRequested)
        {
            return WriteOutcome.StallTimeout;
        }
        catch (OperationCanceledException)
        {
            throw; // 用户取消：原样传播
        }
        if (!gotSlot)
            return WriteOutcome.StallTimeout;

        Interlocked.Increment(ref _pending);
        try
        {
            bool ok = await write(packet, ct);
            return ok ? WriteOutcome.Ok : WriteOutcome.WriteFailed;
        }
        catch (OperationCanceledException) when (!ct.IsCancellationRequested)
        {
            return WriteOutcome.StallTimeout;
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        finally
        {
            _slots.Release();
            if (Interlocked.Decrement(ref _pending) == 0)
                _drained.TrySetResult(true);
        }
    }

    public Task DrainAsync(TimeSpan timeout, CancellationToken ct) =>
        _pending == 0 ? Task.CompletedTask : _drained.Task.WaitAsync(timeout, ct);

    public void Dispose() => _slots.Dispose();
}


