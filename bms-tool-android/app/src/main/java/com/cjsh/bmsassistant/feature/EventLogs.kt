package com.cjsh.bmsassistant.feature

data class DeviceEventLogRow(val position:Int, val eventName:String, val intervalText:String, val populated:Boolean)

object EventLogs {
    fun decode(words:IntArray): List<DeviceEventLogRow> = words.mapIndexed { i, word ->
        val id = (word ushr 8) and 0xFF
        val interval = word and 0xFF
        if (id == 0) DeviceEventLogRow(i + 1, "—", "—", false)
        else DeviceEventLogRow(i + 1, eventName(id), intervalText(interval), true)
    }

    fun eventName(id:Int):String = when(id) {
        1 -> "BMS启动"
        2 -> "进入休眠"
        3 -> "均衡开启"
        4 -> "加热开启"
        5 -> "制冷开启"
        6 -> "单体过压"
        7 -> "总压过压"
        8 -> "充电过流"
        9 -> "单体欠压"
        10 -> "总压欠压"
        11 -> "放电过流"
        12 -> "充电低温"
        13 -> "放电低温"
        14 -> "充电高温"
        15 -> "放电高温"
        16 -> "单体压差保护"
        17 -> "CBC错误"
        18 -> "AFE1错误"
        19 -> "AFE2错误"
        20 -> "EEPROM错误"
        else -> "未知事件($id)"
    }

    fun intervalText(code:Int):String = when {
        code == 0 -> "启动记录 / 0"
        code == 171 -> "≤1分钟"
        code == 170 -> ">168小时"
        code in 1..168 -> "约 $code 小时"
        else -> "未知间隔编码 $code"
    }
}
