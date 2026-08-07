using NUnit.Framework;
using TelinkOta.Core.Ota;

namespace TelinkOta.Core.Tests;

/// <summary>
/// 会话级集成测试：用 FakeTransport 驱动完整状态机，验证
/// 成功、Result 错误、PDU 超限、超时、取消、Legacy 回退、重启重连、版本复核。
/// </summary>
public class OtaSessionTests
{
    private static OtaFirmware MakeFirmware(int size = 0x200)
    {
        var payload = new byte[size + 4];
        BitConverter.TryWriteBytes(payload.AsSpan(size, 4), 0x12345678);
        return new OtaFirmware
        {
            SourcePath = "test.bin",
            Payload = payload,
            DeclaredSize = (uint)size,
            BinVersion = 0x0102,
            MarkValid = true,
            CrcWasAppended = false,
            CrcVerified = false,
        };
    }

    private sealed class FakeTransport : IBleTransport
    {
        public readonly List<byte[]> Writes = new();
        public readonly List<byte[]> SppWrites = new();

        public bool ConnectResult = true;
        public bool DiscoverOtaResult = true;
        public bool DiscoverSppResult = true;
        public bool NotifyResult = true;
        public byte? ResultCode;
        public ushort LocalVersion = 0x0010;
        public bool VersionAccept = true;

        /// <summary>非 0 时抑制前 N 次版本协商响应（模拟设备不支持 Extend 版本协商）。</summary>
        public int VersionRspSuppress;

        public bool FailFirstWrite;
        public int MaxWrite = 20;
        public byte[]? SppResponseData;
        public int FailConnectAfterN;

        private int _connectCount;
        private int _versionReqCount;

        public event Action<byte[]>? OtaNotifyReceived;
        public event Action<byte[]>? SppNotifyReceived;
        public event Action? ConnectionLost;

        public ulong DeviceAddress => 0x112233445566;
        public int MaxWriteLength => MaxWrite;
        public bool Connected { get; private set; }

        public Task<bool> ConnectAsync(TimeSpan timeout, CancellationToken ct)
        {
            int n = Interlocked.Increment(ref _connectCount);
            if (FailConnectAfterN > 0 && n > FailConnectAfterN)
            {
                Connected = false;
                return Task.FromResult(false);
            }
            Connected = ConnectResult;
            return Task.FromResult(ConnectResult);
        }

        public Task<bool> DiscoverOtaServiceAsync(TimeSpan timeout, CancellationToken ct) =>
            Task.FromResult(DiscoverOtaResult);

        public Task<bool> DiscoverSppServiceAsync(TimeSpan timeout, CancellationToken ct) =>
            Task.FromResult(DiscoverSppResult);

        public Task<bool> EnableOtaNotificationsAsync(TimeSpan timeout, CancellationToken ct) =>
            Task.FromResult(NotifyResult);

        public Task<bool> EnableSppNotificationsAsync(TimeSpan timeout, CancellationToken ct) =>
            Task.FromResult(NotifyResult);

        public Task<int> NegotiateMtuAsync(TimeSpan timeout, CancellationToken ct) =>
            Task.FromResult(MaxWrite + 7);

        public async Task<bool> WriteWithoutResponseAsync(byte[] data, CancellationToken ct)
        {
            await Task.Delay(1, ct);
            // FailFirstWrite 只让"首个数据包"（长度 > 20）失败，不影响命令写入
            if (FailFirstWrite && data.Length > 20 && Writes.Count(w => w.Length > 20) == 0)
            {
                Writes.Add(data);
                return false;
            }
            Writes.Add(data);

            var opcode = data[0] | (data[1] << 8);
            if (opcode == OtaConstants.CmdOtaFwVersionReq)
            {
                int n = Interlocked.Increment(ref _versionReqCount);
                if (n > VersionRspSuppress)
                {
                    _ = Task.Run(async () =>
                    {
                        await Task.Delay(5);
                        OtaNotifyReceived?.Invoke(
                            new byte[] { 0x05, 0xFF, (byte)(LocalVersion & 0xFF), (byte)(LocalVersion >> 8), VersionAccept ? (byte)1 : (byte)0 });
                    });
                }
            }
            if (opcode == OtaConstants.CmdOtaEnd && ResultCode is { } code)
            {
                _ = Task.Run(async () =>
                {
                    await Task.Delay(5);
                    OtaNotifyReceived?.Invoke(new byte[] { 0x06, 0xFF, code });
                    if (code == 0)
                    {
                        Connected = false;
                        ConnectionLost?.Invoke();
                    }
                });
            }
            return true;
        }

