package com.telink.bmsassistant.domain

import com.telink.bmsassistant.protocol.BMSGeneratedRegisterCatalog
import com.telink.bmsassistant.protocol.BmsModbusCodec
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter

enum class ScanMode(val title: String) {
    AllDevices("全部设备"),
    TargetFirmware("当前固件"),
}

enum class DetailPage(val title: String) {
    Battery("电池状态"),
    Debug("调试工作台"),
}

enum class ConnectionStatus(val title: String) {
    Idle("空闲"),
    Scanning("扫描中"),
    Connecting("连接中"),
    Connected("已连接"),
    Ready("可收发"),
    Disconnected("已断开"),
    Failed("异常"),
}

enum class ExchangeDirection(val title: String) {
    Tx("TX"),
    Rx("RX"),
    Info("INFO"),
    Error("ERR"),
}

enum class BatteryDataSource(val title: String) {
    Unavailable("未读取"),
    LegacyRegisters("旧寄存器兼容模式"),
    RealtimeWindow("实时窗口模式"),
}

data class DiscoveredDevice(
    val id: String,
    val name: String = "",
    val rssi: Int = -127,
    val advertisedServices: List<String> = emptyList(),
    val isConnected: Boolean = false,
) {
    val displayName: String
        get() = name.ifBlank { "Unnamed ${id.takeLast(6)}" }

    val advertisedServicesSummary: String
        get() = advertisedServices.ifEmpty { listOf("No advertised UUID") }.joinToString(", ")

    val isLikelyBms: Boolean
        get() {
            val upperName = displayName.uppercase()
            val services = advertisedServices.map { it.uppercase() }.toSet()
            return upperName.startsWith("BT") ||
                "180F" in services ||
                "1812" in services ||
                "0000180F-0000-1000-8000-00805F9B34FB" in services ||
                "00001812-0000-1000-8000-00805F9B34FB" in services
        }
}

data class DeviceIdentitySnapshot(
    val displayName: String = "-",
    val macAddress: String = "-",
    val serialNumber: String = "-",
    val hardwareVersion: String = "-",
    val softwareVersion: String = "-",
)

data class ExchangeLogEntry(
    val timestamp: LocalDateTime,
    val direction: ExchangeDirection,
    val title: String,
    val payloadHex: String,
    val note: String,
) {
    val timestampText: String
        get() = timestamp.format(DateTimeFormatter.ofPattern("HH:mm:ss"))
}

data class CellVoltageSample(
    val index: Int,
    val millivolts: Int,
) {
    val title: String
        get() = "Cell $index"

    val voltageText: String
        get() = "%.3f V".format(millivolts / 1000.0)
}

data class StatusFlagSample(
    val key: String,
    val title: String,
    val isActive: Boolean,
)

data class RegisterBlock(
    val title: String,
    val startAddress: Int,
    val words: List<Int>,
    val updatedAt: LocalDateTime,
    val responseHex: String,
) {
    val startAddressText: String
        get() = "0x${startAddress.toString(16).uppercase().padStart(4, '0')}"

    fun wordLines(): String {
        if (words.isEmpty()) return "无数据"
        return words.mapIndexed { index, value ->
            val address = startAddress + index
            "0x${address.toString(16).uppercase().padStart(4, '0')}: " +
                "0x${value.toString(16).uppercase().padStart(4, '0')} ($value)"
        }.joinToString("\n")
    }
}

