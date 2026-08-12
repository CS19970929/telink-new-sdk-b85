namespace TelinkOta.Core.Bms;

/// <summary>
/// BMS 业务寄存器映射（vendor/ble_sample 实码核对：modbus_rtu.c read_reg + sci_upper.h stCell_Info）：
///  - 0xD120 稳定实时窗口（11 寄存器，大端 u16）；
///  - 0xD000 完整状态窗口（63 寄存器 = stCell_Info 直接平铺）；
///  - 0xD115/0xD116 SystemStatus（u32 位标志）；
///  - 0xD100~0xD114 故障记录；
///  - 0x2100~0x2140 保护参数（13 组 × 5）；
///  - 0xC002/0xC012/0xC022 产品信息（ASCII）；
///  - 0x0000 MAC（3 寄存器）、0x0100 蓝牙名（12 寄存器）。
/// 单位换算（对接文档 + 结构体注释）：电压 /100 V；电流 u16 /10 A；温度 /10 - 40 ℃；单体 mV；容量 Ah*100。
/// </summary>
public static class BmsRegisters
{
    public const ushort RealtimeBase = 0xD120;
    public const ushort RealtimeCount = 11;
    public const ushort RealtimeMagic = 0x4253; // "BS"
    public const ushort RealtimeVersion = 0x0001;

    public const ushort CellsBase = 0xD000;
    public const ushort CellsCount = 0x3F;      // 63 寄存器（stCell_Info 平铺）

    public const ushort FaultBase = 0xD100;
    public const ushort FaultCount = 0x15;      // 21 寄存器

    public const ushort SystemStatusBase = 0xD115;
    public const ushort SystemStatusCount = 2;

    public const ushort ProtectBase = 0x2100;
    public const ushort ProtectCount = 0x41;    // 65 寄存器（13 组 × 5）

    public const ushort ProdSnBase = 0xC002;
    public const ushort ProdHwBase = 0xC012;
    public const ushort ProdSwBase = 0xC022;
    public const ushort ProdCount = 16;

    public const ushort MacBase = 0x0000;
    public const ushort MacCount = 3;

    public const ushort BtNameBase = 0x0100;
    public const ushort BtNameCount = 12;
}

/// <summary>0xD000 完整窗口的字段索引（stCell_Info 平铺）。</summary>
public static class LegacyWindow
{
    public const int CellCount = 32;
    public const int MaxCell = 32;          // u16VCellMax (mV)
    public const int MinCell = 33;          // u16VCellMin (mV)
    public const int MaxCellPos = 34;
    public const int MinCellPos = 35;
    public const int CellDelta = 36;        // mV
    public const int PackVoltage = 37;      // V*100
    public const int TempBase = 38;         // u16Temperature[10], (+40℃)*10
    public const int TempCount = 10;
    public const int TempMax = 48;
    public const int TempMin = 49;
    public const int Ichg = 50;             // A*10
    public const int Idischg = 51;          // A*10
    public const int Soc = 52;              // %
    public const int Soh = 53;              // %
    public const int CapacityNow = 54;      // Ah*100
    public const int CapacityFull = 55;     // Ah*100
    public const int CapacityFactory = 56;  // Ah*100
    public const int CycleTimes = 57;
    public const int FaultFirst = 58;
    public const int FaultSecond = 59;
    public const int FaultThird = 60;
    public const int BalanceFlag1 = 61;
    public const int BalanceFlag2 = 62;
}

/// <summary>保护参数组（0x2100 起 13 组 × 5：First/Second/Third/Rcv/Filter）。</summary>
public static class ProtectParams
{
    public static readonly (string Name, ushort Base)[] Groups =
    {
        ("Vcell OVP", 0x2100), ("Vcell UVP", 0x2105),
        ("Vbus OVP", 0x210A), ("Vbus UVP", 0x210F),
        ("Ichg OCP", 0x2114), ("Idsg OCP", 0x2119),
        ("Tchg OTP", 0x211E), ("Tchg UTP", 0x2123),
        ("Tdischg OTP", 0x2128), ("Tdischg UTP", 0x212D),
        ("Tmos OTP", 0x2132), ("Vdelta OVP", 0x2137),
        ("SocLow", 0x213C),
    };
}

/// <summary>
/// 电池状态快照。未读到的量保持 null。
/// </summary>
public sealed class BatterySnapshot
{
    public bool IsValid { get; set; }
    public bool UsingStableWindow { get; set; }

    // ---- 实时量（0xD120 稳定窗口或 0xD000 完整窗口）----
    public double? PackVoltageV { get; set; }
    public double? PackCurrentA { get; set; }
    public int? SocPercent { get; set; }
    public double? MaxTempC { get; set; }
    public double? MinTempC { get; set; }
    public double? MosTempC { get; set; }
    public int? MaxCellMv { get; set; }
    public int? MinCellMv { get; set; }
    public int? CellDeltaMv { get; set; }