        public Task<bool> WaitForTxQueueDrainedAsync(TimeSpan timeout, CancellationToken ct) =>
            Task.FromResult(true);

        public async Task<bool> WriteSppAsync(byte[] frame, CancellationToken ct)
        {
            SppWrites.Add(frame);
            if (SppResponseData is not null)
            {
                var resp = new byte[3 + SppResponseData.Length + 2];
                resp[0] = 0x01;
                resp[1] = 0x03;
                resp[2] = (byte)SppResponseData.Length;
                SppResponseData.CopyTo(resp, 3);
                ushort crc = Crc16.Compute(resp.AsSpan(0, 3 + SppResponseData.Length));
                resp[^2] = (byte)(crc & 0xFF);
                resp[^1] = (byte)(crc >> 8);
                await Task.Delay(5, ct);
                SppNotifyReceived?.Invoke(resp);
            }
            return true;
        }

        public Task DisconnectAsync()
        {
            Connected = false;
            return Task.CompletedTask;
        }

        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
    }

    private static OtaSessionOptions DefaultOptions() => new()
    {
        Protocol = OtaProtocolChoice.Extend,
        PduLength = 16,
        WriteWindow = 4,
        VerifyVersion = false,
        TotalTimeout = TimeSpan.FromSeconds(30),
        VersionRspTimeout = TimeSpan.FromMilliseconds(300),
        RebootDetectTimeout = TimeSpan.FromSeconds(2),
        ReconnectTimeout = TimeSpan.FromSeconds(5),
        ResultTimeout = TimeSpan.FromSeconds(5),
    };

    private static ushort Opcode(byte[] cmd) => (ushort)(cmd[0] | (cmd[1] << 8));

    [Test]
    public async Task Extend_Success_Flow()
    {
        var t = new FakeTransport { ResultCode = 0 };
        var session = new OtaSession(t, MakeFirmware(), DefaultOptions());
        var result = await session.RunAsync(CancellationToken.None);

        Assert.That(result.Outcome, Is.EqualTo(OtaOutcome.Success), result.Message);
        Assert.That(result.FinalState, Is.EqualTo(OtaState.Success));
        // 版本协商发生两次（升级前 + 升级后复核）
        Assert.That(t.Writes.Count(w => Opcode(w) == OtaConstants.CmdOtaFwVersionReq), Is.EqualTo(2));
        Assert.That(Opcode(t.Writes[1]), Is.EqualTo(OtaConstants.CmdOtaStartExt));
        // 最后一次写为复核版本请求，倒数第二次为 END
        Assert.That(Opcode(t.Writes[^2]), Is.EqualTo(OtaConstants.CmdOtaEnd));
        // END index_max = ceil(516/16)-1 = 33-1 = 32
        Assert.That(t.Writes[^2][2] | (t.Writes[^2][3] << 8), Is.EqualTo(32));
        Assert.That(t.Writes[^2][4] | (t.Writes[^2][5] << 8), Is.EqualTo(32 ^ 0xFFFF));
    }

