package com.example.bmsota.ota

import com.example.bmsota.ble.BleOtaSession
import java.io.IOException
import java.util.concurrent.atomic.AtomicBoolean

class TelinkOtaController(
    private val session: BleOtaSession,
    private val log: (String) -> Unit,
    private val progress: (completedPackets: Int, totalPackets: Int, imageBytes: Int, bytesPerSecond: Double) -> Unit
) {
    private val cancelled = AtomicBoolean(false)
    @Volatile private var otaResult: OtaResult? = null

    fun cancel() {
        cancelled.set(true)
    }

    fun upgrade(image: FirmwareImage, packetDelayMs: Int = 0) {
        require(packetDelayMs in 0..1000)
        cancelled.set(false)
        otaResult = null
        session.notificationListener = { data -> onNotification(data) }

        try {
            checkCancelled()
            log("Mode=LegacyFast; MTU=${session.negotiatedMtu}; notifications=${session.notificationsEnabled}; payload=16 bytes; packetDelay=$packetDelayMs ms")
            log("TX OTA START: 01 FF")
            session.write(LegacyPacketBuilder.buildStart())
            delay(packetDelayMs)

            val startedNs = System.nanoTime()
            var lastProgressNs = 0L

            for (i in 0 until image.packetCount) {
                checkCancelled()
                throwIfOtaRejected()

                val packet = LegacyPacketBuilder.buildData(i, image.bytes, i * FirmwareImage.PAYLOAD_SIZE)
                session.write(packet)

                val sent = minOf((i + 1) * FirmwareImage.PAYLOAD_SIZE, image.imageSize)
                val now = System.nanoTime()
                if (i == image.packetCount - 1 || lastProgressNs == 0L || now - lastProgressNs >= 100_000_000L) {
                    lastProgressNs = now
                    val elapsedSec = (now - startedNs).coerceAtLeast(1L) / 1_000_000_000.0
                    progress(i + 1, image.packetCount, sent, sent / elapsedSec)
                }

                if (i == 0 || i == image.packetCount - 1 || (i + 1) % 256 == 0) {
                    log("TX DATA index=$i imageBytes=$sent/${image.imageSize}")
                }

                if (packetDelayMs > 0) delay(packetDelayMs)
                else if ((i and 0x3F) == 0x3F) Thread.yield()
            }

            checkCancelled()
            throwIfOtaRejected()
            val lastIndex = image.packetCount - 1
            val end = LegacyPacketBuilder.buildEnd(lastIndex)
            log("TX OTA END: lastIndex=$lastIndex bytes=${end.toHex()}")
            session.write(end)

            if (session.notificationsEnabled) {
                val deadline = System.nanoTime() + 2_000_000_000L
                while (otaResult == null && System.nanoTime() < deadline) {
                    checkCancelled()
                    Thread.sleep(20)
                }
                val result = otaResult
                if (result != null) {
                    if (result.code != 0) throw IOException("OTA_END rejected: 0x${result.code.toString(16).padStart(2, '0')} ${result.name}")
                    log("RX OTA_RESULT: 0x00 OTA_SUCCESS")
                } else {
                    log("OTA_RESULT timeout; data transfer completed but final server result is unconfirmed.")
                }
            } else {
                log("OTA notifications unavailable; transfer completed without OTA_RESULT confirmation.")
            }

            log("OTA data transmission complete; device is expected to validate/switch image and reboot.")
        } finally {
            session.notificationListener = null
        }
    }

    private fun onNotification(data: ByteArray) {
        if (data.size < 3 || data[0] != 0x06.toByte() || data[1] != 0xFF.toByte()) return
        val code = data[2].toInt() and 0xFF
        val result = OtaResult(code, resultName(code))
        otaResult = result
        log("RX OTA_RESULT: 0x${code.toString(16).padStart(2, '0').uppercase()} ${result.name}")
    }

    private fun throwIfOtaRejected() {
        val result = otaResult ?: return
        if (result.code != 0) throw IOException("OTA rejected: 0x${result.code.toString(16).padStart(2, '0')} ${result.name}")
    }

    private fun resultName(code: Int): String = when (code) {
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
        0x0E -> "OTA_CONNECTION_TERMINATE"
        0x0F -> "OTA_MCU_NOT_SUPPORTED"
        0x10 -> "OTA_LOGIC_ERR"
        else -> "OTA_RESULT_0x${code.toString(16).padStart(2, '0').uppercase()}"
    }

    private fun checkCancelled() {
        if (cancelled.get()) throw InterruptedException("OTA cancelled. Reconnect and restart from index 0.")
    }

    private fun delay(ms: Int) {
        if (ms > 0) Thread.sleep(ms.toLong())
    }

    private data class OtaResult(val code: Int, val name: String)
    private fun ByteArray.toHex(): String = joinToString("") { "%02X".format(it.toInt() and 0xFF) }
}