data class BatteryStatusSnapshot(
    val isSupported: Boolean = false,
    val source: BatteryDataSource = BatteryDataSource.Unavailable,
    val supportsRealtimeWindow: Boolean = false,
    val protocolVersion: Int = 0,
    val packVoltageRaw: Int = 0,
    val signedCurrentRaw: Int = 0,
    val socRaw: Int = 0,
    val maxTempRaw: Int = 0,
    val minTempRaw: Int = 0,
    val mosTempRaw: Int = 0,
    val maxCellVoltageRaw: Int = 0,
    val minCellVoltageRaw: Int = 0,
    val cellDeltaRaw: Int = 0,
    val maxCellPosition: Int = 0,
    val minCellPosition: Int = 0,
    val sohRaw: Int = 0,
    val capacityNowRaw: Int = 0,
    val capacityFullRaw: Int = 0,
    val capacityFactoryRaw: Int = 0,
    val cycleCountRaw: Int = 0,
    val legacyPackVoltageRawMv: Int = 0,
    val legacyBatteryTempAdcMv: Int = 0,
    val legacyMosTempAdcMv: Int = 0,
    val cellVoltages: List<CellVoltageSample> = emptyList(),
    val statusFlags: List<StatusFlagSample> = batteryStatusFlags(0),
    val systemStatusRaw: Int = 0,
    val updatedAt: LocalDateTime? = null,
) {
    val packVoltageText: String
        get() = if (isSupported) "%.2f V".format(packVoltageRaw / 100.0) else "-"

    val currentText: String
        get() = if (isSupported) "%.1f A".format(signedCurrentRaw / 10.0) else "-"

    val currentDirectionText: String
        get() = when {
            !isSupported -> "未支持"
            signedCurrentRaw > 0 -> "充电"
            signedCurrentRaw < 0 -> "放电"
            else -> "静置"
        }

    val socText: String
        get() = if (isSupported) "$socRaw %" else "-"

    val maxTempText: String
        get() = temperatureText(maxTempRaw)

    val minTempText: String
        get() = temperatureText(minTempRaw)

    val mosTempText: String
        get() = temperatureText(mosTempRaw)

    val maxCellVoltageText: String
        get() = if (maxCellVoltageRaw > 0) "$maxCellVoltageRaw mV" else "-"

    val minCellVoltageText: String
        get() = if (minCellVoltageRaw > 0) "$minCellVoltageRaw mV" else "-"

    val cellDeltaText: String
        get() = if (cellDeltaRaw > 0 || maxCellVoltageRaw > 0 || minCellVoltageRaw > 0) "$cellDeltaRaw mV" else "-"

    val sohText: String
        get() = if (isSupported) "$sohRaw %" else "-"

    val capacityNowText: String
        get() = if (isSupported) "%.2f Ah".format(capacityNowRaw / 100.0) else "-"

    val capacityFullText: String
        get() = if (isSupported) "%.2f Ah".format(capacityFullRaw / 100.0) else "-"

    val capacityFactoryText: String
        get() = if (isSupported) "%.2f Ah".format(capacityFactoryRaw / 100.0) else "-"

    val cycleCountText: String
        get() = if (isSupported) "$cycleCountRaw" else "-"

    val systemStatusHexText: String
        get() = "0x${systemStatusRaw.toString(16).uppercase().padStart(8, '0')}"

    val activeStatusFlags: List<StatusFlagSample>
        get() = statusFlags.filter { it.isActive }

    val updatedAtText: String
        get() = updatedAt?.format(DateTimeFormatter.ofPattern("HH:mm:ss")) ?: "未刷新"

    private fun temperatureText(raw: Int): String {
        if (!isSupported) return "-"
        return "%.1f °C".format(raw / 10.0 - 40.0)
    }
}

data class BmsUiState(
    val bluetoothStateLabel: String = "系统托管",
    val connectionStatus: ConnectionStatus = ConnectionStatus.Idle,
    val statusMessage: String = "等待蓝牙初始化",
    val scanMode: ScanMode = ScanMode.AllDevices,
    val showOnlyLikelyBms: Boolean = false,
    val searchText: String = "",
    val selectedDeviceId: String? = null,
    val devices: List<DiscoveredDevice> = emptyList(),
    val connectedDeviceName: String = "未连接",
    val identity: DeviceIdentitySnapshot = DeviceIdentitySnapshot(),
    val batteryStatus: BatteryStatusSnapshot = BatteryStatusSnapshot(),
    val latestCellArrayBlock: RegisterBlock? = null,
    val latestStatusBlock: RegisterBlock? = null,
    val latestRealtimeBlock: RegisterBlock? = null,
    val latestProtectBlock: RegisterBlock? = null,
    val latestEventLogBlock: RegisterBlock? = null,
    val latestManualBlock: RegisterBlock? = null,
    val responsePreview: String = "",
    val logs: List<ExchangeLogEntry> = emptyList(),
    val busyCommandName: String? = null,
) {
    val canSendCommands: Boolean
        get() = connectionStatus == ConnectionStatus.Ready && busyCommandName == null

    val filteredDevices: List<DiscoveredDevice>
        get() {
            val normalized = searchText.trim().lowercase()
            return devices.filter { device ->
                val modeOk = scanMode == ScanMode.AllDevices || device.isLikelyBms
                val likelyOk = !showOnlyLikelyBms || device.isLikelyBms
                val searchOk = normalized.isBlank() ||
                    listOf(device.displayName, device.id, device.advertisedServicesSummary)
                        .joinToString(" ")
                        .lowercase()
                        .contains(normalized)
                modeOk && likelyOk && searchOk
            }.sortedWith(
                compareByDescending<DiscoveredDevice> { it.isConnected }
                    .thenByDescending { it.isLikelyBms }
                    .thenByDescending { it.rssi }
                    .thenBy { it.displayName },
            )
        }

    val batteryBlocks: List<RegisterBlock>
        get() = listOfNotNull(latestCellArrayBlock, latestStatusBlock, latestRealtimeBlock)

    val debugBlocks: List<RegisterBlock>
        get() = listOfNotNull(latestStatusBlock, latestProtectBlock, latestEventLogBlock, latestManualBlock)
}