    [Test]
    public async Task Legacy_Fallback_WhenNoVersionRsp()
    {
        var t = new FakeTransport
        {
            ResultCode = 0,
            VersionRspSuppress = 1,
        };
        var opts = DefaultOptions();
        opts.Protocol = OtaProtocolChoice.Auto; // 允许 Extend 超时后回退 Legacy
        var session = new OtaSession(t, MakeFirmware(), opts);
        var result = await session.RunAsync(CancellationToken.None);

        Assert.That(result.Outcome, Is.EqualTo(OtaOutcome.Success), result.Message);
        // 流程：先发 FW_VERSION_REQ（无响应）→ 回退后发 CMD_OTA_START（Legacy）
        Assert.That(Opcode(t.Writes[0]), Is.EqualTo(OtaConstants.CmdOtaFwVersionReq));
        Assert.That(Opcode(t.Writes[1]), Is.EqualTo(OtaConstants.CmdOtaStart));
    }

    [Test]
    public async Task Extend_NoFallback_WhenFixed()
    {
        var t = new FakeTransport { VersionRspSuppress = 1 };
        var opts = DefaultOptions();
        opts.Protocol = OtaProtocolChoice.Extend; // 固定 Extend，不回退
        var session = new OtaSession(t, MakeFirmware(), opts);
        var result = await session.RunAsync(CancellationToken.None);

        Assert.That(result.Outcome, Is.Not.EqualTo(OtaOutcome.Success));
        Assert.That(t.Writes.Any(w => Opcode(w) == OtaConstants.CmdOtaStart), Is.False);
    }

    [Test]
    public async Task VersionRejected_Fails()
    {
        var t = new FakeTransport { VersionAccept = false };
        var session = new OtaSession(t, MakeFirmware(), DefaultOptions());
        var result = await session.RunAsync(CancellationToken.None);
        Assert.That(result.Outcome, Is.EqualTo(OtaOutcome.Failed));
        Assert.That(result.Message, Does.Contain("拒绝"));
    }

    [Test]
    public async Task DeviceResultError_Mapped()
    {
        var t = new FakeTransport { ResultCode = 0x03 };
        var session = new OtaSession(t, MakeFirmware(), DefaultOptions());
        var result = await session.RunAsync(CancellationToken.None);
        Assert.That(result.Outcome, Is.EqualTo(OtaOutcome.Failed));
        Assert.That(result.DeviceResult, Is.Not.Null);
        Assert.That(result.DeviceResult!.Code, Is.EqualTo(0x03));
        Assert.That(result.Message, Does.Contain("CRC").IgnoreCase);
    }

    [Test]
    public async Task FirstWriteFailure_PduTooLarge_DowngradeHint()
    {
        var t = new FakeTransport
        {
            FailFirstWrite = true,
            MaxWrite = 240, // 保证 PDU=64 不会被传输层钳制
        };
        var opts = DefaultOptions();
        opts.PduLength = 64; // 需要降级
        opts.MaxWriteLength = t.MaxWrite; // 传输层允许 240，确保 PDU=64 生效
        var session = new OtaSession(t, MakeFirmware(), opts);
        var result = await session.RunAsync(CancellationToken.None);
        Assert.That(result.Outcome, Is.EqualTo(OtaOutcome.PduTooLarge));
    }

    [Test]
    public async Task TotalTimeout_TerminatesAsTimedOut()
    {
        var t = new FakeTransport { ResultCode = 0 };
        var opts = DefaultOptions();
        opts.TotalTimeout = TimeSpan.FromMilliseconds(150);
        var session = new OtaSession(t, MakeFirmware(0x4000), opts);
        var result = await session.RunAsync(CancellationToken.None);
        Assert.That(result.Outcome, Is.EqualTo(OtaOutcome.TimedOut));
        Assert.That(result.FinalState, Is.EqualTo(OtaState.TimedOut));
    }

