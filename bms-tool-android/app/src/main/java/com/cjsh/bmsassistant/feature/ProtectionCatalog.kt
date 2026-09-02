package com.cjsh.bmsassistant.feature

import com.cjsh.bmsassistant.protocol.BmsRegisters
import kotlin.math.roundToInt

enum class ProtectionValueKind { MILLIVOLT, PACK_VOLT_001V, CURRENT_01A, TEMP_OFFSET_01C, PERCENT, FILTER_RAW }

data class ProtectionParameter(
    val index: Int,
    val address: Int,
    val group: String,
    val stage: String,
    val kind: ProtectionValueKind,
    var deviceRaw: Int = 0
) {
    val name get() = "$group / $stage"
    val unit get() = when (kind) {
        ProtectionValueKind.MILLIVOLT -> "mV"
        ProtectionValueKind.PACK_VOLT_001V -> "V"
        ProtectionValueKind.CURRENT_01A -> "A"
        ProtectionValueKind.TEMP_OFFSET_01C -> "℃"
        ProtectionValueKind.PERCENT -> "%"
        ProtectionValueKind.FILTER_RAW -> "滤波值"
    }
    fun display(raw: Int = deviceRaw): String = when (kind) {
        ProtectionValueKind.PACK_VOLT_001V -> "%.2f".format(java.util.Locale.US, raw / 100.0)
        ProtectionValueKind.CURRENT_01A -> "%.1f".format(java.util.Locale.US, raw / 10.0)
        ProtectionValueKind.TEMP_OFFSET_01C -> "%.1f".format(java.util.Locale.US, raw / 10.0 - 40.0)
        else -> raw.toString()
    }
    fun parse(text: String): Int {
        val v = text.trim().toDoubleOrNull() ?: throw IllegalArgumentException("$name 请输入有效数值")
        val raw = when (kind) {
            ProtectionValueKind.PACK_VOLT_001V -> v * 100.0
            ProtectionValueKind.CURRENT_01A -> v * 10.0
            ProtectionValueKind.TEMP_OFFSET_01C -> (v + 40.0) * 10.0
            else -> v
        }.roundToInt()
        require(raw in 0..0xFFFF) { "$name 超出16位参数范围" }
        return raw
    }
}

object ProtectionCatalog {
    private val groups = arrayOf(
        "单体过压", "单体欠压", "总压过压", "总压欠压", "充电过流", "放电过流",
        "充电高温", "充电低温", "放电高温", "放电低温", "MOS高温", "压差过大", "SOC低保护"
    )
    private val stages = arrayOf("一级", "二级", "三级", "恢复", "滤波")

    fun create(): MutableList<ProtectionParameter> {
        val out = mutableListOf<ProtectionParameter>()
        var index = 0
        for (g in groups.indices) for (s in stages.indices) {
            val kind = if (s == 4) ProtectionValueKind.FILTER_RAW else when (g) {
                0, 1, 11 -> ProtectionValueKind.MILLIVOLT
                2, 3 -> ProtectionValueKind.PACK_VOLT_001V
                4, 5 -> ProtectionValueKind.CURRENT_01A
                6, 7, 8, 9, 10 -> ProtectionValueKind.TEMP_OFFSET_01C
                12 -> ProtectionValueKind.PERCENT
                else -> ProtectionValueKind.FILTER_RAW
            }
            out.add(ProtectionParameter(index, BmsRegisters.PROTECT + index, groups[g], stages[s], kind))
            index++
        }
        return out
    }
}
