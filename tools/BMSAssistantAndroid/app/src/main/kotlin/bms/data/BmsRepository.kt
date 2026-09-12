package com.telink.bmsassistant.data

import android.os.Handler
import android.os.Looper
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.telink.bmsassistant.ble.BmsBleClient
import com.telink.bmsassistant.domain.BmsUiState
import com.telink.bmsassistant.domain.ConnectionStatus
import com.telink.bmsassistant.domain.DeviceIdentitySnapshot
import com.telink.bmsassistant.domain.DiscoveredDevice
import com.telink.bmsassistant.domain.ExchangeDirection
import com.telink.bmsassistant.domain.ExchangeLogEntry
import com.telink.bmsassistant.domain.RegisterBlock
import com.telink.bmsassistant.domain.ScanMode
import com.telink.bmsassistant.domain.decodeBatteryStatus
import com.telink.bmsassistant.domain.parseReadResponseBlock
import com.telink.bmsassistant.protocol.BMSGeneratedRegisterCatalog
import com.telink.bmsassistant.protocol.BMSGeneratedUUIDs
import com.telink.bmsassistant.protocol.BmsModbusCodec
import com.telink.bmsassistant.protocol.ModbusCodecError
import com.telink.bmsassistant.protocol.ResponseAccumulator
import java.time.LocalDateTime

private data class CommandStep(
    val title: String,
    val request: ByteArray,
    val startAddress: Int? = null,
    val expectedLengthHint: Int? = null,
    val handler: (ByteArray) -> Unit,
)

private data class PendingSequence(
    val actionName: String,
    val steps: List<CommandStep>,
    val onComplete: (() -> Unit)? = null,
    var index: Int = 0,
)

private data class PendingExchange(
    val name: String,
    val onSuccess: (ByteArray) -> Unit,
    val onFailure: (Exception) -> Unit,
    val timeoutRunnable: Runnable,
)

class BmsRepository(private val bleClient: BmsBleClient) {
    var state by mutableStateOf(BmsUiState())
        private set

    private val handler = Handler(Looper.getMainLooper())
    private val accumulator = ResponseAccumulator()
    private var pendingSequence: PendingSequence? = null
    private var pendingExchange: PendingExchange? = null
    private val devices = linkedMapOf<String, DiscoveredDevice>()

    init {
        bleClient.onDeviceDiscovered = ::onDeviceDiscovered
        bleClient.onConnectionChanged = ::onConnectionChanged
        bleClient.onReady = ::onReady
        bleClient.onData = ::onData
        bleClient.onError = ::reportError
    }

    fun setScanMode(mode: ScanMode) {
        state = state.copy(scanMode = mode)
    }

    fun setSearchText(text: String) {
        state = state.copy(searchText = text)
    }

    fun setShowOnlyLikelyBms(enabled: Boolean) {
        state = state.copy(showOnlyLikelyBms = enabled)
    }

    fun selectDevice(id: String?) {
        state = state.copy(selectedDeviceId = id)
    }

    fun startScan() {
        bleClient.startScan()
    }

    fun stopScan() {
        bleClient.stopScan()
    }

    fun connectSelected() {
        state.selectedDeviceId?.let { bleClient.connect(it) }
    }

    fun disconnect() {
        cancelActiveWorkflow(ModbusCodecError("连接已断开"), logError = false)
        bleClient.disconnect()
    }

