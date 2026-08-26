package com.telink.bmsassistant

import com.telink.bmsassistant.ota.TelinkOtaCodec
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test

class TelinkOtaCodecTest {
    @Test
    fun startPacketMatchesVector() {
        assertArrayEquals(hex("01 FF"), TelinkOtaCodec.startPacket())
    }

    @Test
    fun dataPacketMatchesVector() {
        val data = hex("26 80 00 00 00 00 5D 02 4B 4E 4C 54 30 04 88 00")
        val expected = hex("00 00 26 80 00 00 00 00 5D 02 4B 4E 4C 54 30 04 88 00 1C A3")
        assertArrayEquals(expected, TelinkOtaCodec.dataPacket(0, data))
    }

    @Test
    fun endPacketMatchesVector() {
        assertArrayEquals(hex("02 FF FA 14 05 EB"), TelinkOtaCodec.endPacket(0x14FA))
    }

    @Test
    fun resultPacketMatchesVector() {
        assertEquals(0, TelinkOtaCodec.parseResult(hex("06 FF 00")))
        assertEquals(3, TelinkOtaCodec.parseResult(hex("06 FF 03")))
    }

    private fun hex(text: String): ByteArray = text
        .replace(" ", "")
        .chunked(2)
        .map { it.toInt(16).toByte() }
        .toByteArray()
}
