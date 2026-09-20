using System.Globalization;
using System.IO;

namespace BmsTool.Windows;

// Development firmware only. All requests are ordinary read-only Modbus 0x03.
public static class SocInputRecording
{
    private static uint U32(ushort[] w, int at) => ((uint)w[at] << 16) | w[at + 1];
    public const string CsvHeader = "scenario,step,timestamp_32k,current_ma,cell_min_mv,cell_max_mv,cell_delta_mv,pack_voltage_mv,temp_min_x10,temp_max_x10,sample_valid,voltage_valid,balancing,heating,openwire_active,openwire_suspected,afe_fault,temperature_fault,current_fault,pack_fault,cell_ovp,cell_uvp,charger_known,charger_present,load_known,load_present,event,true_soc,firmware_seed_soc,temperature_valid,chemistry,nominal_capacity_0p1ah,current_deadband_ma,ocv_rest_prepare_s,ocv_error_band_percent,capacity_learning_enable,cell_ovp_mv,cell_uvp_mv,device_sequence,soc_est,soc_display,full_capacity_0p1ah";

    public static async Task<uint> ReadSequenceAsync(BmsClient client, CancellationToken ct)
    {
        ushort[] h = await client.ReadRegistersAsync(0x3000, 6, ct);
        if (h[0] != 0x5343 || h[1] != 1 || h[2] != 32 || h[3] != 32)
            throw new InvalidDataException("SOC input recorder unavailable; requires BMS_SOC_RECORD_ENABLE=1 development firmware.");
        return U32(h, 4);
    }

    public static async Task<ushort[]> ReadSampleAsync(BmsClient client, uint sequence, CancellationToken ct)
    {
        ushort address = (ushort)(0x3020 + sequence % 32 * 32);
        // One main-loop transaction snapshots the whole record. The existing
        // transport reassembles fragmented BLE notifications (also at MTU=23).
        ushort[] words = await client.ReadRegistersAsync(address, 32, ct);
        Validate(words, sequence);
        return words;
    }

    public static void Validate(ushort[] words, uint sequence)
    {
        if (words.Length != 32 || U32(words, 0) != sequence ||
            U32(words, 30) != sequence)
            throw new InvalidDataException("SOC input overwritten/reset during read; capture has a gap.");
        if (words[16] is not (1 or 2) || words[17] is 0 or > 6553)
            throw new InvalidDataException("SOC input has invalid chemistry/capacity.");
    }

    public static string ToCsv(ushort[] w, uint sequence, int step)
    {
        Validate(w, sequence);
        uint flags = U32(w, 13);
        int F(int bit) => (int)((flags >> bit) & 1);
        object[] values = { 1, step, U32(w, 2), unchecked((int)U32(w, 4)),
            w[8], w[9], w[10], U32(w, 6), w[11], w[12], F(0), F(1),
            F(3), F(4), F(5), F(6), F(7), F(8), F(9), F(10), F(11), F(12), F(13),
            F(14), F(15), F(16), step == 0 ? 2 : 0, "", w[15], F(2), w[16], w[17],
            w[18], w[19], w[20], w[21], w[24], w[25], sequence, w[15], w[23], w[22] };
        return string.Join(',', values.Select(v => Convert.ToString(v, CultureInfo.InvariantCulture)));
    }
}