    fun refreshIdentity() {
        val result = linkedMapOf<String, RegisterBlock>()
        val steps = listOf(
            makeReadStep("读取 MAC 地址", BMSGeneratedRegisterCatalog.MAC_ADDRESS_START, BMSGeneratedRegisterCatalog.MAC_ADDRESS_WORD_COUNT) {
                result["mac"] = it
            },
            makeReadStep("读取序列号", BMSGeneratedRegisterCatalog.PRODUCT_SERIAL_START, BMSGeneratedRegisterCatalog.PRODUCT_SERIAL_WORD_COUNT) {
                result["serial"] = it
            },
            makeReadStep("读取硬件版本", BMSGeneratedRegisterCatalog.PRODUCT_HARDWARE_VERSION_START, BMSGeneratedRegisterCatalog.PRODUCT_HARDWARE_VERSION_WORD_COUNT) {
                result["hardware"] = it
            },
            makeReadStep("读取软件版本", BMSGeneratedRegisterCatalog.PRODUCT_SOFTWARE_VERSION_START, BMSGeneratedRegisterCatalog.PRODUCT_SOFTWARE_VERSION_WORD_COUNT) {
                result["software"] = it
            },
        )
        startSequence("刷新设备身份", steps) {
            state = state.copy(
                identity = DeviceIdentitySnapshot(
                    displayName = state.connectedDeviceName,
                    macAddress = BmsModbusCodec.macStringFromWords(result.getValue("mac").words),
                    serialNumber = BmsModbusCodec.asciiStringFromWords(result.getValue("serial").words),
                    hardwareVersion = BmsModbusCodec.asciiStringFromWords(result.getValue("hardware").words),
                    softwareVersion = BmsModbusCodec.asciiStringFromWords(result.getValue("software").words),
                ),
                statusMessage = "设备身份信息已刷新",
            )
        }
    }

    fun refreshBatteryStatus() {
        val result = linkedMapOf<String, RegisterBlock>()
        val steps = listOf(
            makeReadStep("单串电压与兼容数据", BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_START, BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_WORD_COUNT) {
                result["legacy"] = it
            },
            makeReadStep("系统状态", BMSGeneratedRegisterCatalog.SYSTEM_STATUS_START, BMSGeneratedRegisterCatalog.SYSTEM_STATUS_WORD_COUNT) {
                result["status"] = it
            },
            makeReadStep("电池状态页", BMSGeneratedRegisterCatalog.REALTIME_STATUS_START, BMSGeneratedRegisterCatalog.REALTIME_STATUS_WORD_COUNT) {
                result["realtime"] = it
            },
        )
        startSequence("刷新电池状态", steps) {
            val legacy = result.getValue("legacy")
            val status = result.getValue("status")
            val realtime = result.getValue("realtime")
            val snapshot = decodeBatteryStatus(
                realtimeWords = realtime.words,
                legacyCellWords = legacy.words,
                systemStatusWords = status.words,
                updatedAt = LocalDateTime.now(),
            )
            state = state.copy(
                batteryStatus = snapshot,
                latestCellArrayBlock = legacy,
                latestStatusBlock = status,
                latestRealtimeBlock = realtime,
                statusMessage = if (snapshot.supportsRealtimeWindow) {
                    "电池状态已刷新（实时窗口模式）"
                } else {
                    "电池状态已刷新（旧寄存器兼容模式）"
                },
            )
        }
    }

    fun readProtectPreview() {
        val steps = listOf(
            makeReadStep(
                "保护参数预览",
                BMSGeneratedRegisterCatalog.PROTECT_PREVIEW_START,
                BMSGeneratedRegisterCatalog.PROTECT_PREVIEW_WORD_COUNT,
            ) {
                state = state.copy(latestProtectBlock = it)
            },
        )
        startSequence("读取保护参数预览", steps) {
            state = state.copy(statusMessage = "已读取保护参数预览")
        }
    }

    fun readEventLogPreview() {
        val steps = listOf(
            makeReadStep(
                "事件日志预览",
                BMSGeneratedRegisterCatalog.EVENT_LOG_PREVIEW_START,
                BMSGeneratedRegisterCatalog.EVENT_LOG_PREVIEW_WORD_COUNT,
            ) {
                state = state.copy(latestEventLogBlock = it)
            },
        )
        startSequence("读取事件日志预览", steps) {
            state = state.copy(statusMessage = "已读取事件日志预览")
        }
    }

    fun readManualBlock(startText: String, quantityText: String) {
        val start = BmsModbusCodec.parseAddress(startText)
        val quantity = BmsModbusCodec.parseAddress(quantityText)
        val steps = listOf(makeReadStep("手动读取", start, quantity) {
            state = state.copy(latestManualBlock = it)
        })
        startSequence("手动读取寄存器", steps) {
            state = state.copy(statusMessage = "手动读取完成")
        }
    }

