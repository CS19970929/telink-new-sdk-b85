package com.telink.bmsassistant.protocol

import java.nio.charset.StandardCharsets

class ModbusCodecError(message: String) : IllegalArgumentException(message)

data class ParsedResponse(
    val kind: String,
    val words: List<Int> = emptyList(),
    val register: Int? = null,
    val value: Int? = null,
    val quantity: Int? = null,
    val function: Int? = null,
    val code: Int? = null,
    val raw: ByteArray,
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is ParsedResponse) return false
        return kind == other.kind &&
            words == other.words &&
            register == other.register &&
            value == other.value &&
            quantity == other.quantity &&
            function == other.function &&
            code == other.code &&
            raw.contentEquals(other.raw)
    }

    override fun hashCode(): Int {
        var result = kind.hashCode()
        result = 31 * result + words.hashCode()
        result = 31 * result + (register ?: 0)
        result = 31 * result + (value ?: 0)
        result = 31 * result + (quantity ?: 0)
        result = 31 * result + (function ?: 0)
        result = 31 * result + (code ?: 0)
        result = 31 * result + raw.contentHashCode()
        return result
    }
}

object BmsModbusCodec {
    fun parseAddress(text: String): Int {
        val cleaned = text.trim()
        val value = if (cleaned.startsWith("0x", ignoreCase = true)) {
            cleaned.drop(2).toIntOrNull(16)
                ?: throw ModbusCodecError("无法解析十六进制输入: $text")
        } else {
            cleaned.toIntOrNull(10)
                ?: throw ModbusCodecError("无法解析十六进制输入: $text")
        }
        if (value !in 0..0xFFFF) {
            throw ModbusCodecError("输入超出 16 bit 范围: $text")
        }
        return value
    }

    fun parseWords(text: String): List<Int> {
        val parts = text.replace(",", " ").split(Regex("\\s+")).filter { it.isNotBlank() }
        if (parts.isEmpty()) throw ModbusCodecError("无法解析十六进制输入: $text")
        return parts.map { parseAddress(it) }
    }

    fun parseRawBytes(text: String): ByteArray {
        val cleaned = text
            .replace("0x", "", ignoreCase = true)
            .filter { it in '0'..'9' || it in 'a'..'f' || it in 'A'..'F' }
        if (cleaned.isBlank() || cleaned.length % 2 != 0) {
            throw ModbusCodecError("无法解析十六进制输入: $text")
        }
        return cleaned.chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    }

    fun readHolding(start: Int, quantity: Int): ByteArray {
        return frame(byteArrayOf(0x01, 0x03) + u16be(start) + u16be(quantity))
    }

    fun writeSingle(register: Int, value: Int): ByteArray {
        return frame(byteArrayOf(0x01, 0x06) + u16be(register) + u16be(value))
    }

    fun writeMultiple(register: Int, values: List<Int>): ByteArray {
        val payload = values.flatMap { u16be(it).asIterable() }.toByteArray()
        val body = byteArrayOf(0x01, 0x10) +
            u16be(register) +
            u16be(values.size) +
            byteArrayOf(payload.size.toByte()) +
            payload
        return frame(body)
    }

    fun echo(payload: ByteArray): ByteArray {
        return frame(byteArrayOf(0x01, 0x7F) + payload)
    }

    fun ensureSafeBleLength(request: ByteArray) {
        if (request.size > BMSGeneratedBLEConstraints.SAFE_SINGLE_REQUEST_PAYLOAD_BYTES) {
            throw ModbusCodecError(
                "请求长度 ${request.size} byte，超过当前固件 BLE 单包安全上限 " +
                    "${BMSGeneratedBLEConstraints.SAFE_SINGLE_REQUEST_PAYLOAD_BYTES} byte",
            )
        }
    }

