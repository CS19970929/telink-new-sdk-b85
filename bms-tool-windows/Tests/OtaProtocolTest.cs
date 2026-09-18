using BmsTool.Windows;

class Program
{
    static void Check(bool condition, string why)
    {
        if (!condition) throw new Exception(why);
    }

    static void Main()
    {
        byte[] legacyStart = TelinkOtaProtocol.BuildLegacyStart();
        Check(legacyStart.SequenceEqual(new byte[] { 0x01, 0xFF }), "legacy START");

        byte[] extendedStart = TelinkOtaProtocol.BuildExtendedStart(64);
        Check(extendedStart.Length == 20, "START_EXT must be 20 bytes");
        Check(extendedStart[0] == 0x03 && extendedStart[1] == 0xFF, "START_EXT opcode");
        Check(extendedStart[2] == 64 && extendedStart[3] == 0, "START_EXT length/version_compare");
        Check(extendedStart.Skip(4).All(b => b == 0), "START_EXT reserved bytes");

        bool invalidLengthRejected = false;
        try { TelinkOtaProtocol.BuildExtendedStart(17); }
        catch (ArgumentOutOfRangeException) { invalidLengthRejected = true; }
        Check(invalidLengthRejected, "invalid extended PDU length accepted");

        byte[] firmware = Enumerable.Range(0, 70).Select(i => (byte)i).ToArray();

        byte[] legacy = TelinkOtaProtocol.BuildData(firmware, 0, 16, extended: false);
        Check(legacy.Length == 20, "legacy data packet length");
        Check(legacy[0] == 0 && legacy[1] == 0, "legacy index must start at 0");
        Check(legacy.AsSpan(2, 16).SequenceEqual(firmware.AsSpan(0, 16)), "legacy payload");
        Check(legacy[^2] == 0x7B && legacy[^1] == 0xF3, "Telink CRC16 known vector");

        byte[] ext0 = TelinkOtaProtocol.BuildData(firmware, 0, 64, extended: true);
        Check(ext0.Length == 68, "extended 64-byte packet length");
        Check(ext0[0] == 1 && ext0[1] == 0, "extended index must start at 1");

        byte[] ext1 = TelinkOtaProtocol.BuildData(firmware, 1, 64, extended: true);
        Check(ext1.Length == 20, "extended tail must align data to 16 bytes");
        Check(ext1[0] == 2 && ext1[1] == 0, "extended second index");
        Check(ext1.AsSpan(2, 6).SequenceEqual(firmware.AsSpan(64, 6)), "extended tail payload");
        Check(ext1.AsSpan(8, 10).ToArray().All(b => b == 0xFF), "extended tail padding");

        byte[] end = TelinkOtaProtocol.BuildEnd(1);
        Check(end.SequenceEqual(new byte[] { 0x02, 0xFF, 0x01, 0x00, 0xFE, 0xFF }), "OTA END");

        byte[] marker = new byte[32];
        marker[8] = 0x4B; marker[9] = 0x4E; marker[10] = 0x4C; marker[11] = 0x54;
        Check(TelinkOtaProtocol.HasD008TlnkStartupMarker(marker), "D008 TLNK marker");

        Console.WriteLine("PASS Telink OTA protocol: START/START_EXT, index, CRC16, padding, END, D008 marker");
    }
}