    [Test]
    public async Task UserCancel_ReturnsCancelled()
    {
        var t = new FakeTransport { ResultCode = 0 };
        using var cts = new CancellationTokenSource();
        var session = new OtaSession(t, MakeFirmware(0x4000), DefaultOptions());
        var run = session.RunAsync(cts.Token);
        cts.CancelAfter(150);
        var result = await run;
        Assert.That(result.Outcome, Is.EqualTo(OtaOutcome.Cancelled));
        Assert.That(result.FinalState, Is.EqualTo(OtaState.Cancelled));
    }

    [Test]
    public async Task ConnectFailure_Fails()
    {
        var t = new FakeTransport { ConnectResult = false };
        var session = new OtaSession(t, MakeFirmware(), DefaultOptions());
        var result = await session.RunAsync(CancellationToken.None);
        Assert.That(result.Outcome, Is.EqualTo(OtaOutcome.Failed));
    }

    [Test]
    public async Task ServiceNotFound_Fails()
    {
        var t = new FakeTransport { DiscoverOtaResult = false };
        var session = new OtaSession(t, MakeFirmware(), DefaultOptions());
        var result = await session.RunAsync(CancellationToken.None);
        Assert.That(result.Outcome, Is.EqualTo(OtaOutcome.Failed));
        Assert.That(result.Message, Does.Contain("DiscoveringServices"));
    }

    [Test]
    public async Task ReconnectFailure_AfterReboot_Fails()
    {
        var t = new FakeTransport
        {
            ResultCode = 0,
            FailConnectAfterN = 1, // 首连成功，重启后重连失败
        };
        var session = new OtaSession(t, MakeFirmware(), DefaultOptions());
        var result = await session.RunAsync(CancellationToken.None);
        Assert.That(result.Outcome, Is.EqualTo(OtaOutcome.Failed));
        Assert.That(result.Message, Does.Contain("重连失败"));
    }

    [Test]
    public async Task VerifyVersion_ReadsBeforeAndAfter()
    {
        var t = new FakeTransport
        {
            ResultCode = 0,
            SppResponseData = System.Text.Encoding.ASCII.GetBytes("V1.2"),
        };
        var opts = DefaultOptions();
        opts.VerifyVersion = true;
        var session = new OtaSession(t, MakeFirmware(), opts);
        var result = await session.RunAsync(CancellationToken.None);

        Assert.That(result.Outcome, Is.EqualTo(OtaOutcome.Success), result.Message);
        Assert.That(result.VersionBefore, Is.EqualTo("V1.2"));
        Assert.That(result.VersionAfter, Is.EqualTo("V1.2"));
        Assert.That(t.SppWrites.Count, Is.GreaterThanOrEqualTo(2)); // 升级前后各一次
    }

    [Test]
    public async Task Progress_Events_Fired()
    {
        var t = new FakeTransport { ResultCode = 0 };
        var fw = MakeFirmware(0x400);
        var session = new OtaSession(t, fw, DefaultOptions());
        var progress = new List<(int, int)>();
        session.ProgressChanged += (a, b) => progress.Add((a, b));
        var result = await session.RunAsync(CancellationToken.None);
        Assert.That(result.Outcome, Is.EqualTo(OtaOutcome.Success), result.Message);
        Assert.That(progress, Is.Not.Empty);
        int total = (0x400 + 4 + 15) / 16; // ceil(1028/16) = 65
        Assert.That(progress[^1].Item1, Is.EqualTo(total));
        Assert.That(progress[^1].Item2, Is.EqualTo(total));
    }

    [Test]
    public async Task PduLength_Clamped_ToMaxWrite()
    {
        var t = new FakeTransport
        {
            ResultCode = 0,
            MaxWrite = 20, // MTU=23 场景：只能 PDU=16
        };
        var opts = DefaultOptions();
        opts.PduLength = 240;
        var session = new OtaSession(t, MakeFirmware(), opts);
        var result = await session.RunAsync(CancellationToken.None);
        Assert.That(result.Outcome, Is.EqualTo(OtaOutcome.Success), result.Message);
        Assert.That(result.PduLength, Is.EqualTo(16));
    }
}


