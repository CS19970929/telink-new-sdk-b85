package com.telink.bmsassistant.ota

object TelinkOtaCodec {
    const val OTA_SERVICE_UUID = "00010203-0405-0607-0809-0A0B0C0D1912"
    const val OTA_CHARACTERISTIC_UUID = "00010203-0405-0607-0809-0A0B0C0D2B12"
    const val OTA_PDU_BYTES = 16
    const val OTA_PACKET_BYTES = 20
    private const val CMD_START = 0xFF01
    private const val CMD_END = 0xFF02
    private const val CMD_RESULT = 0xFF06

    data class FirmwareImage(
        val source: ByteArray,
        val firmware: ByteArray,
        val declaredSize: Int,
        val packetCount: Int,
        val maxIndex: Int,
    )

    fun parseFirmware(raw: ByteArray): FirmwareImage {
        require(raw.size >= 0x1C) { "firmware is too small to contain Telink header" }
        val mark = byteArrayOf('K'.code.toByte(), 'N'.code.toByte(), 'L'.code.toByte(), 'T'.code.toByte())
        require(raw.copyOfRange(0x08, 0x0C).contentEquals(mark)) { "firmware mark at 0x08 is not KNLT" }
        val declaredSize = u32le(raw, 0x18)
        require(declaredSize > 0 && declaredSize <= raw.size) {
            "invalid firmware size field: declared=$declaredSize file=${raw.size}"
        }
        val packetCount = (declaredSize + OTA_PDU_BYTES - 1) / OTA_PDU_BYTES
        require(packetCount in 1..0x10000) { "unsupported OTA packet count: $packetCount" }
        return FirmwareImage(
            source = raw,
            firmware = raw.copyOfRange(0, declaredSize),
            declaredSize = declaredSize,
            packetCount = packetCount,
            maxIndex = packetCount - 1,
        )
    }

    fun startPacket(): ByteArray = u16le(CMD_START)

    fun dataPacket(index: Int, payload: ByteArray): ByteArray {
        require(index in 0..0xFFFF) { "OTA index out of range: $index" }
        require(payload.size <= OTA_PDU_BYTES) { "OTA payload must be <= 16 bytes" }
        val body = ByteArray(18) { 0xFF.toByte() }
        val indexBytes = u16le(index)
        body[0] = indexBytes[0]
        body[1] = indexBytes[1]
        payload.copyInto(body, destinationOffset = 2)
        val crc = crc16(body)
        return body + u16le(crc)
    }

    fun dataPacket(image: FirmwareImage, index: Int): ByteArray {
        require(index in 0 until image.packetCount) { "OTA packet index out of image: $index" }
        val start = index * OTA_PDU_BYTES
        val end = minOf(start + OTA_PDU_BYTES, image.firmware.size)
        return dataPacket(index, image.firmware.copyOfRange(start, end))
    }

    fun endPacket(maxIndex: Int): ByteArray {
        require(maxIndex in 0..0xFFFF) { "OTA max index out of range: $maxIndex" }
        return u16le(CMD_END) + u16le(maxIndex) + u16le(maxIndex xor 0xFFFF)
    }

    fun parseResult(packet: ByteArray): Int? {
        if (packet.size < 3 || u16le(packet, 0) != CMD_RESULT) return null
        return packet[2].toUByte().toInt()
    }

    fun resultText(code: Int): String = when (code) {
        0x00 -> "OTA_SUCCESS"
        0x01 -> "OTA_DATA_PACKET_SEQ_ERR"
        0x02 -> "OTA_PACKET_INVALID"
        0x03 -> "OTA_DATA_CRC_ERR"
        0x04 -> "OTA_WRITE_FLASH_ERR"
        0x05 -> "OTA_DATA_INCOMPLETE"
        0x06 -> "OTA_FLOW_ERR"
        0x07 -> "OTA_FW_CHECK_ERR"
        0x08 -> "OTA_VERSION_COMPARE_ERR"
        0x09 -> "OTA_PDU_LEN_ERR"
        0x0A -> "OTA_FIRMWARE_MARK_ERR"
        0x0B -> "OTA_FW_SIZE_ERR"
        0x0C -> "OTA_DATA_PACKET_TIMEOUT"
        0x0D -> "OTA_TIMEOUT"
        0x0E -> "OTA_FAIL_DUE_TO_CONNECTION_TERMINATE"
        0x0F -> "OTA_MCU_NOT_SUPPORTED"
        0x10 -> "OTA_LOGIC_ERR"
        else -> "OTA_UNKNOWN_RESULT_0x${code.toString(16).uppercase().padStart(2, '0')}"
    }

    fun crc16(data: ByteArray): Int {
        var crc = 0xFFFF
        for (byte in data) {
            crc = crc xor byte.toUByte().toInt()
            repeat(8) {
                crc = if ((crc and 1) != 0) (crc shr 1) xor 0xA001 else crc shr 1
            }
        }
        return crc and 0xFFFF
    }

    private fun u16le(value: Int): ByteArray = byteArrayOf(
        (value and 0xFF).toByte(),
        ((value shr 8) and 0xFF).toByte(),
    )

    private fun u16le(data: ByteArray, offset: Int): Int =
        data[offset].toUByte().toInt() or (data[offset + 1].toUByte().toInt() shl 8)

    private fun u32le(data: ByteArray, offset: Int): Int =
        data[offset].toUByte().toInt() or
            (data[offset + 1].toUByte().toInt() shl 8) or
            (data[offset + 2].toUByte().toInt() shl 16) or
            (data[offset + 3].toUByte().toInt() shl 24)
}
