using System.Buffers.Binary;
using System.IO;

namespace BmsTool.Windows;

[Flags]
public enum SocHilSampleFlags : uint
{
    None = 0,
    SampleValid = 1u << 0,
    VoltageValid = 1u << 1,
    TemperatureValid = 1u << 2,
    Balancing = 1u << 3,
    Heating = 1u << 4,
    OpenWire = 1u << 5,
    OpenWireSuspected = 1u << 6,
    AfeFault = 1u << 7,
    TemperatureFault = 1u << 8,
    CurrentFault = 1u << 9,
    PackFault = 1u << 10,
    CellOvp = 1u << 11,
    CellUvp = 1u << 12,
    ChargerKnown = 1u << 13,
    ChargerPresent = 1u << 14,
    LoadKnown = 1u << 15,
    LoadPresent = 1u << 16,
    Normal = SampleValid | VoltageValid | TemperatureValid | ChargerKnown | LoadKnown
}

public sealed record SocHilSession(ushort Token, ushort TimeoutSeconds, byte ProtocolVersion, byte SeedSoc);

public sealed record SocHilInput(
    int CurrentMa,
    ushort CellMinMv,
    ushort CellMaxMv,
    ushort TemperatureRawX10,
    SocHilSampleFlags Flags)
{
    public static SocHilInput Normal(int currentMa, ushort cellMinMv = 3250, ushort cellMaxMv = 3260,
        ushort temperatureRawX10 = 650, bool chargerPresent = false, bool loadPresent = false)
    {
        SocHilSampleFlags flags = SocHilSampleFlags.Normal |
            (chargerPresent ? SocHilSampleFlags.ChargerPresent : 0) |
            (loadPresent ? SocHilSampleFlags.LoadPresent : 0);
        return new(currentMa, cellMinMv, cellMaxMv, temperatureRawX10, flags);
    }
}

public sealed record SocHilStatus(
    ushort Token,
    ushort RemainingSeconds,
    byte Sequence,
    bool SampleReady,
    uint AppliedCount,
    byte SocEstimate,
    byte SocDisplay,
    byte LastSampleState,
    byte LastIntegralDirection,
    byte LastSocAction,
    byte LastSocBefore,
    byte LastSocAfter,
    byte LastSocTarget,
    uint LastSampleElapsed32k,
    uint LastIntegralDeltaAs10,
    int CurrentMa,
    ushort CellMinMv,
    ushort CellMaxMv,
    byte EndpointState,
    byte EndpointEventFlags,
    byte OcvState,
    byte LearningState);

public sealed partial class BmsClient
{
    public async Task<SocHilSession> SocHilOpenAsync(byte seedSoc, bool learningEnable = false,
        CancellationToken ct = default)
    {
        if (seedSoc > 100) throw new ArgumentOutOfRangeException(nameof(seedSoc));
        byte[] rsp = await TransactAsync(ModbusRtu.SocHilOpen(seedSoc, learningEnable), ct);
        ModbusRtu.ValidateSocHilResponse(rsp, 0x01);
        if (rsp.Length != 12) throw new IOException("SOC HIL open response length mismatch.");
        return new SocHilSession(
            BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(4, 2)),
            BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(6, 2)), rsp[8], rsp[9]);
    }

    public async Task SocHilHeartbeatAsync(ushort token, CancellationToken ct = default)
    {
        byte[] rsp = await TransactAsync(ModbusRtu.SocHilCommand(0x02, token), ct);
        ModbusRtu.ValidateSocHilResponse(rsp, 0x02);
        if (rsp.Length != 10 || BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(4, 2)) != token)
            throw new IOException("SOC HIL heartbeat response mismatch.");
    }

    public async Task<uint> SocHilSetSampleAsync(ushort token, byte sequence, SocHilInput input,
        CancellationToken ct = default)
    {
        if (input.CellMinMv > input.CellMaxMv || input.CellMaxMv > 6000)
            throw new ArgumentOutOfRangeException(nameof(input), "SOC HIL cell range is invalid.");
        if (input.TemperatureRawX10 > 2550 || input.TemperatureRawX10 % 10 != 0)
            throw new ArgumentOutOfRangeException(nameof(input), "Temperature raw must be 0..2550 in 10-unit steps.");
        byte[] request = ModbusRtu.SocHilSetSample(token, sequence, input.CurrentMa,
            input.CellMinMv, input.CellMaxMv, checked((byte)(input.TemperatureRawX10 / 10)), (uint)input.Flags);
        byte[] rsp = await TransactAsync(request, ct);
        ModbusRtu.ValidateSocHilResponse(rsp, 0x03);
        if (rsp.Length != 13 || BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(4, 2)) != token || rsp[6] != sequence)
            throw new IOException("SOC HIL set-sample response mismatch.");
        return BinaryPrimitives.ReadUInt32BigEndian(rsp.AsSpan(7, 4));
    }

    public async Task<SocHilStatus> SocHilReadStatusAsync(ushort token, CancellationToken ct = default)
    {
        byte[] rsp = await TransactAsync(ModbusRtu.SocHilCommand(0x04, token), ct);
        ModbusRtu.ValidateSocHilResponse(rsp, 0x04);
        if (rsp.Length != 44 || BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(4, 2)) != token)
            throw new IOException("SOC HIL status response mismatch.");
        ushort U16(int offset) => BinaryPrimitives.ReadUInt16BigEndian(rsp.AsSpan(offset, 2));
        uint U32(int offset) => BinaryPrimitives.ReadUInt32BigEndian(rsp.AsSpan(offset, 4));
        return new SocHilStatus(U16(4), U16(6), rsp[8], rsp[9] != 0, U32(10),
            rsp[14], rsp[15], rsp[16], rsp[17], rsp[18], rsp[19], rsp[20], rsp[21],
            U32(22), U32(26), unchecked((int)U32(30)), U16(34), U16(36),
            rsp[38], rsp[39], rsp[40], rsp[41]);
    }

    public async Task SocHilCloseAsync(ushort token, CancellationToken ct = default)
    {
        byte[] rsp = await TransactAsync(ModbusRtu.SocHilCommand(0x05, token), ct);
        ModbusRtu.ValidateSocHilResponse(rsp, 0x05);
        if (rsp.Length != 6) throw new IOException("SOC HIL close response length mismatch.");
    }
}