    fun writeManualWords(addressText: String, wordsText: String) {
        val register = BmsModbusCodec.parseAddress(addressText)
        val words = BmsModbusCodec.parseWords(wordsText)
        val request = if (words.size == 1) {
            BmsModbusCodec.writeSingle(register, words[0])
        } else {
            BmsModbusCodec.writeMultiple(register, words)
        }
        BmsModbusCodec.ensureSafeBleLength(request)
        val steps = listOf(
            CommandStep("手动写寄存器", request) { frame ->
                val response = BmsModbusCodec.parseResponse(frame)
                if (response.kind !in setOf("write_single_ack", "write_multiple_ack")) {
                    throw ModbusCodecError("响应内容不符合预期: 写寄存器响应类型错误")
                }
            },
        )
        startSequence("手动写寄存器", steps) {
            state = state.copy(statusMessage = "写入完成")
        }
    }

    fun sendEchoTest() {
        val request = BmsModbusCodec.echo(byteArrayOf(0x12, 0x34, 0x56, 0x78))
        BmsModbusCodec.ensureSafeBleLength(request)
        val steps = listOf(
            CommandStep("Echo 测试", request, expectedLengthHint = request.size) { frame ->
                if (BmsModbusCodec.parseResponse(frame).kind != "echo") {
                    throw ModbusCodecError("Echo 响应类型错误")
                }
            },
        )
        startSequence("Echo 测试", steps) {
            state = state.copy(statusMessage = "Echo 成功，链路可收发")
        }
    }

    fun sendRawFrame(rawText: String) {
        val request = BmsModbusCodec.parseRawBytes(rawText)
        BmsModbusCodec.ensureSafeBleLength(request)
        val expected = if (request.size >= 2 && request[1].toUByte().toInt() == 0x7F) request.size else null
        val steps = listOf(
            CommandStep("原始帧", request, expectedLengthHint = expected) { frame ->
                BmsModbusCodec.parseResponse(frame)
            },
        )
        startSequence("发送原始 Modbus 帧", steps) {
            state = state.copy(statusMessage = "原始帧发送完成")
        }
    }

    fun writeSOC(valueText: String) {
        val value = BmsModbusCodec.parseAddress(valueText)
        if (value > 100) throw ModbusCodecError("SOC 建议范围 0~100")
        val request = BmsModbusCodec.writeSingle(BMSGeneratedRegisterCatalog.SOC_WRITE_REGISTER_START, value)
        BmsModbusCodec.ensureSafeBleLength(request)
        val steps = listOf(
            CommandStep("写入 SOC", request) { frame ->
                if (BmsModbusCodec.parseResponse(frame).kind != "write_single_ack") {
                    throw ModbusCodecError("响应内容不符合预期: SOC 写入响应类型错误")
                }
            },
        )
        startSequence("写入 SOC", steps) {
            state = state.copy(statusMessage = "SOC 写入完成，已写 0x1005 = $value")
            refreshBatteryStatus()
        }
    }

    fun writeRegister1103() {
        val request = BmsModbusCodec.writeSingle(BMSGeneratedRegisterCatalog.DEBUG_REGISTER_1103_START, 0x0003)
        BmsModbusCodec.ensureSafeBleLength(request)
        val steps = listOf(
            CommandStep("写入 0x1103", request) { frame ->
                if (BmsModbusCodec.parseResponse(frame).kind != "write_single_ack") {
                    throw ModbusCodecError("响应内容不符合预期: 0x1103 写入响应类型错误")
                }
            },
        )
        startSequence("写入 0x1103", steps) {
            state = state.copy(statusMessage = "已写 0x1103 = 0x0003")
        }
    }

    fun writeBluetoothNameSuffix(suffix: String) {
        val cleaned = suffix.trim()
        if (cleaned.isBlank()) throw ModbusCodecError("蓝牙名后缀不能为空")
        if (cleaned.toByteArray(Charsets.UTF_8).size > 10) {
            throw ModbusCodecError("蓝牙名后缀建议不超过 10 个 ASCII 字节")
        }
        val words = BmsModbusCodec.encodeAsciiWords(cleaned)
        val request = BmsModbusCodec.writeMultiple(BMSGeneratedRegisterCatalog.BT_NAME_START, words)
        BmsModbusCodec.ensureSafeBleLength(request)
        val steps = listOf(
            CommandStep("写入蓝牙名", request) { frame ->
                if (BmsModbusCodec.parseResponse(frame).kind != "write_multiple_ack") {
                    throw ModbusCodecError("响应内容不符合预期: 蓝牙名写入响应类型错误")
                }
            },
        )
        startSequence("写入蓝牙名后缀", steps) {
            state = state.copy(statusMessage = "蓝牙名已写入，请重新扫描确认广播名刷新")
        }
    }

