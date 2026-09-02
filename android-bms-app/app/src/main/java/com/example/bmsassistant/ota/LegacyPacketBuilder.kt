package com.example.bmsassistant.ota

object LegacyPacketBuilder {
    fun buildStart()=byteArrayOf(0x01,0xFF.toByte())
    fun buildData(index:Int,image:ByteArray,offset:Int):ByteArray{
        require(index in 0..0xFFFF)
        val p=ByteArray(20)
        p[0]=index.toByte();p[1]=(index shr 8).toByte()
        for(i in 0 until 16)p[2+i]=if(offset+i<image.size)image[offset+i] else 0xFF.toByte()
        val crc=TelinkCrc16.compute(p,18);p[18]=crc.toByte();p[19]=(crc shr 8).toByte();return p
    }
    fun buildEnd(lastIndex:Int):ByteArray{
        require(lastIndex in 0..0xFFFF)
        val xor=lastIndex xor 0xFFFF
        return byteArrayOf(0x02,0xFF.toByte(),lastIndex.toByte(),(lastIndex shr 8).toByte(),xor.toByte(),(xor shr 8).toByte())
    }
}
