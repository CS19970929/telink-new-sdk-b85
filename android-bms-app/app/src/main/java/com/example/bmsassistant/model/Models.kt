package com.example.bmsassistant.model

data class DeviceIdentity(val mac:String, val serial:String, val hardware:String, val software:String, val bluetoothName:String)

data class BatterySnapshot(
    val timeMs:Long = System.currentTimeMillis(), val packVoltageV:Double, val currentA:Double, val soc:Int, val soh:Int,
    val maxTempC:Double, val minTempC:Double, val mosTempC:Double, val maxCellMv:Int, val minCellMv:Int, val deltaMv:Int,
    val maxCellPos:Int, val minCellPos:Int, val cycleCount:Int, val capacityNowAh:Double, val capacityFullAh:Double,
    val capacityFactoryAh:Double, val systemStatus:Long, val protect1:Int, val protect2:Int, val protect3:Int,
    val cells:List<Int>
) {
    val workState get() = when { currentA > .05 -> "充电"; currentA < -.05 -> "放电"; else -> "静置" }
    fun bit(n:Int) = (systemStatus and (1L shl n)) != 0L
    val prechargeMos get() = bit(1); val chargeMos get()=bit(2); val dischargeMos get()=bit(3)
    val heating get()=bit(8); val cooling get()=bit(9); val afe1 get()=bit(10); val afe2 get()=bit(11); val balancing get()=bit(12); val preparingSleep get()=bit(13)
    val protectionSummary get() = if ((protect1 or protect2 or protect3) == 0) "正常" else "保护中"
    fun protectText(raw:Int):String {
        val names = arrayOf("单体过压","单体欠压","总压过压","总压欠压","充电过流","放电过流","充电高温","放电高温","充电低温","放电低温","单体压差过大","温差过大","SOC过低","MOS高温")
        val a = names.indices.filter { raw and (1 shl it) != 0 }.map { names[it] }
        return if (a.isEmpty()) "无" else a.joinToString("、")
    }
}

data class EventLogRow(val position:Int, val event:String, val interval:String, val valid:Boolean)
data class MonitorRecord(val snapshot:BatterySnapshot, val identity:DeviceIdentity?)
