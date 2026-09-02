package com.cjsh.bmsassistant.model

data class DeviceIdentity(
    val mac: String,
    val serial: String,
    val hardware: String,
    val software: String,
    val bluetoothName: String
)

data class BatterySnapshot(
    val packVoltageV: Double,
    val currentA: Double,
    val socPercent: Int,
    val sohPercent: Int,
    val maxTempC: Double,
    val minTempC: Double,
    val mosTempC: Double,
    val maxCellMv: Int,
    val minCellMv: Int,
    val cellDeltaMv: Int,
    val maxCellPosition: Int,
    val minCellPosition: Int,
    val cycleCount: Int,
    val capacityNowAh: Double,
    val capacityFullAh: Double,
    val capacityFactoryAh: Double,
    val systemStatus: Long,
    val protocolVersion: Int,
    val usesRealtimeWindow: Boolean,
    val protectionLevel1Raw: Int,
    val protectionLevel2Raw: Int,
    val protectionLevel3Raw: Int,
    val cellMillivolts: List<Int>
) {
    val workState: String get() = if (currentA > 0.05) "充电" else if (currentA < -0.05) "放电" else "静置"
    val prechargeMosOn get() = bit(1)
    val chargeMosOn get() = bit(2)
    val dischargeMosOn get() = bit(3)
    val heatingOn get() = bit(8)
    val coolingOn get() = bit(9)
    val afe1On get() = bit(10)
    val afe2On get() = bit(11)
    val balancingOn get() = bit(12)
    val preparingSleep get() = bit(13)
    val systemLimited get() = bit(16)
    val hasProtection get() = (protectionLevel1Raw or protectionLevel2Raw or protectionLevel3Raw) != 0
    val protectionSummary get() = if (hasProtection) "保护中" else "正常"
    val protectionLevel1Text get() = decodeProtection(protectionLevel1Raw)
    val protectionLevel2Text get() = decodeProtection(protectionLevel2Raw)
    val protectionLevel3Text get() = decodeProtection(protectionLevel3Raw)
    val averageCellMv: Double get() = if (cellMillivolts.isEmpty()) 0.0 else cellMillivolts.average()

    private fun bit(bit: Int) = (systemStatus and (1L shl bit)) != 0L

    companion object {
        private val names = arrayOf(
            "单体过压", "单体欠压", "总压过压", "总压欠压",
            "充电过流", "放电过流", "充电高温", "放电高温",
            "充电低温", "放电低温", "单体压差过大", "温差过大",
            "SOC过低", "MOS高温"
        )
        fun decodeProtection(raw: Int): String {
            if ((raw and 0x3FFF) == 0) return "无"
            return names.indices.filter { (raw and (1 shl it)) != 0 }.joinToString("、") { names[it] }.ifEmpty { "无" }
        }
    }
}

data class MonitorRecord(
    val timeMillis: Long,
    val bluetoothName: String,
    val snapshot: BatterySnapshot,
    val identity: DeviceIdentity?
)

data class ScanEntry(val name: String, val address: String, val rssi: Int)
