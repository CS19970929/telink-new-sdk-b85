using Bms.Ota.Core.Telink;

static void AssertHex(string expected, byte[] actual, string name)
{
    string got = Convert.ToHexString(actual);
    if (!string.Equals(expected, got, StringComparison.OrdinalIgnoreCase))
        throw new Exception($"{name}: expected {expected}, got {got}");
}

AssertHex("01FF", LegacyPacketBuilder.BuildStart(), "LEGACY_START");
var data0 = Enumerable.Range(0, 16).Select(i => (byte)i).ToArray();
AssertHex("0000000102030405060708090A0B0C0D0E0F7BF3", LegacyPacketBuilder.BuildData(0, data0, 0), "LEGACY_DATA0");
AssertHex("02FF3412CBED", LegacyPacketBuilder.BuildEnd(0x1234), "END");
AssertHex("03FF4000", ExtendedPacketBuilder.BuildStart(64), "EXT64_START");

if (!OtaResult.TryParse([0x06, 0xFF, 0x00], out var success) || success is null || !success.IsSuccess)
    throw new Exception("OTA_RESULT success parser failed");
if (!OtaResult.TryParse([0x06, 0xFF, 0x09], out var pduErr) || pduErr?.Name != "OTA_PDU_LEN_ERR")
    throw new Exception("OTA_RESULT PDU parser failed");

Console.WriteLine("Bms.Ota.Core smoke tests PASS");