    // ---- 完整窗口扩展量（0xD000）----
    public IReadOnlyList<int> CellVoltagesMv { get; set; } = Array.Empty<int>();
    public int? MaxCellPosition { get; set; }
    public int? MinCellPosition { get; set; }
    public IReadOnlyList<double> TemperaturesC { get; set; } = Array.Empty<double>();
    public double? ChargeCurrentA { get; set; }
    public double? DischargeCurrentA { get; set; }
    public int? SohPercent { get; set; }
    public double? CapacityNowAh { get; set; }
    public double? CapacityFullAh { get; set; }
    public double? CapacityFactoryAh { get; set; }
    public int? CycleTimes { get; set; }
    public ushort? MdlFaultFirst { get; set; }
    public ushort? MdlFaultSecond { get; set; }
    public ushort? MdlFaultThird { get; set; }
    public ushort? BalanceFlag1 { get; set; }
    public ushort? BalanceFlag2 { get; set; }

    // ---- 状态与故障 ----
    public uint? SystemStatus { get; set; }
    public IReadOnlyList<string> FaultRecordsHex { get; set; } = Array.Empty<string>();
    public IReadOnlyList<(string Name, ushort[] Values)> ProtectValues { get; set; } = Array.Empty<(string, ushort[])>();

    // ---- 产品信息 ----
    public string Mac { get; set; } = "";
    public string BtName { get; set; } = "";
    public string SerialNumber { get; set; } = "";
    public string HardwareVersion { get; set; } = "";
    public string SoftwareVersion { get; set; } = "";

    // ================= 解析（Modbus 0x03 响应数据区，大端 u16）=================

    /// <summary>解析 0xD120 稳定窗口（11 寄存器 = 22 字节）。Magic 不符返回 null。</summary>
    public static BatterySnapshot? ParseRealtime(byte[] data)
    {
        if (data is null || data.Length < BmsRegisters.RealtimeCount * 2)
            return null;

        ushort magic = Be16(data, 0);
        if (magic != BmsRegisters.RealtimeMagic)
            return null;

        return new BatterySnapshot
        {
            IsValid = true,
            UsingStableWindow = true,
            PackVoltageV = Be16(data, 2) / 100.0,
            PackCurrentA = (short)Be16(data, 3) / 10.0,
            SocPercent = Be16(data, 4),
            MaxTempC = Be16(data, 5) / 10.0 - 40.0,
            MinTempC = Be16(data, 6) / 10.0 - 40.0,
            MosTempC = Be16(data, 7) / 10.0 - 40.0,
            MaxCellMv = Be16(data, 8),
            MinCellMv = Be16(data, 9),
            CellDeltaMv = Be16(data, 10),
        };
    }

    /// <summary>解析 0xD000 完整窗口（63 寄存器 = 126 字节，stCell_Info 平铺）。</summary>
    public static BatterySnapshot? ParseLegacyWindow(byte[] data)
    {
        if (data is null || data.Length < BmsRegisters.CellsCount * 2)
            return null;

        var snap = new BatterySnapshot
        {
            IsValid = true,
            UsingStableWindow = false,
        };

        var cells = new List<int>();
        for (int i = 0; i < LegacyWindow.CellCount; i++)
            cells.Add(Be16(data, i));
        snap.CellVoltagesMv = cells;

        snap.MaxCellMv = Be16(data, LegacyWindow.MaxCell);
        snap.MinCellMv = Be16(data, LegacyWindow.MinCell);
        snap.MaxCellPosition = Be16(data, LegacyWindow.MaxCellPos);
        snap.MinCellPosition = Be16(data, LegacyWindow.MinCellPos);
        snap.CellDeltaMv = Be16(data, LegacyWindow.CellDelta);

        ushort pack = Be16(data, LegacyWindow.PackVoltage);
        snap.PackVoltageV = pack > 0 ? pack / 100.0 : null;

        var temps = new List<double>();
        for (int i = 0; i < LegacyWindow.TempCount; i++)
            temps.Add(Be16(data, LegacyWindow.TempBase + i) / 10.0 - 40.0);
        snap.TemperaturesC = temps;
        snap.MaxTempC = Be16(data, LegacyWindow.TempMax) / 10.0 - 40.0;
        snap.MinTempC = Be16(data, LegacyWindow.TempMin) / 10.0 - 40.0;

        snap.ChargeCurrentA = Be16(data, LegacyWindow.Ichg) / 10.0;
        snap.DischargeCurrentA = Be16(data, LegacyWindow.Idischg) / 10.0;
        snap.SocPercent = Be16(data, LegacyWindow.Soc);
        snap.SohPercent = Be16(data, LegacyWindow.Soh);
        snap.CapacityNowAh = Be16(data, LegacyWindow.CapacityNow) / 100.0;
        snap.CapacityFullAh = Be16(data, LegacyWindow.CapacityFull) / 100.0;
        snap.CapacityFactoryAh = Be16(data, LegacyWindow.CapacityFactory) / 100.0;
        snap.CycleTimes = Be16(data, LegacyWindow.CycleTimes);

        snap.MdlFaultFirst = Be16(data, LegacyWindow.FaultFirst);
        snap.MdlFaultSecond = Be16(data, LegacyWindow.FaultSecond);
        snap.MdlFaultThird = Be16(data, LegacyWindow.FaultThird);
        snap.BalanceFlag1 = Be16(data, LegacyWindow.BalanceFlag1);
        snap.BalanceFlag2 = Be16(data, LegacyWindow.BalanceFlag2);

        // 电流（若稳定窗口未读到，用充/放电电流推导）
        if (snap.DischargeCurrentA > 0)
            snap.PackCurrentA = -snap.DischargeCurrentA;
        else if (snap.ChargeCurrentA > 0)
            snap.PackCurrentA = snap.ChargeCurrentA;

        return snap;
    }