fun decodeBatteryStatus(
    realtimeWords: List<Int>,
    legacyCellWords: List<Int>,
    systemStatusWords: List<Int>,
    updatedAt: LocalDateTime,
): BatteryStatusSnapshot {
    fun word(index: Int, words: List<Int>): Int = words.getOrNull(index)?.and(0xFFFF) ?: 0
    fun int16(value: Int): Int = if ((value and 0x8000) != 0) value - 0x10000 else value

    val cellWords = legacyCellWords.take(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_WORD_COUNT)
    val cells = cellWords.take(10).mapIndexed { index, value ->
        CellVoltageSample(index = index + 1, millivolts = value)
    }
    val chargeCurrent = int16(word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_CHARGE_CURRENT_RAW_INDEX, cellWords))
    val dischargeCurrent = int16(word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_DISCHARGE_CURRENT_RAW_INDEX, cellWords))
    val signedCurrent = if (dischargeCurrent > 0) -dischargeCurrent else chargeCurrent
    val statusLow = systemStatusWords.getOrNull(0) ?: 0
    val statusHigh = systemStatusWords.getOrNull(1) ?: 0
    val statusRaw = statusLow or (statusHigh shl 16)

    var snapshot = BatteryStatusSnapshot(
        isSupported = cells.isNotEmpty() || systemStatusWords.isNotEmpty(),
        source = BatteryDataSource.LegacyRegisters,
        supportsRealtimeWindow = false,
        packVoltageRaw = word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_PACK_VOLTAGE_ENGINEERING_RAW_INDEX, cellWords),
        signedCurrentRaw = signedCurrent,
        socRaw = word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_SOC_RAW_INDEX, cellWords),
        maxTempRaw = word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_MAX_TEMPERATURE_RAW_INDEX, cellWords),
        minTempRaw = word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_MIN_TEMPERATURE_RAW_INDEX, cellWords),
        mosTempRaw = word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_MOS_TEMPERATURE_RAW_INDEX, cellWords),
        maxCellVoltageRaw = word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_MAX_CELL_VOLTAGE_MV_INDEX, cellWords),
        minCellVoltageRaw = word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_MIN_CELL_VOLTAGE_MV_INDEX, cellWords),
        cellDeltaRaw = word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_CELL_DELTA_MV_INDEX, cellWords),
        maxCellPosition = word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_MAX_CELL_POSITION_INDEX, cellWords),
        minCellPosition = word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_MIN_CELL_POSITION_INDEX, cellWords),
        sohRaw = word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_SOH_RAW_INDEX, cellWords),
        capacityNowRaw = word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_CAPACITY_NOW_RAW_INDEX, cellWords),
        capacityFullRaw = word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_CAPACITY_FULL_RAW_INDEX, cellWords),
        capacityFactoryRaw = word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_CAPACITY_FACTORY_RAW_INDEX, cellWords),
        cycleCountRaw = word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_CYCLE_COUNT_RAW_INDEX, cellWords),
        legacyPackVoltageRawMv = word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_PACK_VOLTAGE_ADC_MV_INDEX, cellWords),
        legacyBatteryTempAdcMv = word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_BATTERY_TEMP_ADC_MV_INDEX, cellWords),
        legacyMosTempAdcMv = word(BMSGeneratedRegisterCatalog.LEGACY_CELL_ARRAY_MOS_TEMP_ADC_MV_INDEX, cellWords),
        cellVoltages = cells,
        statusFlags = batteryStatusFlags(statusRaw),
        systemStatusRaw = statusRaw,
        updatedAt = updatedAt,
    )

    if (
        realtimeWords.size >= BMSGeneratedRegisterCatalog.REALTIME_STATUS_WORD_COUNT &&
        realtimeWords[0] == BMSGeneratedRegisterCatalog.REALTIME_STATUS_MAGIC
    ) {
        snapshot = snapshot.copy(
            source = BatteryDataSource.RealtimeWindow,
            supportsRealtimeWindow = true,
            protocolVersion = realtimeWords[1],
            packVoltageRaw = realtimeWords[2],
            signedCurrentRaw = int16(realtimeWords[3]),
            socRaw = realtimeWords[4],
            maxTempRaw = realtimeWords[5],
            minTempRaw = realtimeWords[6],
            mosTempRaw = realtimeWords[7],
            maxCellVoltageRaw = realtimeWords[8],
            minCellVoltageRaw = realtimeWords[9],
            cellDeltaRaw = realtimeWords[10],
        )
    }
    return snapshot
}

