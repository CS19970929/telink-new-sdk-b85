package com.example.bmsassistant.ota

import com.example.bmsassistant.ble.BleOtaSession
import java.io.IOException

class TelinkOtaController(
    private val session:BleOtaSession,
    private val log:(String)->Unit,
    private val progress:(Int,Int,Int,Double)->Unit
){
    @Volatile private var result:Int?=null
    fun upgrade(image:FirmwareImage){
        result=null
        session.notificationListener={d->if(d.size>=3&&d[0]==0x06.toByte()&&d[1]==0xFF.toByte()){result=d[2].toInt()and 0xFF;log("OTA_RESULT=0x%02X".format(result))}}
        try{
            session.write(LegacyPacketBuilder.buildStart())
            val start=System.nanoTime();var last=0L
            for(i in 0 until image.packetCount){
                result?.let{if(it!=0)throw IOException("OTA被拒绝 result=0x%02X".format(it))}
                session.write(LegacyPacketBuilder.buildData(i,image.bytes,i*FirmwareImage.PAYLOAD_SIZE))
                val now=System.nanoTime();val sent=minOf((i+1)*16,image.imageSize)
                if(i==image.packetCount-1||now-last>=100_000_000L){last=now;progress(i+1,image.packetCount,sent,sent/((now-start).coerceAtLeast(1)/1e9))}
                if((i and 63)==63)Thread.yield()
            }
            session.write(LegacyPacketBuilder.buildEnd(image.packetCount-1))
            if(session.notificationsEnabled){val dl=System.nanoTime()+2_000_000_000L;while(result==null&&System.nanoTime()<dl)Thread.sleep(20);result?.let{if(it!=0)throw IOException("OTA_END失败 result=0x%02X".format(it))}}
            log("OTA发送完成，等待设备校验并重启")
        }finally{session.notificationListener=null}
    }
}