    fun clearLogs() {
        state = state.copy(logs = emptyList())
    }

    private fun makeReadStep(
        title: String,
        start: Int,
        quantity: Int,
        assign: (RegisterBlock) -> Unit,
    ): CommandStep {
        val request = BmsModbusCodec.readHolding(start, quantity)
        BmsModbusCodec.ensureSafeBleLength(request)
        return CommandStep(title = title, request = request, startAddress = start) { raw ->
            assign(parseReadResponseBlock(title, start, raw))
        }
    }

    private fun startSequence(actionName: String, steps: List<CommandStep>, onComplete: (() -> Unit)? = null) {
        if (state.busyCommandName != null) {
            state = state.copy(statusMessage = "仍有请求进行中：${state.busyCommandName}")
            return
        }
        if (!state.canSendCommands) {
            state = state.copy(statusMessage = "BLE 通道尚未就绪，请先连接并完成特征发现")
            return
        }
        pendingSequence = PendingSequence(actionName = actionName, steps = steps, onComplete = onComplete)
        state = state.copy(busyCommandName = actionName)
        runNextStep()
    }

    private fun runNextStep() {
        val sequence = pendingSequence ?: return
        if (sequence.index >= sequence.steps.size) {
            pendingSequence = null
            state = state.copy(busyCommandName = null)
            sequence.onComplete?.invoke()
            return
        }
        val step = sequence.steps[sequence.index]
        sendRequest(
            name = step.title,
            request = step.request,
            expectedLengthHint = step.expectedLengthHint,
            onSuccess = { frame ->
                try {
                    val parsed = BmsModbusCodec.parseResponse(frame)
                    if (parsed.kind == "exception") {
                        throw ModbusCodecError(
                            "设备返回异常响应: function 0x${parsed.function?.toString(16)}, code 0x${parsed.code?.toString(16)}",
                        )
                    }
                    step.handler(frame)
                    sequence.index += 1
                    runNextStep()
                } catch (exc: Exception) {
                    cancelActiveWorkflow(exc)
                }
            },
            onFailure = ::cancelActiveWorkflow,
        )
    }

    private fun sendRequest(
        name: String,
        request: ByteArray,
        expectedLengthHint: Int?,
        onSuccess: (ByteArray) -> Unit,
        onFailure: (Exception) -> Unit,
    ) {
        if (pendingExchange != null) {
            onFailure(ModbusCodecError("当前仍有未完成请求: ${pendingExchange?.name}"))
            return
        }
        accumulator.reset(expectedLengthHint)
        appendLog(ExchangeDirection.Tx, name, BmsModbusCodec.spacedHex(request), "${request.size} byte")
        state = state.copy(responsePreview = "")

        val timeout = Runnable {
            appendLog(ExchangeDirection.Error, name, "", "等待响应超时")
            failPendingExchange(ModbusCodecError("等待响应超时"))
        }
        pendingExchange = PendingExchange(name, onSuccess, onFailure, timeout)
        try {
            bleClient.send(request)
        } catch (exc: Exception) {
            clearPendingExchange()
            onFailure(exc)
            return
        }
        handler.postDelayed(timeout, 3000)
    }

    private fun onData(fragment: ByteArray) {
        val event = accumulator.append(fragment)
        if (event.state == "waiting") {
            state = state.copy(
                responsePreview = BmsModbusCodec.spacedHex(accumulator.buffer),
                statusMessage = if (event.expectedLength == null) {
                    "接收响应中：已收 ${accumulator.buffer.size} byte"
                } else {
                    "接收响应中：已收 ${accumulator.buffer.size}/${event.expectedLength} byte，分片 ${event.fragments}"
                },
            )
            return
        }
        state = state.copy(responsePreview = BmsModbusCodec.spacedHex(event.frame))
        if (event.state == "invalid_crc") {
            appendLog(ExchangeDirection.Error, currentExchangeName(), state.responsePreview, "assembled from ${event.fragments} notify")
            failPendingExchange(ModbusCodecError("收到的响应 CRC 校验失败"))
            return
        }
        appendLog(ExchangeDirection.Rx, currentExchangeName(), state.responsePreview, "assembled from ${event.fragments} notify")
        succeedPendingExchange(event.frame)
    }

