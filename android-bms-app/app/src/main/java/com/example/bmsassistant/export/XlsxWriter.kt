package com.example.bmsassistant.export

import com.example.bmsassistant.model.MonitorRecord
import java.io.OutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

object XlsxWriter {
    fun write(records:List<MonitorRecord>,out:OutputStream,intervalSec:Int){
        val maxCells=records.maxOfOrNull{it.snapshot.cells.size}?:0
        ZipOutputStream(out).use { z ->
            put(z,"[Content_Types].xml",contentTypes());put(z,"_rels/.rels",rels());put(z,"xl/workbook.xml",workbook());put(z,"xl/_rels/workbook.xml.rels",workbookRels());put(z,"xl/styles.xml",styles());put(z,"xl/worksheets/sheet1.xml",sheet1(records,maxCells));put(z,"xl/worksheets/sheet2.xml",sheet2(records,intervalSec))
        }
    }
    private fun put(z:ZipOutputStream,name:String,text:String){z.putNextEntry(ZipEntry(name));z.write(text.toByteArray(Charsets.UTF_8));z.closeEntry()}
    private fun esc(s:String)=s.replace("&","&amp;").replace("<","&lt;").replace(">","&gt;").replace("\"","&quot;")
    private fun c(ref:String,v:String)="<c r=\"$ref\" t=\"inlineStr\"><is><t>${esc(v)}</t></is></c>"
    private fun n(ref:String,v:Number)="<c r=\"$ref\"><v>$v</v></c>"
    private fun col(i:Int):String{var x=i+1;var s="";while(x>0){x--;s=('A'.code+x%26).toChar()+s;x/=26};return s}
    private fun sheet1(records:List<MonitorRecord>,maxCells:Int):String{
        val headers=mutableListOf("时间","蓝牙名称","总压(V)","电流(A)","SOC(%)","SOH(%)","工作状态","剩余容量(Ah)","满充容量(Ah)","循环次数","最高温度(℃)","最低温度(℃)","MOS温度(℃)","最高单体(mV)","最低单体(mV)","压差(mV)","充电MOS","放电MOS","加热","制冷","均衡","一级告警","二级告警","三级保护","硬件版本","软件版本","BMS序列号")
        repeat(maxCells){headers+="单体${it+1}(mV)"}
        val sb=StringBuilder("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews><sheetData>")
        sb.append("<row r=\"1\">");headers.forEachIndexed{i,h->sb.append(c("${col(i)}1",h))};sb.append("</row>")
        val df=SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS",Locale.US)
        records.forEachIndexed{ri,r->val row=ri+2;val s=r.snapshot;val id=r.identity;val vals=mutableListOf<Any>(df.format(Date(s.timeMs)),id?.bluetoothName.orEmpty(),s.packVoltageV,s.currentA,s.soc,s.soh,s.workState,s.capacityNowAh,s.capacityFullAh,s.cycleCount,s.maxTempC,s.minTempC,s.mosTempC,s.maxCellMv,s.minCellMv,s.deltaMv,if(s.chargeMos)"开启"else"关闭",if(s.dischargeMos)"开启"else"关闭",if(s.heating)"开启"else"关闭",if(s.cooling)"开启"else"关闭",if(s.balancing)"开启"else"关闭",s.protectText(s.protect1),s.protectText(s.protect2),s.protectText(s.protect3),id?.hardware.orEmpty(),id?.software.orEmpty(),id?.serial.orEmpty());repeat(maxCells){vals+=s.cells.getOrElse(it){0}};sb.append("<row r=\"$row\">");vals.forEachIndexed{i,v->sb.append(if(v is Number)n("${col(i)}$row",v)else c("${col(i)}$row",v.toString()))};sb.append("</row>")}
        sb.append("</sheetData><autoFilter ref=\"A1:${col(headers.size-1)}${records.size+1}\"/></worksheet>");return sb.toString()
    }
    private fun sheet2(records:List<MonitorRecord>,interval:Int):String{val start=records.firstOrNull()?.snapshot?.timeMs;val end=records.lastOrNull()?.snapshot?.timeMs;val df=SimpleDateFormat("yyyy-MM-dd HH:mm:ss",Locale.US);val rows=listOf("记录条数" to records.size.toString(),"记录间隔(s)" to interval.toString(),"开始时间" to (start?.let{df.format(Date(it))}.orEmpty()),"结束时间" to (end?.let{df.format(Date(it))}.orEmpty()),"说明" to "仅记录有效BMS快照；断线期间不写入0值或重复假数据。");val sb=StringBuilder("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>");rows.forEachIndexed{i,p->val r=i+1;sb.append("<row r=\"$r\">${c("A$r",p.first)}${c("B$r",p.second)}</row>")};return sb.append("</sheetData></worksheet>").toString()}
    private fun contentTypes()="""<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/worksheets/sheet2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/></Types>"""
    private fun rels()="""<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>"""
    private fun workbook()="""<?xml version="1.0" encoding="UTF-8"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="电池长期监控" sheetId="1" r:id="rId1"/><sheet name="监控信息" sheetId="2" r:id="rId2"/></sheets></workbook>"""
    private fun workbookRels()="""<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet2.xml"/><Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/></Relationships>"""
    private fun styles()="""<?xml version="1.0" encoding="UTF-8"?><styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><fonts count="1"><font><sz val="11"/><name val="Calibri"/></font></fonts><fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills><borders count="1"><border/></borders><cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs><cellXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/></cellXfs></styleSheet>"""
}
