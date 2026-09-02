package com.cjsh.bmsassistant.feature

import com.cjsh.bmsassistant.protocol.BmsRegisters
import kotlin.math.abs
import kotlin.math.roundToInt

data class AfeParameter(
    val wireIndex: Int,
    val group: String,
    val name: String,
    val unit: String,
    val hint: String,
    var currentWire: Int = 0
) {
    val title get() = "$group / $name"
}

data class AfeWriteGroup(val startRegister: Int, val values: IntArray, val name: String)

class AfeHardwareCatalog {
    companion object {
        const val BASE = BmsRegisters.AFE
        const val COUNT = BmsRegisters.AFE_COUNT
        private val OVUV_DELAY = intArrayOf(10,20,30,40,60,80,100,200,300,400,600,800,1000,2000,3000,4000)
        private val OCC_OCD2_DELAY = intArrayOf(1,2,4,6,8,10,20,40,60,80,100,200,400,800,1000,2000)
        private val OCD1_DELAY = intArrayOf(5,10,20,40,60,80,100,200,400,600,800,1000,1500,2000,3000,4000)
        private val OCC_OCD1_CURRENT = intArrayOf(200,300,400,500,600,700,800,900,1000,1100,1200,1300,1400,1600,1800,2000)
        private val OCD2_CURRENT = intArrayOf(300,400,500,600,700,800,900,1000,1200,1400,1600,1800,2000,3000,4000,5000)
        private val SHORT_CURRENT = intArrayOf(50,80,110,140,170,200,230,260,290,320,350,400,500,600,800,1000)
        private val SHORT_DELAY = intArrayOf(0,64,128,192,256,320,384,448,512,576,640,704,768,832,896,960)
    }

    val rows = mutableListOf<AfeParameter>()
    private var deviceRaw = IntArray(COUNT)

    init {
        add(0,"单体过压","保护电压","mV","3600~4500mV，5mV/step")
        add(1,"单体过压","恢复电压","mV","3300~4500mV，5mV/step")
        add(2,"单体过压","保护延时","ms","SH367309 离散延时档位")
        add(3,"单体欠压","保护电压","mV","2000~3100mV，20mV/step")
        add(4,"单体欠压","恢复电压","mV","2000~3600mV，20mV/step")
        add(5,"单体欠压","保护延时","ms","SH367309 离散延时档位")
        add(6,"充电过流","硬件阈值","A","20~200A离散档位")
        add(7,"充电过流","硬件延时","ms","SH367309 离散延时档位")
        add(10,"放电过流1","硬件阈值","A","20~200A离散档位")
        add(11,"放电过流1","硬件延时","ms","SH367309 离散延时档位")
        add(12,"放电过流2","硬件阈值","A","30~500A离散档位")
        add(13,"放电过流2","硬件延时","ms","SH367309 离散延时档位")
        add(14,"充电高温","保护温度","℃","45~70℃")
        add(15,"充电高温","恢复温度","℃","40~70℃")
        add(16,"充电低温","保护温度","℃","-20~10℃")
        add(17,"充电低温","恢复温度","℃","-20~15℃")
        add(18,"放电高温","保护温度","℃","45~80℃")
        add(19,"放电高温","恢复温度","℃","40~80℃")
        add(20,"放电低温","保护温度","℃","-40~10℃")
        add(21,"放电低温","恢复温度","℃","-40~15℃")
        add(22,"短路保护","短路电流","A","SH367309短路电流离散档位")
        add(23,"短路保护","短路延时","us","0~960us，64us/档")
    }

    private fun add(i:Int,g:String,n:String,u:String,h:String) { rows.add(AfeParameter(i,g,n,u,h)) }

    fun load(raw: IntArray) {
        require(raw.size == COUNT)
        deviceRaw = raw.copyOf()
        rows.forEach { it.currentWire = raw[it.wireIndex] }
    }

    fun display(row: AfeParameter, wire: Int = row.currentWire): String = when (row.wireIndex) {
        2,5,7,11,13 -> (wire * 10).toString()
        6,10,12 -> "%.1f".format(java.util.Locale.US, wire / 10.0)
        14,15,16,17,18,19,20,21 -> (wire / 10.0 - 40.0).roundToInt().toString()
        else -> wire.toString()
    }