    private fun onReady() {
        appendLog(ExchangeDirection.Info, "BLE 就绪", "", "已订阅响应特征 ${BMSGeneratedUUIDs.RESPONSE_CHARACTERISTIC_UUID}")
        refreshBatteryStatus()
    }

    private fun onConnectionChanged(status: ConnectionStatus, message: String, deviceId: String?) {
        val connectedId = when (status) {
            ConnectionStatus.Connecting,
            ConnectionStatus.Connected,
            ConnectionStatus.Ready,
            -> deviceId ?: state.selectedDeviceId
            else -> null
        }
        if (connectedId != null && devices.containsKey(connectedId)) {
            devices[connectedId] = devices.getValue(connectedId).copy(isConnected = status == ConnectionStatus.Ready)
        }
        state = state.copy(
            connectionStatus = status,
            statusMessage = message,
            devices = devices.values.toList(),
            selectedDeviceId = connectedId ?: state.selectedDeviceId,
            connectedDeviceName = connectedId?.let { devices[it]?.displayName ?: it } ?: "未连接",
        )
        if (status == ConnectionStatus.Disconnected || status == ConnectionStatus.Failed) {
            state = state.copy(
                batteryStatus = com.telink.bmsassistant.domain.BatteryStatusSnapshot(),
                busyCommandName = null,
            )
            cancelActiveWorkflow(ModbusCodecError(message), logError = false)
        }
    }

    private fun onDeviceDiscovered(device: DiscoveredDevice) {
        val existing = devices[device.id]
        devices[device.id] = if (existing == null) {
            device
        } else {
            existing.copy(
                name = device.name.ifBlank { existing.name },
                rssi = device.rssi,
                advertisedServices = if (device.advertisedServices.isEmpty()) existing.advertisedServices else device.advertisedServices,
            )
        }
        val selected = state.selectedDeviceId ?: devices.values.firstOrNull()?.id
        state = state.copy(
            devices = devices.values.toList(),
            selectedDeviceId = selected,
            statusMessage = "扫描中，已发现 ${devices.size} 台设备，当前显示 ${state.filteredDevices.size} 台。",
        )
    }

    private fun appendLog(direction: ExchangeDirection, title: String, payloadHex: String, note: String) {
        state = state.copy(
            logs = (
                listOf(
                    ExchangeLogEntry(
                        timestamp = LocalDateTime.now(),
                        direction = direction,
                        title = title,
                        payloadHex = payloadHex,
                        note = note,
                    ),
                ) + state.logs
                ).take(200),
        )
    }

    private fun reportError(message: String) {
        appendLog(ExchangeDirection.Error, "BLE 错误", "", message)
        state = state.copy(statusMessage = message)
        cancelActiveWorkflow(ModbusCodecError(message), logError = false)
    }

    private fun cancelActiveWorkflow(error: Exception, logError: Boolean = true) {
        clearPendingExchange()
        pendingSequence = null
        if (logError) {
            appendLog(ExchangeDirection.Error, currentExchangeName(), "", error.message ?: error.toString())
        }
        state = state.copy(
            busyCommandName = null,
            statusMessage = error.message ?: error.toString(),
        )
    }

    private fun failPendingExchange(error: Exception) {
        val pending = pendingExchange
        clearPendingExchange()
        pending?.onFailure?.invoke(error)
    }

    private fun succeedPendingExchange(frame: ByteArray) {
        val pending = pendingExchange
        clearPendingExchange()
        pending?.onSuccess?.invoke(frame)
    }

    private fun clearPendingExchange() {
        pendingExchange?.let { handler.removeCallbacks(it.timeoutRunnable) }
        pendingExchange = null
    }

    private fun currentExchangeName(): String {
        return pendingExchange?.name ?: pendingSequence?.actionName ?: "未命名请求"
    }
}