    /// <summary>解析 0xD115/0xD116 SystemStatus（4 字节 → u32）。</summary>
    public static uint? ParseSystemStatus(byte[] data)
    {
        if (data is null || data.Length < 4)
            return null;
        // 固件 modbus_rtu.c：0xD115 返回 SystemStatus 低 16 位，0xD116 返回高 16 位；
        // 每个寄存器内部仍按 Modbus 大端传输。
        return (uint)(Be16(data, 0) | (Be16(data, 1) << 16));
    }

    /// <summary>解析故障记录窗口（0xD100~0xD114，21 寄存器）为十六进制行。</summary>
    public static IReadOnlyList<string> ParseFaultRecords(byte[] data)
    {
        var rows = new List<string>();
        if (data is null)
            return rows;
        for (int i = 0; i + 8 <= data.Length; i += 8)
        {
            rows.Add(Convert.ToHexString(data, i, 8));
        }
        return rows;
    }

    /// <summary>解析 MAC 寄存器（3 寄存器 = 6 字节）。</summary>
    public static string ParseMac(byte[] data)
    {
        if (data is null || data.Length < 6)
            return "";
        return string.Join(":", data.Take(6).Select(b => b.ToString("X2")));
    }

    /// <summary>解析产品信息 ASCII 寄存器区（每寄存器两个字符，大端，遇 0 截断）。</summary>
    public static string ParseAsciiRegs(byte[] data)
    {
        if (data is null || data.Length == 0)
            return "";
        var sb = new System.Text.StringBuilder(data.Length);
        foreach (byte b in data)
        {
            if (b == 0)
                break;
            sb.Append((char)b);
        }
        return sb.ToString().Trim();
    }

    private static ushort Be16(byte[] d, int regIndex) =>
        (ushort)((d[regIndex * 2] << 8) | d[regIndex * 2 + 1]);
}

/// <summary>SystemStatus 位标志说明（sh367309_datadeal.h System_Status_Flag）。</summary>
public static class SystemStatusBits
{
    public static readonly (int Bit, string Name)[] Bits =
    {
        (0,  "StartUpBMS"), (1, "MOS_PRE"), (2, "MOS_CHG"), (3, "MOS_DSG"),
        (4,  "Relay_PRE"), (5, "Relay_CHG"), (6, "Relay_DSG"), (7, "Relay_MAIN"),
        (8,  "Heat"), (9, "Cool"), (10, "AFE1"), (11, "AFE2"),
        (12, "Balance"), (13, "ToSleep"), (14, "BnCloseIO"), (15, "HeatCloseIO"),
        (16, "SysLimits"), (17, "CBCCloseIO"), (18, "DriverExtCtrl"), (19, "Rsvd"),
        (20, "ProjectVer(4bit)"), (24, "Rsvd2"),
    };

    public static IReadOnlyList<string> Decode(uint status)
    {
        var list = new List<string>();
        foreach (var (bit, name) in Bits)
        {
            if (bit == 20)
            {
                int ver = (int)((status >> 20) & 0xF);
                if (ver != 0)
                    list.Add($"ProjectVer={ver}");
                continue;
            }
            if (bit >= 24) continue;
            if ((status & (1u << bit)) != 0)
                list.Add(name);
        }
        return list;
    }
}

/// <summary>MDLCHGFAULT 位标志说明（sci_upper.h MDLCHGFAULT_BITS）。</summary>
public static class FaultBits
{
    public static readonly (int Bit, string Name)[] Bits =
    {
        (0, "CellOvp"), (1, "CellUvp"), (2, "BatOvp"), (3, "BatUvp"),
        (4, "IchgOcp"), (5, "IdischgOcp"), (6, "CellChgOtp"), (7, "CellDischgOtp"),
        (8, "CellChgUtp"), (9, "CellDischgUtp"), (10, "VcellDeltaBig"), (11, "TempDeltaBig"),
        (12, "SocLow"), (13, "TmosOtp"), (14, "Rsvd1"), (15, "Rsvd2"),
    };

    public static IReadOnlyList<string> Decode(ushort flags)
    {
        var list = new List<string>();
        foreach (var (bit, name) in Bits)
        {
            if ((flags & (1 << bit)) != 0)
                list.Add(name);
        }
        return list;
    }
}
