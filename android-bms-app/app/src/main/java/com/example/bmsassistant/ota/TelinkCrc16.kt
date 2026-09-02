package com.example.bmsassistant.ota

object TelinkCrc16 {
    fun compute(data:ByteArray,length:Int=data.size):Int{
        var crc=0xFFFF
        for(i in 0 until length){crc=crc xor (data[i].toInt() and 0xFF);repeat(8){crc=if((crc and 1)!=0)(crc ushr 1) xor 0xA001 else crc ushr 1}}
        return crc and 0xFFFF
    }
}
