package com.example.bmsassistant.bms

import java.io.IOException
import kotlin.math.roundToInt

enum class ValueKind{MV,PACK_V,CURRENT_A,TEMP_C,PERCENT,FILTER}
data class ProtectParam(val index:Int,val group:String,val stage:String,val kind:ValueKind,var device:Int=0,var edit:String=""){
    val address get()=BmsRegisters.PROTECT+index
    fun unit()=when(kind){ValueKind.MV->"mV";ValueKind.PACK_V->"V";ValueKind.CURRENT_A->"A";ValueKind.TEMP_C->"℃";ValueKind.PERCENT->"%";ValueKind.FILTER->"滤波值"}
    fun display(raw:Int)=when(kind){ValueKind.PACK_V->"%.2f".format(raw/100.0);ValueKind.CURRENT_A->"%.1f".format(raw/10.0);ValueKind.TEMP_C->"%.1f".format(raw/10.0-40);else->raw.toString()}
    fun parse():Int{val x=edit.trim().toDoubleOrNull()?:throw IllegalArgumentException("${group}/${stage} 输入无效");val r=when(kind){ValueKind.PACK_V->x*100;ValueKind.CURRENT_A->x*10;ValueKind.TEMP_C->(x+40)*10;else->x};if(r<0||r>65535)throw IllegalArgumentException("${group}/${stage} 超范围");return r.roundToInt()}
    fun load(v:Int){device=v;edit=display(v)}
}
object ProtectionCatalog{
    val groups=arrayOf("单体过压","单体欠压","总压过压","总压欠压","充电过流","放电过流","充电高温","充电低温","放电高温","放电低温","MOS高温","压差过大","SOC低保护")
    val stages=arrayOf("一级","二级","三级","恢复","滤波")
    fun create():MutableList<ProtectParam>{val out=mutableListOf<ProtectParam>();var idx=0;groups.indices.forEach{g->stages.indices.forEach{s->val kind=if(s==4)ValueKind.FILTER else when(g){0,1,11->ValueKind.MV;2,3->ValueKind.PACK_V;4,5->ValueKind.CURRENT_A;6,7,8,9,10->ValueKind.TEMP_C;12->ValueKind.PERCENT;else->ValueKind.FILTER};out+=ProtectParam(idx++,groups[g],stages[s],kind)}};return out}
}

