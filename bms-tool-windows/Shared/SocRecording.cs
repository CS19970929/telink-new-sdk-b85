using System.Globalization;
using System.IO;
using System.Text;

namespace BmsTool.Windows;

public sealed record SocRecord(
    DateTimeOffset CapturedUtc,
    uint Timestamp32k,
    string CellVoltagesMv,
    int PackVoltageMv,
    int CellMinMv,
    int CellMaxMv,
    int CellDeltaMv,
    int CurrentRawMa,
    int CurrentMa,
    int TempMinX10,
    int TempMaxX10,
    bool SampleValid,
    ushort FirmwareSocEst,
    ushort FirmwareSocDisplay,
    int NominalCapacity0p1Ah,
    int FullCapacity0p1Ah,
    int RemainingCapacity0p1Ah,
    string OcvState,
    ushort OcvSoc,
    ushort OcvLow,
    ushort OcvHigh,
    ushort RestSeconds,
    string EndpointState,
    string LearningState,
    int? TteMinutes,
    int? TtfMinutes,
    string ChargerState,
    string LoadState,
    string ProtectionFlags,
    string BalancingActive,
    string HeatingActive,
    string SocEvent)
{
    public const string CsvHeader = "captured_utc,timestamp_32k,cell_voltages_mv,pack_voltage_mv,cell_min_mv,cell_max_mv,cell_delta_mv,current_raw_ma,current_ma,temp_min_x10,temp_max_x10,sample_valid,voltage_valid,firmware_soc_est,soc_est,soc_display,nominal_capacity_0p1ah,full_capacity_0p1ah,remaining_capacity_0p1ah,ocv_state,ocv_soc,ocv_low,ocv_high,rest_seconds,endpoint_state,learning_state,tte_min,ttf_min,charger_state,load_state,protection_flags,balancing_active,heating_active,soc_event,true_soc,firmware_seed_soc";

    public static SocRecord From(DiagnosticCapture capture, SocDiagnosticSnapshot soc, BatterySnapshot battery)
    {
        ushort[] words = capture.Words ?? throw new InvalidDataException("SOC diagnostics words unavailable");
        // The public realtime window reports charge-positive/discharge-negative;
        // the SOC core contract is the inverse: charge-negative/discharge-positive.
        int currentMa = -(int)Math.Round(battery.CurrentA * 1000.0, MidpointRounding.AwayFromZero);
        string action = soc.LastSocAction == "NONE" ? "" : soc.LastSocAction;
        if (soc.EndpointState is "CONFIRMED_FULL" or "CONFIRMED_EMPTY")
            action = string.IsNullOrEmpty(action) ? soc.EndpointState : action + ";" + soc.EndpointState;
        return new SocRecord(
            DateTimeOffset.UtcNow, BmsDiagnostics.U32(words, 198),
            string.Join(';', battery.CellMillivolts.Where(v => v != BmsRegisters.MissingCellVoltageMv)),
            (int)Math.Round(battery.PackVoltageV * 1000.0), battery.MinCellMv, battery.MaxCellMv,
            battery.CellDeltaMv, BmsDiagnostics.I32(words, 194), currentMa,
            (int)Math.Round((battery.MinTempC + 40.0) * 10.0),
            (int)Math.Round((battery.MaxTempC + 40.0) * 10.0),
            (words[193] & 1) != 0, soc.SocEstimate, soc.SocDisplay,
            (int)Math.Round(soc.NominalCapacityAh * 10.0),
            (int)Math.Round(soc.EffectiveCapacityAh * 10.0),
            (int)Math.Round(soc.RemainingCapacityAh * 10.0),
            soc.OcvState, soc.OcvCenter, soc.OcvLow, soc.OcvHigh, soc.RestSeconds,
            soc.EndpointState, soc.LearningState, soc.TimeToEmptyMinutes, soc.TimeToFullMinutes,
            currentMa < -200 ? "PRESENT/CHARGE" : "UNKNOWN_OR_IDLE",
            currentMa > 200 ? "PRESENT/DISCHARGE" : "UNKNOWN_OR_IDLE",
            $"0x{battery.ProtectionLevel1Raw:X4}/0x{battery.ProtectionLevel2Raw:X4}/0x{battery.ProtectionLevel3Raw:X4}",
            battery.BalancingOn ? "1" : "0", battery.HeatingOn ? "1" : "0", action);
    }

    public static void WriteCsv(string path, IEnumerable<SocRecord> records)
    {
        using var writer = new StreamWriter(path, false, new UTF8Encoding(false));
        writer.WriteLine(CsvHeader);
        foreach (SocRecord record in records) writer.WriteLine(record.ToCsvLine());
    }

    public string ToCsvLine()
    {
        static string Q(string value) => '"' + value.Replace("\"", "\"\"") + '"';
        string[] values = {
            CapturedUtc.ToString("O", CultureInfo.InvariantCulture), Timestamp32k.ToString(), Q(CellVoltagesMv),
            PackVoltageMv.ToString(), CellMinMv.ToString(), CellMaxMv.ToString(), CellDeltaMv.ToString(),
            CurrentRawMa.ToString(), CurrentMa.ToString(), TempMinX10.ToString(), TempMaxX10.ToString(),
            SampleValid ? "1" : "0", SampleValid ? "1" : "0", FirmwareSocEst.ToString(), FirmwareSocEst.ToString(),
            FirmwareSocDisplay.ToString(), NominalCapacity0p1Ah.ToString(), FullCapacity0p1Ah.ToString(),
            RemainingCapacity0p1Ah.ToString(), OcvState, OcvSoc.ToString(), OcvLow.ToString(),
            OcvHigh.ToString(), RestSeconds.ToString(), EndpointState, LearningState,
            TteMinutes?.ToString() ?? "", TtfMinutes?.ToString() ?? "", ChargerState, LoadState,
            ProtectionFlags, BalancingActive, HeatingActive, Q(SocEvent), "", FirmwareSocEst.ToString()
        };
        return string.Join(',', values);
    }

    public static List<SocRecord> ReadCsv(string path)
    {
        using var reader = new StreamReader(path, Encoding.UTF8, true);
        string[] header = ParseCsvLine(reader.ReadLine() ?? throw new InvalidDataException("SOC Record CSV is empty"));
        var at = header.Select((name, index) => (name, index)).ToDictionary(x => x.name, x => x.index,
            StringComparer.OrdinalIgnoreCase);
        string Get(string[] values, string name, string fallback = "") =>
            at.TryGetValue(name, out int index) && index < values.Length ? values[index].Trim('"') : fallback;
        int Int(string[] values, string name, int fallback = 0) =>
            int.TryParse(Get(values, name), NumberStyles.Integer, CultureInfo.InvariantCulture, out int value) ? value : fallback;
        uint Uint(string[] values, string name, uint fallback = 0) =>
            uint.TryParse(Get(values, name), NumberStyles.Integer, CultureInfo.InvariantCulture, out uint value) ? value : fallback;
        var records = new List<SocRecord>();
        string? line;
        while ((line = reader.ReadLine()) is not null)
        {
            if (string.IsNullOrWhiteSpace(line)) continue;
            string[] v = ParseCsvLine(line);
            records.Add(new SocRecord(
                DateTimeOffset.TryParse(Get(v, "captured_utc"), CultureInfo.InvariantCulture,
                    DateTimeStyles.RoundtripKind, out var captured) ? captured : DateTimeOffset.MinValue,
                Uint(v, "timestamp_32k"), Get(v, "cell_voltages_mv"),
                Int(v, "pack_voltage_mv"), Int(v, "cell_min_mv"), Int(v, "cell_max_mv"),
                Int(v, "cell_delta_mv"), Int(v, "current_raw_ma"), Int(v, "current_ma"),
                Int(v, "temp_min_x10"), Int(v, "temp_max_x10"), Int(v, "sample_valid") != 0,
                (ushort)Int(v, "firmware_soc_est", Int(v, "soc_est")), (ushort)Int(v, "soc_display"),
                Int(v, "nominal_capacity_0p1ah"), Int(v, "full_capacity_0p1ah"),
                Int(v, "remaining_capacity_0p1ah"), Get(v, "ocv_state"),
                (ushort)Int(v, "ocv_soc"), (ushort)Int(v, "ocv_low"), (ushort)Int(v, "ocv_high"),
                (ushort)Int(v, "rest_seconds"), Get(v, "endpoint_state"), Get(v, "learning_state"),
                int.TryParse(Get(v, "tte_min"), out int tte) ? tte : null,
                int.TryParse(Get(v, "ttf_min"), out int ttf) ? ttf : null,
                Get(v, "charger_state"), Get(v, "load_state"), Get(v, "protection_flags"),
                Get(v, "balancing_active", "UNKNOWN"), Get(v, "heating_active", "UNKNOWN"),
                Get(v, "soc_event")));
        }
        return records;
    }

    private static string[] ParseCsvLine(string line)
    {
        var values = new List<string>();
        var value = new StringBuilder();
        bool quoted = false;
        for (int i = 0; i < line.Length; i++)
        {
            char c = line[i];
            if (c == '"')
            {
                if (quoted && i + 1 < line.Length && line[i + 1] == '"')
                {
                    value.Append('"');
                    i++;
                }
                else quoted = !quoted;
            }
            else if (c == ',' && !quoted)
            {
                values.Add(value.ToString());
                value.Clear();
            }
            else value.Append(c);
        }
        if (quoted) throw new InvalidDataException("SOC Record CSV has an unterminated quoted field");
        values.Add(value.ToString());
        return values.ToArray();
    }
}
