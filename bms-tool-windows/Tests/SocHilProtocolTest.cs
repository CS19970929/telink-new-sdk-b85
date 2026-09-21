using BmsTool.Windows;
using System.Buffers.Binary;

static class SocHilProtocolTest
{
    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    public static int Main()
    {
        byte[] open = ModbusRtu.SocHilOpen(60, false);
        Require(open.Length == 11, "OPEN must fit one legacy BLE packet.");
        ModbusRtu.ValidateFrame(open);
        Require(open[1] == 0x43 && open[2] == 0x01 && open[7] == 60, "OPEN layout mismatch.");

        byte[] command = ModbusRtu.SocHilCommand(0x04, 0x1234);
        Require(command.Length == 7, "Command length mismatch.");
        Require(BinaryPrimitives.ReadUInt16BigEndian(command.AsSpan(3, 2)) == 0x1234, "Token byte order mismatch.");

        byte[] sample = ModbusRtu.SocHilSetSample(0x1234, 7, -1_500_000,
            3000, 3500, 65, (uint)(SocHilSampleFlags.Normal | SocHilSampleFlags.ChargerPresent));
        Require(sample.Length == 20, "SET_SAMPLE must be exactly ATT payload size for MTU=23.");
        ModbusRtu.ValidateFrame(sample);
        Require(BinaryPrimitives.ReadInt32BigEndian(sample.AsSpan(6, 4)) == -1_500_000, "Signed current byte order mismatch.");
        Require(sample[15] == 0 && sample[16] == 0xE0 && sample[17] == 0x07, "24-bit flags mismatch.");

        byte[] statusBody = new byte[42];
        statusBody[0] = 1;
        statusBody[1] = 0x43;
        statusBody[2] = 0x04;
        byte[] okStatus = ModbusRtu.Frame(statusBody);
        Require(okStatus.Length == 44, "STATUS response fixture mismatch.");
        Require(ModbusRtu.InferExpectedLength(okStatus) == 44, "STATUS expected-length inference mismatch.");
        ModbusRtu.ValidateSocHilResponse(okStatus, 0x04);

        byte[] error = ModbusRtu.Frame(new byte[] { 1, 0x43, 0x01, 4 });
        Require(ModbusRtu.InferExpectedLength(error) == 6, "Error response expected-length inference mismatch.");
        try
        {
            ModbusRtu.ValidateSocHilResponse(error, 0x01);
            throw new InvalidOperationException("Unsafe-current response was accepted.");
        }
        catch (SocHilProtocolException ex)
        {
            Require(ex.Status == 4 && ex.Message.Contains("unsafe_real_current", StringComparison.Ordinal),
                "Unsafe-current error mapping mismatch.");
        }

        Console.WriteLine("PASS SOC HIL protocol: MTU=23 request, CRC, byte order, response lengths and safety errors");
        return 0;
    }
}