data class AfeParam(val wireIndex:Int,val group:String,val name:String,val unit:String,val hint:String,val decode:(Int)->String,val encode:(String)->Int,var device:Int=0,var edit:String="") { fun load(v:Int){device=v;edit=decode(v)} }
data class AfeWriteGroup(val start:Int,val values:IntArray,val name:String)
class AfeModel{
    val rows=mutableListOf<AfeParam>();var raw=IntArray(24)
    private val ovUv=intArrayOf(10,20,30,40,60,80,100,200,300,400,600,800,1000,2000,3000,4000)
    private val occOcd2=intArrayOf(1,2,4,6,8,10,20,40,60,80,100,200,400,800,1000,2000)
    private val ocd1D=intArrayOf(5,10,20,40,60,80,100,200,400,600,800,1000,1500,2000,3000,4000)
    private val occCur=intArrayOf(200,300,400,500,600,700,800,900,1000,1100,1200,1300,1400,1600,1800,2000)
    private val ocd2Cur=intArrayOf(300,400,500,600,700,800,900,1000,1200,1400,1600,1800,2000,3000,4000,5000)
    private val shortCur=intArrayOf(50,80,110,140,170,200,230,260,290,320,350,400,500,600,800,1000)
    private val shortDelay=intArrayOf(0,64,128,192,256,320,384,448,512,576,640,704,768,832,896,960)
    init{
        rows+=voltage(0,"单体过压","保护电压",3600,4500,5);rows+=voltage(1,"单体过压","恢复电压",3300,4500,5);rows+=delay(2,"单体过压","保护延时",ovUv)
        rows+=voltage(3,"单体欠压","保护电压",2000,3100,20);rows+=voltage(4,"单体欠压","恢复电压",2000,3600,20);rows+=delay(5,"单体欠压","保护延时",ovUv)
        rows+=current(6,"充电过流","硬件阈值",occCur);rows+=delay(7,"充电过流","硬件延时",occOcd2)
        rows+=current(10,"放电过流1","硬件阈值",occCur);rows+=delay(11,"放电过流1","硬件延时",ocd1D)
        rows+=current(12,"放电过流2","硬件阈值",ocd2Cur);rows+=delay(13,"放电过流2","硬件延时",occOcd2)
        rows+=temp(14,"充电高温","保护温度",45,70);rows+=temp(15,"充电高温","恢复温度",40,70);rows+=temp(16,"充电低温","保护温度",-20,10);rows+=temp(17,"充电低温","恢复温度",-20,15)
        rows+=temp(18,"放电高温","保护温度",45,80);rows+=temp(19,"放电高温","恢复温度",40,80);rows+=temp(20,"放电低温","保护温度",-40,10);rows+=temp(21,"放电低温","恢复温度",-40,15)
        rows+=direct(22,"短路保护","短路电流","A",shortCur);rows+=direct(23,"短路保护","短路延时","us",shortDelay)
    }
    fun load(a:IntArray){require(a.size==24);raw=a.copyOf();rows.forEach{it.load(a[it.wireIndex])}}
    fun candidate():IntArray{val c=raw.copyOf();rows.forEach{c[it.wireIndex]=it.encode(it.edit)};c[8]=c[6];c[9]=c[7];if(c[1]>=c[0])error("单体过压恢复电压必须小于保护电压");if(c[4]<=c[3])error("单体欠压恢复电压必须大于保护电压");if(c[15]>=c[14])error("充电高温恢复温度必须低于保护温度");if(c[17]<=c[16])error("充电低温恢复温度必须高于保护温度");if(c[19]>=c[18])error("放电高温恢复温度必须低于保护温度");if(c[21]<=c[20])error("放电低温恢复温度必须高于保护温度");return c}
    fun changedGroups(c:IntArray):List<AfeWriteGroup>{val defs=listOf(Triple(0,3,"单体过压"),Triple(3,3,"单体欠压"),Triple(6,4,"充电过流"),Triple(10,2,"放电过流1"),Triple(12,2,"放电过流2"),Triple(14,2,"充电高温"),Triple(16,2,"充电低温"),Triple(18,2,"放电高温"),Triple(20,2,"放电低温"),Triple(22,2,"短路保护"));return defs.filter{(s,n,_)->(0 until n).any{raw[s+it]!=c[s+it]}}.map{(s,n,name)->AfeWriteGroup(BmsRegisters.AFE+s,c.copyOfRange(s,s+n),name)}}
    private fun voltage(i:Int,g:String,n:String,min:Int,max:Int,step:Int)=AfeParam(i,g,n,"mV","$min~$max mV，步进$step",{it.toString()},{s->val v=s.toIntOrNull()?:error("请输入整数mV");if(v !in min..max||(v-min)%step!=0)error("$g/$n 不符合范围或步进");v})
    private fun delay(i:Int,g:String,n:String,allowed:IntArray)=AfeParam(i,g,n,"ms","SH367309离散档位",{(it*10).toString()},{s->val ms=s.toIntOrNull()?:error("请输入整数ms");if(ms%10!=0||!allowed.contains(ms/10))error("$g/$n 不是支持的离散档位");ms/10})
    private fun current(i:Int,g:String,n:String,allowed:IntArray)=AfeParam(i,g,n,"A","SH367309离散档位",{"%.1f".format(it/10.0)},{s->val a=s.toDoubleOrNull()?:error("请输入电流A");val w=(a*10).roundToInt();if(!allowed.contains(w))error("$g/$n 不是支持的离散档位");w})
    private fun temp(i:Int,g:String,n:String,min:Int,max:Int)=AfeParam(i,g,n,"℃","$min~$max℃",{(it/10.0-40).roundToInt().toString()},{s->val c=s.toIntOrNull()?:error("请输入整数℃");if(c !in min..max)error("$g/$n 超范围");(c+40)*10})
    private fun direct(i:Int,g:String,n:String,u:String,allowed:IntArray)=AfeParam(i,g,n,u,"SH367309离散档位",{it.toString()},{s->val v=s.toIntOrNull()?:error("请输入整数");if(!allowed.contains(v))error("$g/$n 不是支持的离散档位");v})
}

fun BmsClient.readSoftwareProtection()=readRegisters(BmsRegisters.PROTECT,BmsRegisters.PROTECT_COUNT)
fun BmsClient.writeSoftwareParam(p:ProtectParam){val v=p.parse();writeSingle(p.address,v);val rb=readRegisters(p.address,1)[0];if(rb!=v)throw IOException("${p.group}/${p.stage} 回读不一致");p.load(rb)}
fun BmsClient.readAfe()=readRegisters(BmsRegisters.AFE,BmsRegisters.AFE_COUNT)
fun BmsClient.writeAfe(model:AfeModel){val c=model.candidate();for(g in model.changedGroups(c)){writeMultiple(g.start,g.values)};val rb=readAfe();if(!rb.contentEquals(c))throw IOException("AFE参数回读不一致");model.load(rb)}
