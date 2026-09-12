package com.telink.bmsassistant.protocol

import com.telink.bmsassistant.domain.BatteryDataSource
import com.telink.bmsassistant.domain.decodeBatteryStatus
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.LocalDateTime

class BmsProtocolTest {
    @Test
    fun requestVectorsMatchSharedAssets() {
        assertEquals(
            "01 03 D0 00 00 3F 3D 1A",
            BmsModbusCodec.spacedHex(
                BmsModbusCodec.readHolding(
                    BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_START,
                    BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_WORD_COUNT,
                ),
            ),
        )
        assertEquals(
            "01 03 D1 15 00 02 EC F3",
            BmsModbusCodec.spacedHex(
                BmsModbusCodec.readHolding(
                    BMSGeneratedRegisterCatalog.SYSTEM_STATUS_START,
                    BMSGeneratedRegisterCatalog.SYSTEM_STATUS_WORD_COUNT,
                ),
            ),
        )
        assertEquals(
            "01 03 D1 20 00 0B 3C FB",
            BmsModbusCodec.spacedHex(
                BmsModbusCodec.readHolding(
                    BMSGeneratedRegisterCatalog.REALTIME_STATUS_START,
                    BMSGeneratedRegisterCatalog.REALTIME_STATUS_WORD_COUNT,
                ),
            ),
        )
        assertEquals(
            "01 06 10 05 00 3C 9D 1A",
            BmsModbusCodec.spacedHex(BmsModbusCodec.writeSingle(BMSGeneratedRegisterCatalog.SOC_WRITE_REGISTER_START, 60)),
        )
        assertEquals(
            "01 10 01 00 00 04 08 42 54 5F 44 45 4D 4F 00 69 5C",
            BmsModbusCodec.spacedHex(
                BmsModbusCodec.writeMultiple(
                    BMSGeneratedRegisterCatalog.BT_NAME_START,
                    BmsModbusCodec.encodeAsciiWords("BT_DEMO"),
                ),
            ),
        )
    }

    @Test
    fun responseVectorsParse() {
        val status = BmsModbusCodec.parseResponse(hex("01 03 04 00 03 00 01 CB F3"))
        assertEquals("read_holding", status.kind)
        assertEquals(listOf(0x0003, 0x0001), status.words)

        val realtime = BmsModbusCodec.parseResponse(
            hex("01 03 16 42 53 00 01 0E 42 FF 83 00 4E 02 C6 02 6C 02 BC 0C F8 0C DA 00 1E 75 F9"),
        )
        assertEquals("read_holding", realtime.kind)
        assertEquals(11, realtime.words.size)
        assertEquals(0x4253, realtime.words[0])

        val writeSoc = BmsModbusCodec.parseResponse(hex("01 06 10 05 00 3C 9D 1A"))
        assertEquals("write_single_ack", writeSoc.kind)
        assertEquals(0x1005, writeSoc.register)
        assertEquals(0x003C, writeSoc.value)
    }

    @Test
    fun invalidCrcIsRejected() {
        assertFalse(BmsModbusCodec.validateCrc(hex("01 03 04 00 03 00 01 00 00")))
        try {
            BmsModbusCodec.parseResponse(hex("01 03 04 00 03 00 01 00 00"))
            throw AssertionError("expected CRC error")
        } catch (expected: ModbusCodecError) {
            assertTrue(expected.message!!.contains("CRC"))
        }
    }

    @Test
    fun u16InputsDoNotWrap() {
        try {
            BmsModbusCodec.parseAddress("0x10000")
            throw AssertionError("expected range error")
        } catch (expected: ModbusCodecError) {
            assertTrue(expected.message!!.contains("16 bit"))
        }
    }

    @Test
    fun notifyFragmentsAreReassembled() {
        val accumulator = ResponseAccumulator()
        val first = accumulator.append(hex("01 03 16 42 53 00 01 0E 42 FF 83 00 4E 02 C6 02 6C 02 BC 0C"))
        assertEquals("waiting", first.state)
        assertEquals(27, first.expectedLength)

        val second = accumulator.append(hex("F8 0C DA 00 1E 75 F9"))
        assertEquals("complete", second.state)
        assertEquals(
            "01 03 16 42 53 00 01 0E 42 FF 83 00 4E 02 C6 02 6C 02 BC 0C F8 0C DA 00 1E 75 F9",
            BmsModbusCodec.spacedHex(second.frame),
        )
    }

    @Test
    fun batteryRealtimeWindowWinsOverLegacyWords() {
        val legacy = MutableList(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_WORD_COUNT) { 0 }
        legacy[0] = 3300
        legacy[1] = 3310
        legacy[BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_SOC_RAW_INDEX] = 50
        legacy[BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_PACK_VOLTAGE_ENGINEERING_RAW_INDEX] = 3300
        legacy[BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_CHARGE_CURRENT_RAW_INDEX] = 10
        legacy[BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_SOH_RAW_INDEX] = 99

        val realtime = listOf(
            0x4253,
            1,
            0x0E42,
            0xFF83,
            78,
            0x02C6,
            0x026C,
            0x02BC,
            0x0CF8,
            0x0CDA,
            30,
        )
        val snapshot = decodeBatteryStatus(
            realtimeWords = realtime,
            legacyCellWords = legacy,
            systemStatusWords = listOf(0x0003, 0x0001),
            updatedAt = LocalDateTime.now(),
        )

        assertEquals(BatteryDataSource.RealtimeWindow, snapshot.source)
        assertEquals(3650, snapshot.packVoltageRaw)
        assertEquals(-125, snapshot.signedCurrentRaw)
        assertEquals(78, snapshot.socRaw)
        assertEquals(10, snapshot.cellVoltages.size)
        assertEquals(0x00010003, snapshot.systemStatusRaw)
    }

    private fun hex(text: String): ByteArray = BmsModbusCodec.hexToBytes(text)
}
