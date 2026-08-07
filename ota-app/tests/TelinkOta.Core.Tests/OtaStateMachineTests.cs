using NUnit.Framework;
using TelinkOta.Core.Ota;

namespace TelinkOta.Core.Tests;

public class OtaStateMachineTests
{
    [Test]
    public void HappyPath_AdvancesThroughAllStates()
    {
        var sm = new OtaStateMachine();
        var order = new[]
        {
            OtaState.Scanning, OtaState.Connecting, OtaState.DiscoveringServices,
            OtaState.EnablingNotifications, OtaState.ValidatingFirmware, OtaState.NegotiatingMtuAndPdu,
            OtaState.VersionCheck, OtaState.SendingStart, OtaState.Transferring, OtaState.DrainingTxQueue,
            OtaState.SendingEnd, OtaState.WaitingResult, OtaState.WaitingReboot, OtaState.Reconnecting,
            OtaState.VerifyingVersion, OtaState.Success,
        };
        foreach (var s in order)
            sm.AdvanceTo(s);
        Assert.That(sm.Current, Is.EqualTo(OtaState.Success));
        Assert.That(sm.IsTerminal, Is.True);
        Assert.That(sm.History.Count, Is.EqualTo(order.Length)); // 不含初始 Idle
    }

    [Test]
    public void IllegalSkip_Throws()
    {
        var sm = new OtaStateMachine();
        Assert.Throws<InvalidOperationException>(() => sm.AdvanceTo(OtaState.Transferring)); // 从 Idle 直接跳（仅允许 Scanning/Connecting）
    }

    [Test]
    public void IllegalBackwards_Throws()
    {
        var sm = new OtaStateMachine();
        sm.AdvanceTo(OtaState.Connecting);
        Assert.Throws<InvalidOperationException>(() => sm.AdvanceTo(OtaState.Scanning));
    }

    [Test]
    public void AnyState_CanFailTo_Terminal()
    {
        var sm = new OtaStateMachine();
        WalkTo(sm, OtaState.Transferring);
        sm.FailTo(OtaState.Failed);
        Assert.That(sm.Current, Is.EqualTo(OtaState.Failed));
        Assert.That(sm.IsTerminal, Is.True);
    }

    [Test]
    public void AnyState_CanCancel_OrTimeout()
    {
        var sm = new OtaStateMachine();
        WalkTo(sm, OtaState.Connecting);
        sm.FailTo(OtaState.Cancelled);
        Assert.That(sm.Current, Is.EqualTo(OtaState.Cancelled));

        var sm2 = new OtaStateMachine();
        WalkTo(sm2, OtaState.SendingEnd);
        sm2.FailTo(OtaState.TimedOut);
        Assert.That(sm2.Current, Is.EqualTo(OtaState.TimedOut));

        var sm3 = new OtaStateMachine();
        WalkTo(sm3, OtaState.Transferring);
        sm3.FailTo(OtaState.Disconnected);
        Assert.That(sm3.Current, Is.EqualTo(OtaState.Disconnected));
    }

    private static readonly OtaState[] WalkOrder =
    {
        OtaState.Connecting, OtaState.DiscoveringServices, OtaState.EnablingNotifications,
        OtaState.ValidatingFirmware, OtaState.NegotiatingMtuAndPdu, OtaState.VersionCheck,
        OtaState.SendingStart, OtaState.Transferring, OtaState.DrainingTxQueue, OtaState.SendingEnd,
    };

    private static void WalkTo(OtaStateMachine sm, OtaState target)
    {
        foreach (var s in WalkOrder)
        {
            sm.AdvanceTo(s);
            if (s == target) return;
        }
    }

    [Test]
    public void TerminalState_CannotTransition()
    {
        var sm = new OtaStateMachine();
        sm.FailTo(OtaState.Failed);
        Assert.Throws<InvalidOperationException>(() => sm.AdvanceTo(OtaState.Success));
    }

    [Test]
    public void StateChanged_FiresEachTransition()
    {
        var sm = new OtaStateMachine();
        var seen = new List<OtaState>();
        sm.StateChanged += s => seen.Add(s);
        sm.AdvanceTo(OtaState.Connecting);
        sm.AdvanceTo(OtaState.DiscoveringServices);
        sm.FailTo(OtaState.Failed);
        Assert.That(seen, Is.EqualTo(new[] { OtaState.Connecting, OtaState.DiscoveringServices, OtaState.Failed }));
    }
}
