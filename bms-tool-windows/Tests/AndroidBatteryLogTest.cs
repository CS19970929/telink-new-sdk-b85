using System.Globalization;
using BmsTool.Android;
using BmsTool.Windows;

static class AndroidBatteryLogTest
{
    private static void Check(bool condition, string failure)
    {
        if (!condition) throw new Exception(failure);
    }

    public static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("fr-FR");
        var time = new DateTimeOffset(2026, 11, 24, 9, 12, 30, TimeSpan.FromHours(8));
        var sample = new BatterySnapshot
        {
            PackVoltageV = 48.72, CurrentA = -3.25, SocPercent = 54,
            MinTempC = 18.5, MaxTempC = 24.2, MosTempC = 29.1, CapacityNowAh = 14.6, CycleCount = 22,
            CellMillivolts = [3300, BmsRegisters.MissingCellVoltageMv, 3410]
        };
        string[] header = BatteryLogCsv.Header.Split(',');
        string[] row = BatteryLogCsv.Row(time, "EC:F6:4C:E9:F1:3D", "ok", sample).Split(',');
        Check(row.Length == header.Length && header.Length == 47, "all logical cell slots must have stable columns");
        string Get(string column) => row[Array.IndexOf(header, column)];
        Check(Get("time") == "2026-11-24T09:12:30.0000000+08:00" && Get("mac") == "EC:F6:4C:E9:F1:3D", "timestamp and device identity");
        Check(Get("pack_v") == "48.72" && Get("current_a") == "-3.25" && Get("soc_pct") == "54", "numeric units and invariant decimal separator");
        Check(Get("temp_min_c") == "18.5" && Get("temp_max_c") == "24.2" && Get("mos_temp_c") == "29.1", "temperatures");
        Check(Get("cell_01_mv") == "3300" && Get("cell_02_mv") == "" && Get("cell_03_mv") == "3410" && Get("cell_32_mv") == "", "missing cells never compact or use fake zeros");
        Check(Get("valid_cell_count") == "2" && Get("cell_delta_mv") == "110" && Get("cell_min_mv") == "3300" && Get("cell_max_mv") == "3410", "statistics count valid cells only");
        row = BatteryLogCsv.Row(time, "EC:F6:4C:E9:F1:3D", "read_error", null).Split(',');
        Check(row.Length == header.Length && Get("status") == "read_error" && row.Skip(3).All(value => value == ""), "failed sampling must not repeat stale values");
        row = BatteryLogCsv.Row(time, "EC:F6:4C:E9:F1:3D", "ok",
            new BatterySnapshot { PackVoltageV = double.NaN, CurrentA = double.PositiveInfinity, SocPercent = 255 }).Split(',');
        Check(Get("pack_v") == "" && Get("current_a") == "" && Get("soc_pct") == "" && Get("valid_cell_count") == "0" &&
            Get("cell_delta_mv") == "", "unavailable measurements are blank");
        Console.WriteLine("PASS Android battery log CSV fields, missing cells, units, and read gaps");
    }
}