    fun encode(row: AfeParameter, text: String): Int {
        val s = text.trim()
        return when (row.wireIndex) {
            0 -> encodeRangeInt(s, 3600, 4500, 5)
            1 -> encodeRangeInt(s, 3300, 4500, 5)
            2,5 -> encodeDelay(s, OVUV_DELAY)
            3 -> encodeRangeInt(s, 2000, 3100, 20)
            4 -> encodeRangeInt(s, 2000, 3600, 20)
            6,10 -> encodeCurrent(s, OCC_OCD1_CURRENT)
            7,13 -> encodeDelay(s, OCC_OCD2_DELAY)
            11 -> encodeDelay(s, OCD1_DELAY)
            12 -> encodeCurrent(s, OCD2_CURRENT)
            14 -> encodeTemp(s,45,70)
            15 -> encodeTemp(s,40,70)
            16 -> encodeTemp(s,-20,10)
            17 -> encodeTemp(s,-20,15)
            18 -> encodeTemp(s,45,80)
            19 -> encodeTemp(s,40,80)
            20 -> encodeTemp(s,-40,10)
            21 -> encodeTemp(s,-40,15)
            22 -> encodeDirect(s, SHORT_CURRENT)
            23 -> encodeDirect(s, SHORT_DELAY)
            else -> throw IllegalArgumentException("Unsupported AFE parameter index ${row.wireIndex}")
        }
    }

    fun buildCandidate(edits: Map<Int,String>): IntArray {
        val raw = deviceRaw.copyOf()
        rows.forEach { row -> raw[row.wireIndex] = encode(row, edits[row.wireIndex] ?: display(row)) }
        raw[8] = raw[6]
        raw[9] = raw[7]
        require(raw[1] < raw[0]) { "单体过压恢复电压必须小于保护电压" }
        require(raw[4] > raw[3]) { "单体欠压恢复电压必须大于保护电压" }
        require(raw[15] < raw[14]) { "充电高温恢复温度必须低于保护温度" }
        require(raw[17] > raw[16]) { "充电低温恢复温度必须高于保护温度" }
        require(raw[19] < raw[18]) { "放电高温恢复温度必须低于保护温度" }
        require(raw[21] > raw[20]) { "放电低温恢复温度必须高于保护温度" }
        return raw
    }

    fun changedGroups(candidate: IntArray): List<AfeWriteGroup> {
        require(candidate.size == COUNT)
        val defs = listOf(
            Triple(0,3,"单体过压"), Triple(3,3,"单体欠压"), Triple(6,4,"充电过流"),
            Triple(10,2,"放电过流1"), Triple(12,2,"放电过流2"), Triple(14,2,"充电高温"),
            Triple(16,2,"充电低温"), Triple(18,2,"放电高温"), Triple(20,2,"放电低温"), Triple(22,2,"短路保护")
        )
        return defs.mapNotNull { (start,count,name) ->
            val changed = (0 until count).any { deviceRaw[start + it] != candidate[start + it] }
            if (changed) AfeWriteGroup(BASE + start, candidate.copyOfRange(start, start + count), name) else null
        }
    }

    fun verify(readback: IntArray, candidate: IntArray) {
        require(readback.size == COUNT)
        for (i in 0 until COUNT) if (readback[i] != candidate[i]) throw IllegalStateException("AFE参数回读不一致 index=$i wrote=${candidate[i]} read=${readback[i]}")
        load(readback)
    }

    private fun encodeRangeInt(s:String,min:Int,max:Int,step:Int): Int {
        val v=s.toIntOrNull() ?: throw IllegalArgumentException("请输入整数")
        require(v in min..max) { "允许范围 $min~$max" }
        require((v-min)%step==0) { "必须满足 ${step} 的步进" }
        return v
    }
    private fun encodeDelay(s:String, allowed:IntArray):Int {
        val ms=s.toIntOrNull() ?: throw IllegalArgumentException("请输入整数毫秒值")
        require(ms>=0 && ms%10==0) { "延时必须是10ms整数倍" }
        val wire=ms/10
        require(allowed.contains(wire)) { "该延时不是SH367309支持的离散档位" }
        return wire
    }
    private fun encodeCurrent(s:String, allowed:IntArray):Int {
        val a=s.toDoubleOrNull() ?: throw IllegalArgumentException("请输入电流值(A)")
        val wire=(a*10.0).roundToInt()
        require(abs(a*10.0-wire)<0.0001 && allowed.contains(wire)) { "该电流不是当前SH367309支持的离散档位" }
        return wire
    }
    private fun encodeTemp(s:String,min:Int,max:Int):Int {
        val c=s.toIntOrNull() ?: throw IllegalArgumentException("请输入整数摄氏温度")
        require(c in min..max) { "允许范围 $min~$max℃" }
        return (c+40)*10
    }
    private fun encodeDirect(s:String,allowed:IntArray):Int {
        val v=s.toIntOrNull() ?: throw IllegalArgumentException("请输入整数")
        require(allowed.contains(v)) { "该值不是SH367309支持的离散档位" }
        return v
    }
}
