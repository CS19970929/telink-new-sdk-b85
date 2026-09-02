package com.cjsh.bmsassistant.protocol

import java.io.IOException
import java.nio.charset.StandardCharsets

object BmsRegisters {
    const val DEVICE_ADDRESS = 0x01
    const val MAC = 0x0000
    const val BT_NAME = 0x0100
    const val SERIAL = 0xC002
    const val HARDWARE = 0xC012
    const val SOFTWARE = 0xC022
    const val EVENT_LOG = 0xC008
    const val PROTECT = 0x2100
    const val PROTECT_COUNT = 65
    const val AFE = 0x2400
    const val AFE_COUNT = 24
    const val LEGACY = 0xD000
    const val SYSTEM_STATUS = 0xD115
    const val REALTIME = 0xD120
    const val REALTIME_MAGIC = 0x4253
    const val BT_NAME_READ_WORDS = 12
    const val BT_NAME_MAX_SUFFIX_BYTES = 10
}

object ModbusRtu {
    fun readHolding(start: Int, quantity: Int): ByteArray {
        require(quantity in 1..125)
        return frame(byteArrayOf(
            BmsRegisters.DEVICE_ADDRESS.toByte(), 0x03,
            (start ushr 8).toByte(), start.toByte(),
            (quantity ushr 8).toByte(), quantity.toByte()
        ))
    }

    fun writeSingle(register: Int, value: Int): ByteArray = frame(byteArrayOf(
        BmsRegisters.DEVICE_ADDRESS.toByte(), 0x06,
        (register ushr 8).toByte(), register.toByte(),
        (value ushr 8).toByte(), value.toByte()
    ))

    fun writeMultiple(start: Int, values: IntArray): ByteArray {
        require(values.isNotEmpty())
        val raw = ByteArray(values.size * 2)
        values.forEachIndexed { i, v ->
            require(v in 0..0xFFFF)
            raw[i * 2] = (v ushr 8).toByte()
            raw[i * 2 + 1] = v.toByte()
        }
        return writeMultipleRaw(start, raw)
    }

    fun writeMultipleRaw(start: Int, raw: ByteArray): ByteArray {
        require(raw.isNotEmpty() && raw.size % 2 == 0)
        val qty = raw.size / 2
        val body = ByteArray(7 + raw.size)
        body[0] = BmsRegisters.DEVICE_ADDRESS.toByte()
        body[1] = 0x10
        body[2] = (start ushr 8).toByte(); body[3] = start.toByte()
        body[4] = (qty ushr 8).toByte(); body[5] = qty.toByte()
        body[6] = raw.size.toByte()
        raw.copyInto(body, 7)
        return frame(body)
    }

    fun parseRead(frame: ByteArray, expectedQuantity: Int): IntArray {
        validateFrame(frame)
        val function = frame[1].toInt() and 0xFF
        if ((function and 0x80) != 0) throw IOException("Modbus exception code=0x%02X".format(frame[2].toInt() and 0xFF))
        if ((frame[0].toInt() and 0xFF) != BmsRegisters.DEVICE_ADDRESS || function != 0x03) throw IOException("Unexpected Modbus read response")
        val bytes = frame[2].toInt() and 0xFF
        if (bytes != expectedQuantity * 2 || frame.size != bytes + 5) throw IOException("Modbus read response length mismatch")
        return IntArray(expectedQuantity) { i ->
            ((frame[3 + i * 2].toInt() and 0xFF) shl 8) or (frame[4 + i * 2].toInt() and 0xFF)
        }
    }

    fun validateWriteSingleAck(frame: ByteArray, register: Int, value: Int) {
        validateFrame(frame)
        val f = frame[1].toInt() and 0xFF
        if ((f and 0x80) != 0) throw IOException("Modbus exception code=0x%02X".format(frame[2].toInt() and 0xFF))
        if (frame.size != 8 || f != 0x06 || u16(frame, 2) != register || u16(frame, 4) != value) throw IOException("Single-register write acknowledgement mismatch")
    }

    fun validateWriteMultipleAck(frame: ByteArray, register: Int, quantity: Int) {
        validateFrame(frame)
        val f = frame[1].toInt() and 0xFF
        if ((f and 0x80) != 0) throw IOException("Modbus exception code=0x%02X".format(frame[2].toInt() and 0xFF))
        if (frame.size != 8 || f != 0x10 || u16(frame, 2) != register || u16(frame, 4) != quantity) throw IOException("Multi-register write acknowledgement mismatch")
    }

    fun inferExpectedLength(buffer: List<Byte>): Int? {
        if (buffer.size < 2) return null
        val f = buffer[1].toInt() and 0xFF
        if ((f and 0x80) != 0) return 5
        return when (f) {
            0x03 -> if (buffer.size >= 3) (buffer[2].toInt() and 0xFF) + 5 else null
            0x06, 0x10 -> 8
            else -> null
        }
    }

    fun validateFrame(frame: ByteArray) {
        if (frame.size < 4) throw IOException("Modbus response too short")
        val rx = (frame[frame.size - 2].toInt() and 0xFF) or ((frame.last().toInt() and 0xFF) shl 8)
        val calc = crc16(frame, 0, frame.size - 2)
        if (rx != calc) throw IOException("Modbus CRC mismatch: rx=0x%04X calc=0x%04X".format(rx, calc))
    }

    fun frame(body: ByteArray): ByteArray {
        val out = body.copyOf(body.size + 2)
        val crc = crc16(body, 0, body.size)
        out[out.size - 2] = crc.toByte()
        out[out.size - 1] = (crc ushr 8).toByte()
        return out
    }

    fun crc16(data: ByteArray, offset: Int = 0, length: Int = data.size - offset): Int {
        var crc = 0xFFFF
        for (i in offset until offset + length) {
            crc = crc xor (data[i].toInt() and 0xFF)
            repeat(8) { crc = if ((crc and 1) != 0) (crc ushr 1) xor 0xA001 else crc ushr 1 }
        }
        return crc and 0xFFFF
    }

    fun decodeAscii(words: IntArray): String {
        val bytes = ArrayList<Byte>(words.size * 2)
        words.forEach { w -> bytes.add((w ushr 8).toByte()); bytes.add(w.toByte()) }
        val raw = bytes.takeWhile { it.toInt() != 0 }.toByteArray()
        return String(raw, StandardCharsets.UTF_8).trim()
    }

    fun hex(data: ByteArray): String = data.joinToString("") { "%02X".format(it.toInt() and 0xFF) }
    private fun u16(data: ByteArray, off: Int) = ((data[off].toInt() and 0xFF) shl 8) or (data[off + 1].toInt() and 0xFF)
}
