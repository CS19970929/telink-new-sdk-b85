package com.cjsh.bmsassistant.feature

import com.cjsh.bmsassistant.model.MonitorRecord
import java.io.OutputStream
import java.io.OutputStreamWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

object XlsxWriter {
    fun write(output: OutputStream, records: List<MonitorRecord>, intervalSeconds: Int) {
        ZipOutputStream(output).use { zip ->
            entry(zip, "[Content_Types].xml", contentTypes())
            entry(zip, "_rels/.rels", rootRels())
            entry(zip, "xl/workbook.xml", workbook())
            entry(zip, "xl/_rels/workbook.xml.rels", workbookRels())
            entry(zip, "xl/styles.xml", styles())
            writeMonitorSheet(zip, records)
            writeInfoSheet(zip, records, intervalSeconds)
        }
    }

    private fun writeMonitorSheet(zip: ZipOutputStream, records: List<MonitorRecord>) {
        val maxCells = records.maxOfOrNull { it.snapshot.cellMillivolts.size } ?: 0
        val headers = mutableListOf(
            "时间", "蓝牙名称", "总压(V)", "电流(A)", "SOC(%)", "SOH(%)", "工作状态",
            "剩余容量(Ah)", "满充容量(Ah)", "循环次数", "最高温度(℃)", "最低温度(℃)", "MOS温度(℃)",
            "最高单体(mV)", "最低单体(mV)", "压差(mV)", "充电MOS", "放电MOS", "加热", "制冷", "均衡",
            "一级告警", "二级告警", "三级保护", "硬件版本", "软件版本", "BMS序列号"
        )
        for (i in 1..maxCells) headers.add("单体${i}(mV)")
        zip.putNextEntry(ZipEntry("xl/worksheets/sheet1.xml"))
        val w = OutputStreamWriter(zip, Charsets.UTF_8)
        w.write("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
        w.write("<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">")
        w.write("<sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews>")
        w.write("<sheetData>")
        writeRow(w, 1, headers.map { Cell.Str(it, true) })
        val fmt = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US)
        records.forEachIndexed { idx, r ->
            val s = r.snapshot
            val id = r.identity
            val cells = mutableListOf<Cell>(
                Cell.Str(fmt.format(Date(r.timeMillis))), Cell.Str(r.bluetoothName),
                Cell.Num(s.packVoltageV), Cell.Num(s.currentA), Cell.Num(s.socPercent), Cell.Num(s.sohPercent), Cell.Str(s.workState),
                Cell.Num(s.capacityNowAh), Cell.Num(s.capacityFullAh), Cell.Num(s.cycleCount), Cell.Num(s.maxTempC), Cell.Num(s.minTempC), Cell.Num(s.mosTempC),
                Cell.Num(s.maxCellMv), Cell.Num(s.minCellMv), Cell.Num(s.cellDeltaMv),
                Cell.Str(onOff(s.chargeMosOn)), Cell.Str(onOff(s.dischargeMosOn)), Cell.Str(onOff(s.heatingOn)), Cell.Str(onOff(s.coolingOn)), Cell.Str(onOff(s.balancingOn)),
                Cell.Str(s.protectionLevel1Text), Cell.Str(s.protectionLevel2Text), Cell.Str(s.protectionLevel3Text),
                Cell.Str(id?.hardware ?: ""), Cell.Str(id?.software ?: ""), Cell.Str(id?.serial ?: "")
            )
            for (i in 0 until maxCells) cells.add(if (i < s.cellMillivolts.size) Cell.Num(s.cellMillivolts[i]) else Cell.Str(""))
            writeRow(w, idx + 2, cells)
        }
        w.write("</sheetData><autoFilter ref=\"A1:${columnName(headers.size)}${records.size + 1}\"/></worksheet>")
        w.flush()
        zip.closeEntry()
    }

    private fun writeInfoSheet(zip: ZipOutputStream, records: List<MonitorRecord>, intervalSeconds: Int) {
        zip.putNextEntry(ZipEntry("xl/worksheets/sheet2.xml"))
        val w = OutputStreamWriter(zip, Charsets.UTF_8)
        w.write("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>")
        w.write("<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>")
        val fmt = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US)
        val rows = listOf(
            "项目" to "内容",
            "记录条数" to records.size.toString(),
            "记录间隔" to "${intervalSeconds}秒",
            "开始时间" to (records.firstOrNull()?.let { fmt.format(Date(it.timeMillis)) } ?: ""),
            "结束时间" to (records.lastOrNull()?.let { fmt.format(Date(it.timeMillis)) } ?: ""),
            "说明" to "仅记录成功读取的BMS快照；断线期间不写0、不伪造重复数据，重连成功后继续记录。"
        )
        rows.forEachIndexed { i, p -> writeRow(w, i + 1, listOf(Cell.Str(p.first, i == 0), Cell.Str(p.second, i == 0))) }
        w.write("</sheetData></worksheet>")
        w.flush(); zip.closeEntry()
    }

    private sealed class Cell {
        data class Str(val value:String, val header:Boolean=false):Cell()
        data class Num(val value:Number):Cell()
    }

    private fun writeRow(w:OutputStreamWriter, row:Int, cells:List<Cell>) {
        w.write("<row r=\"$row\">")
        cells.forEachIndexed { i, c ->
            val ref = "${columnName(i + 1)}$row"
            when(c) {
                is Cell.Str -> w.write("<c r=\"$ref\" t=\"inlineStr\"${if(c.header) " s=\"1\"" else ""}><is><t>${esc(c.value)}</t></is></c>")
                is Cell.Num -> w.write("<c r=\"$ref\"><v>${c.value}</v></c>")
            }
        }
        w.write("</row>")
    }

    private fun columnName(index:Int):String {
        var n=index; val sb=StringBuilder()
        while(n>0){ val r=(n-1)%26; sb.append(('A'.code+r).toChar()); n=(n-1)/26 }
        return sb.reverse().toString()
    }
    private fun onOff(v:Boolean)=if(v) "开启" else "关闭"
    private fun esc(s:String)=s.replace("&","&amp;").replace("<","&lt;").replace(">","&gt;").replace("\"","&quot;").replace("'","&apos;")
    private fun entry(zip:ZipOutputStream,name:String,text:String){ zip.putNextEntry(ZipEntry(name)); zip.write(text.toByteArray(Charsets.UTF_8)); zip.closeEntry() }

    private fun contentTypes() = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/worksheets/sheet2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/></Types>"""
    private fun rootRels() = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>"""
    private fun workbook() = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="电池长期监控" sheetId="1" r:id="rId1"/><sheet name="监控信息" sheetId="2" r:id="rId2"/></sheets></workbook>"""
    private fun workbookRels() = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet2.xml"/><Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/></Relationships>"""
    private fun styles() = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?><styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><fonts count="2"><font><sz val="11"/><name val="Calibri"/></font><font><b/><sz val="11"/><name val="Calibri"/></font></fonts><fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills><borders count="1"><border><left/><right/><top/><bottom/><diagonal/></border></borders><cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs><cellXfs count="2"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/><xf numFmtId="0" fontId="1" fillId="0" borderId="0" xfId="0" applyFont="1"/></cellXfs></styleSheet>"""
}