    fun parseResponse(data: ByteArray): ParsedResponse {
        if (data.size < 4) throw ModbusCodecError("Modbus 帧无效: 长度小于最小 Modbus RTU 帧")
        if (!validateCrc(data)) throw ModbusCodecError("收到的响应 CRC 校验失败")

        val function = data[1].toUByte().toInt()
        if (function == 0x7F) {
            return ParsedResponse(kind = "echo", raw = data)
        }
        if ((function and 0x80) != 0) {
            if (data.size < 5) throw ModbusCodecError("Modbus 帧无效: 异常响应长度不足")
            return ParsedResponse(
                kind = "exception",
                function = function and 0x7F,
                code = data[2].toUByte().toInt(),
                raw = data,
            )
        }
        if (function == 0x03) {
            if (data.size < 5) throw ModbusCodecError("Modbus 帧无效: 读寄存器响应长度不足")
            val byteCount = data[2].toUByte().toInt()
            if (data.size != byteCount + 5) {
                throw ModbusCodecError("Modbus 帧无效: 读寄存器响应字节数不匹配")
            }
            val words = (3 until 3 + byteCount step 2).map { index ->
                u16FromBe(data, index)
            }
            return ParsedResponse(kind = "read_holding", words = words, raw = data)
        }
        if (function == 0x06) {
            if (data.size != 8) throw ModbusCodecError("Modbus 帧无效: 写单寄存器响应长度应为 8")
            return ParsedResponse(
                kind = "write_single_ack",
                register = u16FromBe(data, 2),
                value = u16FromBe(data, 4),
                raw = data,
            )
        }
        if (function == 0x10) {
            if (data.size != 8) throw ModbusCodecError("Modbus 帧无效: 写多寄存器响应长度应为 8")
            return ParsedResponse(
                kind = "write_multiple_ack",
                register = u16FromBe(data, 2),
                quantity = u16FromBe(data, 4),
                raw = data,
            )
        }
        throw ModbusCodecError("响应内容不符合预期: 未支持的功能码 0x${function.toString(16).uppercase().padStart(2, '0')}")
    }

    fun inferExpectedLength(buffer: ByteArray, hint: Int?): Int? {
        if (hint != null) return hint
        if (buffer.size < 2) return null
        val function = buffer[1].toUByte().toInt()
        return when {
            function == 0x7F -> null
            (function and 0x80) != 0 -> 5
            function == 0x03 && buffer.size >= 3 -> buffer[2].toUByte().toInt() + 5
            function in setOf(0x06, 0x10) -> 8
            else -> null
        }
    }

    fun validateCrc(data: ByteArray): Boolean {
        if (data.size < 4) return false
        val payload = data.copyOf(data.size - 2)
        val crc = crc16(payload)
        return data[data.size - 2].toUByte().toInt() == (crc and 0xFF) &&
            data[data.size - 1].toUByte().toInt() == ((crc shr 8) and 0xFF)
    }

    fun asciiStringFromWords(words: List<Int>): String {
        val bytes = words.flatMap { u16be(it).asIterable() }.toByteArray()
        val end = bytes.indexOf(0).let { if (it >= 0) it else bytes.size }
        return String(bytes.copyOf(end), StandardCharsets.UTF_8)
    }

    fun macStringFromWords(words: List<Int>): String {
        val bytes = words.flatMap { u16be(it).asIterable() }.take(6)
        return bytes.joinToString(":") { it.toUByte().toString(16).uppercase().padStart(2, '0') }
    }

    fun encodeAsciiWords(text: String): List<Int> {
        val raw = text.toByteArray(StandardCharsets.UTF_8)
        val padded = if (raw.size % 2 == 0) raw else raw + byteArrayOf(0)
        return padded.toList().chunked(2).map { pair ->
            ((pair[0].toUByte().toInt() shl 8) or pair[1].toUByte().toInt()) and 0xFFFF
        }
    }

    fun frame(body: ByteArray): ByteArray {
        val crc = crc16(body)
        return body + byteArrayOf((crc and 0xFF).toByte(), ((crc shr 8) and 0xFF).toByte())
    }

    fun crc16(data: ByteArray): Int {
        var crc = 0xFFFF
        for (byte in data) {
            crc = crc xor byte.toUByte().toInt()
            repeat(8) {
                crc = if ((crc and 0x0001) != 0) {
                    (crc shr 1) xor 0xA001
                } else {
                    crc shr 1
                }
            }
        }
        return crc and 0xFFFF
    }

    fun spacedHex(data: ByteArray): String {
        return data.joinToString(" ") { it.toUByte().toString(16).uppercase().padStart(2, '0') }
    }

    fun hexToBytes(text: String): ByteArray {
        return text.replace(" ", "").chunked(2).map { it.toInt(16).toByte() }.toByteArray()
    }

    private fun u16be(value: Int): ByteArray {
        val cleaned = value and 0xFFFF
        return byteArrayOf(((cleaned shr 8) and 0xFF).toByte(), (cleaned and 0xFF).toByte())
    }

    private fun u16FromBe(data: ByteArray, index: Int): Int {
        return ((data[index].toUByte().toInt() shl 8) or data[index + 1].toUByte().toInt()) and 0xFFFF
    }
}