fun batteryStatusFlags(raw: Int): List<StatusFlagSample> {
    val flags = listOf(
        "startup" to "Startup",
        "mos_pre" to "MOS Pre",
        "mos_chg" to "MOS CHG",
        "mos_dsg" to "MOS DSG",
        "relay_pre" to "Relay Pre",
        "relay_chg" to "Relay CHG",
        "relay_dsg" to "Relay DSG",
        "relay_main" to "Relay Main",
        "heat" to "Heat",
        "cool" to "Cool",
        "afe1" to "AFE1",
        "afe2" to "AFE2",
        "balance" to "Balance",
        "sleep" to "Sleep",
        "bn_close" to "BN Close",
        "heat_close" to "Heat Close",
        "driver_ext" to "Driver Ext",
    )
    val bits = listOf(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 18)
    return flags.zip(bits).map { (item, bit) ->
        StatusFlagSample(
            key = item.first,
            title = item.second,
            isActive = (raw and (1 shl bit)) != 0,
        )
    }
}

fun registerBlockText(blocks: List<RegisterBlock>): String {
    if (blocks.isEmpty()) return "尚无寄存器块。"
    return blocks.joinToString("\n\n") { block ->
        "[${block.title}] 起始 ${block.startAddressText}\n" +
            block.wordLines() +
            "\n响应: ${block.responseHex}"
    }
}

fun batterySnapshotJson(state: BmsUiState): String {
    val snapshot = state.batteryStatus
    val cells = snapshot.cellVoltages.joinToString(prefix = "[", postfix = "]") {
        """{"index":${it.index},"millivolts":${it.millivolts}}"""
    }
    return """
        {
          "exportedAt": "${LocalDateTime.now()}",
          "connectedDevice": "${state.connectedDeviceName}",
          "identity": {
            "displayName": "${state.identity.displayName}",
            "macAddress": "${state.identity.macAddress}",
            "serialNumber": "${state.identity.serialNumber}",
            "hardwareVersion": "${state.identity.hardwareVersion}",
            "softwareVersion": "${state.identity.softwareVersion}"
          },
          "batteryStatus": {
            "source": "${snapshot.source.title}",
            "packVoltageRaw": ${snapshot.packVoltageRaw},
            "signedCurrentRaw": ${snapshot.signedCurrentRaw},
            "socRaw": ${snapshot.socRaw},
            "maxTempRaw": ${snapshot.maxTempRaw},
            "minTempRaw": ${snapshot.minTempRaw},
            "mosTempRaw": ${snapshot.mosTempRaw},
            "systemStatusRaw": ${snapshot.systemStatusRaw},
            "cellVoltages": $cells
          },
          "blocks": "${registerBlockText(state.batteryBlocks).replace("\"", "\\\"").replace("\n", "\\n")}"
        }
    """.trimIndent()
}

fun exchangeLogCsv(logs: List<ExchangeLogEntry>): String {
    val header = "时间,方向,标题,Payload,说明"
    val rows = logs.map { entry ->
        listOf(
            entry.timestamp.toString(),
            entry.direction.title,
            entry.title,
            entry.payloadHex,
            entry.note,
        ).joinToString(",") { "\"${it.replace("\"", "\"\"")}\"" }
    }
    return (listOf(header) + rows).joinToString("\n")
}

fun parseReadResponseBlock(title: String, start: Int, raw: ByteArray): RegisterBlock {
    val response = BmsModbusCodec.parseResponse(raw)
    if (response.kind != "read_holding") {
        throw IllegalArgumentException("响应内容不符合预期: 收到的不是 0x03 读寄存器响应")
    }
    return RegisterBlock(
        title = title,
        startAddress = start,
        words = response.words,
        updatedAt = LocalDateTime.now(),
        responseHex = BmsModbusCodec.spacedHex(raw),
    )
}
