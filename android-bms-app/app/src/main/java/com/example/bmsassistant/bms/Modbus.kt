package com.example.bmsassistant.bms

import java.io.IOException
import java.nio.charset.StandardCharsets

object BmsRegisters {
    const val DEVICE = 0x01
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
}

object Modbus {
    fun readHolding(start: Int, qty: Int): ByteArray {
        require(qty in 1..125)
        return frame(byteArrayOf(BmsRegisters.DEVICE.toByte(), 0x03, (start shr 8).toByte(), start.toByte(), (qty shr 8).toByte(), qty.toByte()))
    }
    fun writeSingle(reg: Int, value: Int): ByteArray = frame(byteArrayOf(BmsRegisters.DEVICE.toByte(), 0x06, (reg shr 8).toByte(), reg.toByte(), (value shr 8).toByte(), value.toByte()))
    fun writeMultiple(start: Int, words: IntArray): ByteArray {
        require(words.isNotEmpty() && words.size <= 123)
        val body = ByteArray(7 + words.size * 2)
        body[0] = BmsRegisters.DEVICE.toByte(); body[1] = 0x10
        body[2] = (start shr 8).toByte(); body[3] = start.toByte(); body[4] = (words.size shr 8).toByte(); body[5] = words.size.toByte(); body[6] = (words.size * 2).toByte()
        words.forEachIndexed { i, v -> body[7 + i * 2] = (v shr 8).toByte(); body[8 + i * 2] = v.toByte() }
        return frame(body)
    }
    fun frame(body: ByteArray): ByteArray { val out = ByteArray(body.size + 2); body.copyInto(out); val c = crc16(body); out[out.lastIndex - 1] = c.toByte(); out[out.lastIndex] = (c shr 8).toByte(); return out }
    fun crc16(data: ByteArray, length: Int = data.size): Int { var crc = 0xFFFF; for (i in 0 until length) { crc = crc xor (data[i].toInt() and 0xFF); repeat(8) { crc = if ((crc and 1) != 0) (crc ushr 1) xor 0xA001 else crc ushr 1 } }; return crc and 0xFFFF }
    fun inferLength(buffer: List<Byte>): Int? { if (buffer.size < 2) return null; val f = buffer[1].toInt() and 0xFF; return when { (f and 0x80) != 0 -> 5; f == 0x03 -> if (buffer.size >= 3) (buffer[2].toInt() and 0xFF) + 5 else null; f == 0x06 || f == 0x10 -> 8; else -> null } }
    fun validate(frame: ByteArray) { if (frame.size < 4) throw IOException("Modbus响应过短"); val rx = (frame[frame.size - 2].toInt() and 0xFF) or ((frame.last().toInt() and 0xFF) shl 8); val calc = crc16(frame, frame.size - 2); if (rx != calc) throw IOException("Modbus CRC错误 rx=%04X calc=%04X".format(rx, calc)); if ((frame[1].toInt() and 0x80) != 0) throw IOException("Modbus异常 code=0x%02X".format(frame[2].toInt() and 0xFF)) }
    fun parseRead(frame: ByteArray, qty: Int): IntArray { validate(frame); if ((frame[0].toInt() and 0xFF) != BmsRegisters.DEVICE || (frame[1].toInt() and 0xFF) != 3) throw IOException("非预期Modbus读响应"); if ((frame[2].toInt() and 0xFF) != qty * 2) throw IOException("Modbus读长度不匹配"); return IntArray(qty) { i -> ((frame[3 + i * 2].toInt() and 0xFF) shl 8) or (frame[4 + i * 2].toInt() and 0xFF) } }
    fun validateWriteSingle(frame: ByteArray, reg: Int, value: Int) { validate(frame); if (frame.size != 8 || (frame[1].toInt() and 0xFF) != 6) throw IOException("0x06 ACK无效"); if (u16(frame, 2) != reg || u16(frame, 4) != value) throw IOException("0x06 ACK不匹配") }
    fun validateWriteMultiple(frame: ByteArray, reg: Int, qty: Int) { validate(frame); if (frame.size != 8 || (frame[1].toInt() and 0xFF) != 0x10) throw IOException("0x10 ACK无效"); if (u16(frame, 2) != reg || u16(frame, 4) != qty) throw IOException("0x10 ACK不匹配") }
    fun decodeAscii(words: IntArray): String { val bytes = ArrayList<Byte>(); for (w in words) { bytes += (w shr 8).toByte(); bytes += w.toByte() }; val arr = bytes.takeWhile { it.toInt() != 0 }.toByteArray(); return String(arr, StandardCharsets.UTF_8).trim() }
    private fun u16(a: ByteArray, p: Int) = ((a[p].toInt() and 0xFF) shl 8) or (a[p + 1].toInt() and 0xFF)
    fun hex(a: ByteArray) = a.joinToString("") { "%02X".format(it.toInt() and 0xFF) }
}
