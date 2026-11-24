using System.Globalization;
using BmsTool.Windows;

namespace BmsTool.Android;

internal static class BatteryLogCsv
{
    private static readonly string[] BaseColumns =
    [
        "time", "mac", "status", "pack_v", "current_a", "soc_pct",
        "temp_min_c", "temp_max_c", "mos_temp_c", "remaining_ah",
        "cycle_count", "valid_cell_count", "cell_min_mv", "cell_max_mv", "cell_delta_mv"
    ];

    internal static string Header { get; } = string.Join(",", BaseColumns.Concat(
        Enumerable.Range(1, BmsRegisters.CellVoltageSlotCount).Select(i => $"cell_{i:00}_mv")));

    internal static string Row(DateTimeOffset time, string mac, string status, BatterySnapshot? sample)
    {
        var fields = new string[BaseColumns.Length + BmsRegisters.CellVoltageSlotCount];
        fields[0] = time.ToString("O", CultureInfo.InvariantCulture);
        fields[1] = mac;
        fields[2] = status;
        if (sample is null) return string.Join(",", fields);

        fields[3] = Number(sample.PackVoltageV, sample.PackVoltageValid);
        fields[4] = Number(sample.CurrentA, sample.CurrentValid);
        fields[5] = sample.SocPercent is >= 0 and <= 100 ? Integer(sample.SocPercent) : "";
        fields[6] = Number(sample.MinTempC, sample.BatteryTemperatureAvailable);
        fields[7] = Number(sample.MaxTempC, sample.BatteryTemperatureAvailable);
        fields[8] = Number(sample.MosTempC, sample.MosTemperatureAvailable);
        fields[9] = Number(sample.CapacityNowAh, true);
        fields[10] = Integer(sample.CycleCount);

        int count = 0, min = int.MaxValue, max = 0;
        for (int i = 0; i < BmsRegisters.CellVoltageSlotCount; i++)
        {
            if (!sample.IsCellVoltageValid(i)) continue;
            int mv = sample.CellMillivolts[i];
            fields[BaseColumns.Length + i] = Integer(mv);
            min = Math.Min(min, mv);
            max = Math.Max(max, mv);
            count++;
        }
        fields[11] = Integer(count);
        if (count > 0)
        {
            fields[12] = Integer(min);
            fields[13] = Integer(max);
            fields[14] = Integer(max - min);
        }
        return string.Join(",", fields);
    }

    private static string Integer(int value) => value.ToString(CultureInfo.InvariantCulture);
    private static string Number(double value, bool valid) =>
        valid && double.IsFinite(value) ? value.ToString("0.###", CultureInfo.InvariantCulture) : "";
}
