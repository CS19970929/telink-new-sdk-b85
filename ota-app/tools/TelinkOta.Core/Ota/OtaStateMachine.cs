namespace TelinkOta.Core.Ota;

/// <summary>
/// OTA 会话状态（严格单向状态机）。
/// 任意失败统一收敛到终端状态：Cancelled / Failed / Disconnected / TimedOut。
/// 升级成功后：Success（前提：设备 Result 确认 + 重启重连 + 版本复核）。
/// </summary>
public enum OtaState
{
    Idle,
    Scanning,
    Connecting,
    DiscoveringServices,
    EnablingNotifications,
    ValidatingFirmware,
    NegotiatingMtuAndPdu,
    VersionCheck,
    SendingStart,
    Transferring,
    DrainingTxQueue,
    SendingEnd,
    WaitingResult,
    WaitingReboot,
    Reconnecting,
    VerifyingVersion,
    Success,
    Cancelled,
    Failed,
    Disconnected,
    TimedOut,
}

/// <summary>
/// 纯状态逻辑：记录迁移、校验合法性、派发事件。不依赖平台与 BLE。
/// </summary>
public sealed class OtaStateMachine
{
    private static readonly OtaState[] FlowOrder =
    {
        OtaState.Idle, OtaState.Scanning, OtaState.Connecting, OtaState.DiscoveringServices,
        OtaState.EnablingNotifications, OtaState.ValidatingFirmware, OtaState.NegotiatingMtuAndPdu,
        OtaState.VersionCheck, OtaState.SendingStart, OtaState.Transferring, OtaState.DrainingTxQueue,
        OtaState.SendingEnd, OtaState.WaitingResult, OtaState.WaitingReboot, OtaState.Reconnecting,
        OtaState.VerifyingVersion, OtaState.Success,
    };

    private static readonly OtaState[] TerminalStates =
    {
        OtaState.Success, OtaState.Cancelled, OtaState.Failed, OtaState.Disconnected, OtaState.TimedOut,
    };

    public OtaState Current { get; private set; } = OtaState.Idle;

    public IReadOnlyList<OtaState> History => _history;
    private readonly List<OtaState> _history = new();

    public event Action<OtaState>? StateChanged;

    /// <summary>向前推进到目标状态（必须是流程顺序中的后续状态或指定终端状态）。</summary>
    public bool AdvanceTo(OtaState target, string? reason = null)
    {
        if (!IsValidTransition(Current, target))
        {
            throw new InvalidOperationException(
                $"非法状态迁移 {Current} -> {target}{(string.IsNullOrEmpty(reason) ? "" : $"（{reason}）")}");
        }
        Set(target);
        return true;
    }

    /// <summary>跳转到终端状态（Cancelled/Failed/Disconnected/TimedOut 之一）。</summary>
    public bool FailTo(OtaState terminal, string? reason = null) => AdvanceTo(terminal, reason);

    /// <summary>直接设置初始状态（Idle）。</summary>
    public void Reset()
    {
        if (Current != OtaState.Idle)
        {
            throw new InvalidOperationException("会话未结束，不能 Reset");
        }
        _history.Clear();
    }

    private void Set(OtaState s)
    {
        Current = s;
        _history.Add(s);
        StateChanged?.Invoke(s);
    }

    private static bool IsValidTransition(OtaState from, OtaState to)
    {
        // 会话可从 Idle 直接进入 Scanning（UI 扫描）或 Connecting（程序化连接）
        if (from == OtaState.Idle && to is OtaState.Scanning or OtaState.Connecting)
            return true;

        int fromIdx = Array.IndexOf(FlowOrder, from);
        int toIdx = Array.IndexOf(FlowOrder, to);
        // 允许向前跳过（如重启重连后重新发现/订阅直接进入版本复核），禁止回退；
        // Idle 除外（从 Idle 只允许进入 Scanning/Connecting）
        if (fromIdx > 0 && toIdx > fromIdx)
            return true;

        // 重启后重连会重新执行服务发现/通知订阅（Reconnecting 之后的重新发现回路）
        if (from == OtaState.Reconnecting && to == OtaState.DiscoveringServices)
            return true;

        // 任意非终端状态可直接进入终端状态
        if (Array.IndexOf(TerminalStates, to) >= 0 &&
            Array.IndexOf(TerminalStates, from) < 0 &&
            to is OtaState.Cancelled or OtaState.Failed or OtaState.Disconnected or OtaState.TimedOut)
            return true;

        return false;
    }

    public bool IsTerminal => Array.IndexOf(TerminalStates, Current) >= 0;
}
