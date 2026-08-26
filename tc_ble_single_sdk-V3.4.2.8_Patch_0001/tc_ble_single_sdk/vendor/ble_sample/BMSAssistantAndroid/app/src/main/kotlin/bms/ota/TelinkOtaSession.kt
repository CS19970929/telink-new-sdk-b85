package com.telink.bmsassistant.ota

class TelinkOtaSession(private val client: TelinkOtaBleClient) {
    var onProgress: (Int, String) -> Unit = { _, _ -> }
    var onFinished: (Boolean, String) -> Unit = { _, _ -> }

    private var image: TelinkOtaCodec.FirmwareImage? = null
    private var phase = Phase.Idle
    private var dataIndex = 0
    private var active = false

    private enum class Phase { Idle, Start, Data, End, WaitingResult, Finished }

    init {
        client.onWriteComplete = ::onWriteComplete
        client.onNotification = ::onNotification
        val previousError = client.onError
        client.onError = { message ->
            previousError(message)
            fail(message)
        }
    }

    fun start(image: TelinkOtaCodec.FirmwareImage) {
        check(!active) { "OTA session is already active" }
        this.image = image
        phase = Phase.Start
        dataIndex = 0
        active = true
        onProgress(0, "sending OTA START")
        client.write(TelinkOtaCodec.startPacket())
    }

    fun cancel(reason: String = "OTA cancelled") {
        if (!active) return
        fail(reason)
    }

    private fun onWriteComplete() {
        if (!active) return
        val currentImage = image ?: return fail("firmware image lost")
        when (phase) {
            Phase.Start -> {
                phase = Phase.Data
                sendData(currentImage)
            }
            Phase.Data -> {
                dataIndex += 1
                val percent = (dataIndex * 100 / currentImage.packetCount).coerceIn(0, 100)
                onProgress(percent, "OTA data $dataIndex/${currentImage.packetCount}")
                if (dataIndex < currentImage.packetCount) {
                    sendData(currentImage)
                } else {
                    phase = Phase.End
                    client.write(TelinkOtaCodec.endPacket(currentImage.maxIndex))
                }
            }
            Phase.End -> {
                phase = Phase.WaitingResult
                onProgress(100, "OTA END sent; waiting OTA_RESULT")
            }
            else -> Unit
        }
    }

    private fun sendData(image: TelinkOtaCodec.FirmwareImage) {
        client.write(TelinkOtaCodec.dataPacket(image, dataIndex))
    }

    private fun onNotification(packet: ByteArray) {
        if (!active) return
        val code = TelinkOtaCodec.parseResult(packet) ?: return
        active = false
        phase = Phase.Finished
        if (code == 0) {
            onFinished(true, "OTA_SUCCESS; BMS should reboot into new firmware")
        } else {
            onFinished(false, "${TelinkOtaCodec.resultText(code)} (0x${code.toString(16).uppercase().padStart(2, '0')})")
        }
    }

    private fun fail(message: String) {
        if (!active) return
        active = false
        phase = Phase.Finished
        onFinished(false, message)
    }
}
