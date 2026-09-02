package com.cjsh.bmsassistant.ota

import com.cjsh.bmsassistant.ble.BleBmsSession
import com.cjsh.bmsassistant.protocol.ModbusRtu
import java.io.IOException
import java.util.concurrent.atomic.AtomicBoolean

class FirmwareImage private constructor(val bytes:ByteArray, val imageSize:Int, val packetCount:Int) {
    companion object {
        const val PAYLOAD_SIZE=16
        fun parse(fileBytes:ByteArray):FirmwareImage {
            require(fileBytes.size >= 0x1C) { "BIN过短，缺少Telink固件头" }
            val size = (fileBytes[0x18].toInt() and 0xFF) or
                ((fileBytes[0x19].toInt() and 0xFF) shl 8) or
                ((fileBytes[0x1A].toInt() and 0xFF) shl 16) or
                ((fileBytes[0x1B].toInt() and 0xFF) shl 24)
            require(size > 0 && size <= fileBytes.size) { "BIN声明固件长度无效：$size / ${fileBytes.size}" }
            val packets=(size + PAYLOAD_SIZE - 1)/PAYLOAD_SIZE
            require(packets in 1..65536) { "Legacy OTA包数量超出16位索引范围" }
            return FirmwareImage(fileBytes.copyOf(size),size,packets)
        }
    }
}

class TelinkOtaController(
    private val session:BleBmsSession,
    private val log:(String)->Unit,
    private val progress:(completed:Int,total:Int,imageBytes:Int,bytesPerSecond:Double)->Unit
) {
    private val cancelled=AtomicBoolean(false)
    @Volatile private var resultCode:Int?=null

    fun cancel(){ cancelled.set(true) }

    fun upgrade(image:FirmwareImage) {
        cancelled.set(false); resultCode=null
        session.otaNotificationListener={ onNotification(it) }
        val notify=session.enableOtaNotifications()
        try {
            checkCancel()
            log("Mode=LegacyFast16; MTU=${session.negotiatedMtu}; OTA_RESULT notify=$notify; packetDelay=0ms")
            session.writeOta(byteArrayOf(0x01,0xFF.toByte()))
            val started=System.nanoTime(); var lastProgress=0L
            for(i in 0 until image.packetCount) {
                checkCancel(); throwIfRejected()
                session.writeOta(buildData(i,image))
                val sent=minOf((i+1)*FirmwareImage.PAYLOAD_SIZE,image.imageSize)
                val now=System.nanoTime()
                if(i==image.packetCount-1 || lastProgress==0L || now-lastProgress>=100_000_000L) {
                    lastProgress=now
                    val sec=(now-started).coerceAtLeast(1L)/1_000_000_000.0
                    progress(i+1,image.packetCount,sent,sent/sec)
                }
                if(i==0 || i==image.packetCount-1 || (i+1)%256==0) log("DATA index=$i bytes=$sent/${image.imageSize}")
                if((i and 0x3F)==0x3F) Thread.yield()
            }
            throwIfRejected()
            val last=image.packetCount-1
            val inv=last xor 0xFFFF
            val end=byteArrayOf(0x02,0xFF.toByte(),last.toByte(),(last ushr 8).toByte(),inv.toByte(),(inv ushr 8).toByte())
            session.writeOta(end)
            if(notify) {
                val deadline=System.nanoTime()+2_000_000_000L
                while(resultCode==null && System.nanoTime()<deadline){ checkCancel(); Thread.sleep(20) }
                val code=resultCode
                if(code==null) log("OTA_RESULT timeout；数据发送完成，但服务器最终结果未确认")
                else if(code!=0) throw IOException("OTA_END rejected: 0x%02X %s".format(code,resultName(code)))
                else log("RX OTA_RESULT: 0x00 OTA_SUCCESS")
            } else log("OTA通知不可用；仅确认数据发送完成")
        } finally { session.otaNotificationListener=null }
    }

    private fun buildData(index:Int,image:FirmwareImage):ByteArray {
        val p=ByteArray(20){0xFF.toByte()}
        p[0]=index.toByte(); p[1]=(index ushr 8).toByte()
        val off=index*FirmwareImage.PAYLOAD_SIZE
        val len=minOf(FirmwareImage.PAYLOAD_SIZE,image.imageSize-off)
        image.bytes.copyInto(p,2,off,off+len)
        val crc=ModbusRtu.crc16(p,0,18)
        p[18]=crc.toByte(); p[19]=(crc ushr 8).toByte()
        return p
    }

    private fun onNotification(data:ByteArray) {
        if(data.size<3 || data[0]!=0x06.toByte() || data[1]!=0xFF.toByte()) return
        val code=data[2].toInt() and 0xFF
        resultCode=code
        log("RX OTA_RESULT: 0x%02X %s".format(code,resultName(code)))
    }
    private fun throwIfRejected(){ val c=resultCode ?: return; if(c!=0) throw IOException("OTA rejected: 0x%02X %s".format(c,resultName(c))) }
    private fun checkCancel(){ if(cancelled.get()) throw InterruptedException("OTA已取消，请从索引0重新升级") }
    private fun resultName(c:Int)=when(c){
        0x00->"OTA_SUCCESS";0x01->"OTA_DATA_PACKET_SEQ_ERR";0x02->"OTA_PACKET_INVALID";0x03->"OTA_DATA_CRC_ERR";
        0x04->"OTA_WRITE_FLASH_ERR";0x05->"OTA_DATA_INCOMPLETE";0x06->"OTA_FLOW_ERR";0x07->"OTA_FW_CHECK_ERR";
        0x08->"OTA_VERSION_COMPARE_ERR";0x09->"OTA_PDU_LEN_ERR";0x0A->"OTA_FIRMWARE_MARK_ERR";0x0B->"OTA_FW_SIZE_ERR";
        0x0C->"OTA_DATA_PACKET_TIMEOUT";0x0D->"OTA_TIMEOUT";0x0E->"OTA_CONNECTION_TERMINATE";0x0F->"OTA_MCU_NOT_SUPPORTED";
        0x10->"OTA_LOGIC_ERR";else->"OTA_RESULT_0x%02X".format(c)
    }
}
