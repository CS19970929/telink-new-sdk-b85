namespace TelinkOta.Core.Bms;

/// <summary>
/// BMS 业务寄存器映射（vendor/ble_sample modbus_rtu.c 实码核对）：
///  - 0xD120 稳定实时窗口（11 寄存器，大端 u16）；
///  - 0xD000 兼容窗口 u16VCell[32]（[29]=bat_temp_mv, [30]=mos_temp_mv, [31]=Vbat_mv）；
///  - 0xD115/0xD116 SystemStatus；
///  - 0xC002/0xC012/0xC022 产品信息（ASCII，大端，每寄存器两字符）。
/// 单位换算（对接文档）：电压 /100 V；电流 int16 /10 A（充电正、放电负）；温度 /10 - 40 ℃；单体 mV。
/// </summary>
public static class BmsRegisters
{
    public const ushort RealtimeBase = 0xD120;
    public const ushort RealtimeCount = 11;
    public const ushort RealtimeMagic = 0x4253; // "BS"
    public const ushort RealtimeVersion = 0x0001;

    public const ushort CellsBase = 0xD000;
    public const ushort CellsCount = 0x20;     // 32 寄存器
    public const ushort SystemStatusBase = 0xD115;
    public const ushort SystemStatusCount = 2;

    public const ushort ProdSnBase = 0xC002;
    public const ushort ProdHwBase = 0xC012;
    public const ushort ProdSwBase = 0xC022;
    public const ushort ProdCount = 16;
}

/// <summary>
/// 电池状态快照。全部字段为可空，未读到的量保持 null。
/// </summary>
public sealed class BatterySnapshot
{
    /// <summary>本次快照是否有效（至少读到一种窗口）。</summary>
    public bool IsValid { get; set; }

    /// <summary>true = 使用 0xD120 稳定窗口；false = 0xD000 兼容窗口。</summary>
    public bool UsingStableWindow { get; set; }

    public double? PackVoltageV { get; set; }
    public double? PackCurrentA { get; set; }
    public int? SocPercent { get; set; }
    public double? MaxTempC { get; set; }
    public double? MinTempC { get; set; }
    public double? MosTempC { get; set; }
    public int? MaxCellMv { get; set; }
    public int? MinCellMv { get; set; }
    public int? CellDeltaMv { get; set; }

    /// <summary>各单体电压（mV）。</summary>
    public IReadOnlyList<int> CellVoltagesMv { get; set; } = Array.Empty<int>();

    public ushort? SystemStatus { get; set; }

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

    /// <summary>解析 0xD000 兼容窗口（32 寄存器 = 64 字节），提取单体电压与总压。</summary>
    public static BatterySnapshot? ParseLegacyCells(byte[] data)
    {
        if (data is null || data.Length < BmsRegisters.CellsCount * 2)
            return null;

        var cells = new List<int>();
        for (int i = 0; i < 29; i++)
            cells.Add(Be16(data, i));

        // [31] = Vbat_mv（若为 0 则留空）
        int vbatMv = Be16(data, 31);
        return new BatterySnapshot
        {
            IsValid = true,
            UsingStableWindow = false,
            PackVoltageV = vbatMv > 0 ? vbatMv / 1000.0 : null,
            CellVoltagesMv = cells,
        };
    }

    /// <summary>解析 0xD115/0xD116 SystemStatus（4 字节 → u16）。</summary>
    public static ushort? ParseSystemStatus(byte[] data)
    {
        if (data is null || data.Length < 4)
            return null;
        return Be16(data, 0);
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
