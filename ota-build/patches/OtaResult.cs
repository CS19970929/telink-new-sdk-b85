namespace Bms.Ota.Core.Telink;

public sealed record OtaResult(byte Code, string Name, string Description)
{
    public bool IsSuccess => Code == 0x00;

    public static bool TryParse(ReadOnlySpan<byte> value, out OtaResult? result)
    {
        result = null;
        if (value.Length < 3 || value[0] != 0x06 || value[1] != 0xFF)
            return false;

        byte code = value[2];
        result = code switch
        {
            0x00 => new(code, "OTA_SUCCESS", "success"),
            0x01 => new(code, "OTA_DATA_PACKET_SEQ_ERR", "packet sequence error"),
            0x02 => new(code, "OTA_PACKET_INVALID", "invalid OTA packet"),
            0x03 => new(code, "OTA_DATA_CRC_ERR", "OTA packet CRC error"),
            0x04 => new(code, "OTA_WRITE_FLASH_ERR", "flash write error"),
            0x05 => new(code, "OTA_DATA_INCOMPLETE", "firmware data incomplete"),
            0x06 => new(code, "OTA_FLOW_ERR", "OTA command/data flow error"),
            0x07 => new(code, "OTA_FW_CHECK_ERR", "whole firmware validation failed"),
            0x08 => new(code, "OTA_VERSION_COMPARE_ERR", "firmware version rejected"),
            0x09 => new(code, "OTA_PDU_LEN_ERR", "PDU length rejected"),
            0x0A => new(code, "OTA_FIRMWARE_MARK_ERR", "firmware mark invalid"),
            0x0B => new(code, "OTA_FW_SIZE_ERR", "firmware size invalid"),
            0x0C => new(code, "OTA_DATA_PACKET_TIMEOUT", "packet interval timeout"),
            0x0D => new(code, "OTA_TIMEOUT", "overall OTA timeout"),
            0x0E => new(code, "OTA_FAIL_DUE_TO_CONNECTION_TERMINATE", "connection terminated"),
            0x0F => new(code, "OTA_MCU_NOT_SUPPORTED", "OTA mode not supported by MCU/firmware"),
            0x10 => new(code, "OTA_LOGIC_ERR", "OTA logic error"),
            _ => new(code, $"OTA_RESULT_0x{code:X2}", "unknown/reserved OTA result"),
        };
        return true;
    }
}
