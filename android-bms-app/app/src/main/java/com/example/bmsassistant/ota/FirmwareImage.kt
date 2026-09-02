package com.example.bmsassistant.ota

import java.nio.ByteBuffer
import java.nio.ByteOrder

class FirmwareImage private constructor(val sourceName:String,val bytes:ByteArray){
    val imageSize:Int get()=bytes.size
    val packetCount:Int get()=(imageSize+PAYLOAD_SIZE-1)/PAYLOAD_SIZE
    companion object{
        const val FIRMWARE_SIZE_OFFSET=0x18
        const val MIN_HEADER_LENGTH=0x1C
        const val PAYLOAD_SIZE=16
        fun fromBytes(sourceName:String,file:ByteArray,maxImageBytes:Int=0):FirmwareImage{
            require(maxImageBytes>=0)
            if(file.size<MIN_HEADER_LENGTH)throw IllegalArgumentException("BIN太小: ${file.size}")
            val declared=ByteBuffer.wrap(file,FIRMWARE_SIZE_OFFSET,4).order(ByteOrder.LITTLE_ENDIAN).int.toLong() and 0xFFFF_FFFFL
            if(declared==0L||declared>Int.MAX_VALUE)throw IllegalArgumentException("固件头size无效: $declared")
            if(declared>file.size)throw IllegalArgumentException("固件头size $declared 超过文件长度 ${file.size}")
            if(maxImageBytes>0&&declared>maxImageBytes)throw IllegalArgumentException("固件超过OTA槽大小")
            val packets=(declared.toInt()+PAYLOAD_SIZE-1)/PAYLOAD_SIZE
            if(packets !in 1..65536)throw IllegalArgumentException("Legacy OTA包数超限: $packets")
            return FirmwareImage(sourceName,file.copyOfRange(0,declared.toInt()))
        }
    }
}
