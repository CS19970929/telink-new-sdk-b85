package com.example.bmsassistant.bms

import com.example.bmsassistant.ble.BmsBleSession
import com.example.bmsassistant.model.BatterySnapshot
import com.example.bmsassistant.model.DeviceIdentity
import java.io.IOException
import java.nio.charset.StandardCharsets
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class BmsClient(private val session:BmsBleSession, private val log:(String)->Unit):AutoCloseable {
    private val lock=Any(); private val rx=ArrayList<Byte>(); @Volatile private var pending:CountDownLatch?=null; @Volatile private var response:ByteArray?=null; @Volatile private var responseError:Exception?=null
    init { session.dataListener={onFragment(it)} }

    fun probe(){
        var last:Exception?=null
        repeat(3){ i->
            try{ val w=readRegisters(BmsRegisters.REALTIME,2,1800); if(w[0]!=BmsRegisters.REALTIME_MAGIC)throw IOException("实时窗口Magic错误");log("[MODBUS] PROBE ${i+1}/3 OK");return }
            catch(e:Exception){last=e;log("[MODBUS] PROBE ${i+1}/3 FAIL ${e.message}");if(i<2)Thread.sleep(250)}
        }
        throw IOException("BMS Modbus Probe连续失败",last)
    }

    @Synchronized fun transact(req:ByteArray,timeoutMs:Long=4000):ByteArray{
        synchronized(lock){rx.clear();response=null;responseError=null;pending=CountDownLatch(1)}
        log("TX ${Modbus.hex(req)}")
        session.write(req)
        val l=pending!!
        if(!l.await(timeoutMs,TimeUnit.MILLISECONDS)){val b=synchronized(lock){rx.toByteArray()}; synchronized(lock){pending=null;rx.clear()}; throw IOException("Modbus响应超时 buffered=${Modbus.hex(b)}")}
        synchronized(lock){pending=null;responseError?.let{throw it}; return response?:throw IOException("Modbus无响应")}
    }

    fun readRegisters(start:Int,qty:Int,timeoutMs:Long=4000)=Modbus.parseRead(transact(Modbus.readHolding(start,qty),timeoutMs),qty)
    fun writeSingle(reg:Int,value:Int){val r=transact(Modbus.writeSingle(reg,value));Modbus.validateWriteSingle(r,reg,value)}
    fun writeSingleVerify(reg:Int,value:Int):Int{writeSingle(reg,value);val rb=readRegisters(reg,1)[0];if(rb!=value)throw IOException("写后回读不一致");return rb}
    fun writeMultiple(reg:Int,values:IntArray){val r=transact(Modbus.writeMultiple(reg,values));Modbus.validateWriteMultiple(r,reg,values.size)}

    fun readIdentity():DeviceIdentity{
        val mac=readRegisters(BmsRegisters.MAC,3);val sn=readRegisters(BmsRegisters.SERIAL,16);val hw=readRegisters(BmsRegisters.HARDWARE,16);val sw=readRegisters(BmsRegisters.SOFTWARE,16);val name=readRegisters(BmsRegisters.BT_NAME,12)
        val mb=mac.flatMap{listOf((it shr 8)and 0xFF,it and 0xFF)}.take(6).joinToString(":"){"%02X".format(it)}
        return DeviceIdentity(mb,Modbus.decodeAscii(sn),Modbus.decodeAscii(hw),Modbus.decodeAscii(sw),Modbus.decodeAscii(name))
    }

    fun readBluetoothName()=Modbus.decodeAscii(readRegisters(BmsRegisters.BT_NAME,12))
    fun writeBluetoothName(suffixInput:String):String{
        var suffix=suffixInput.trim();if(suffix.startsWith("BT_",true))suffix=suffix.substring(3)
        require(suffix.isNotEmpty()){ "蓝牙名后缀不能为空" }; require(suffix.toByteArray(StandardCharsets.US_ASCII).size<=10){"蓝牙名后缀最多10字节"};require(suffix.all{it.isLetterOrDigit()||it=='_'||it=='-'}){"仅支持字母、数字、_、-"}
        var raw=suffix.toByteArray(StandardCharsets.US_ASCII);if(raw.size%2!=0)raw=raw+byteArrayOf(0)
        val words=IntArray(raw.size/2){i->((raw[i*2].toInt()and 0xFF)shl 8)or(raw[i*2+1].toInt()and 0xFF)}
        writeMultiple(BmsRegisters.BT_NAME,words);val rb=readBluetoothName();val exp="BT_$suffix";if(rb!=exp)throw IOException("蓝牙名回读不一致: $rb");return rb
    }

    fun readBattery():BatterySnapshot{
        val l=readRegisters(BmsRegisters.LEGACY,63);val st=readRegisters(BmsRegisters.SYSTEM_STATUS,2);val rt=readRegisters(BmsRegisters.REALTIME,11);val useRt=rt[0]==BmsRegisters.REALTIME_MAGIC
        val rawCurrent=if((l[51].toShort()).toInt()>0)-l[51].toShort().toInt() else l[50].toShort().toInt()
        val voltage=if(useRt)rt[2]else l[37];val cur=if(useRt)rt[3].toShort().toInt()else rawCurrent;val soc=if(useRt)rt[4]else l[52];val maxT=if(useRt)rt[5]else l[48];val minT=if(useRt)rt[6]else l[49];val mosT=if(useRt)rt[7]else l[47];val maxCell=if(useRt)rt[8]else l[32];val minCell=if(useRt)rt[9]else l[33];val delta=if(useRt)rt[10]else l[36]
        val candidates=l.take(32);val cells=ArrayList<Int>();for(v in candidates){if(v in 1000..5000)cells+=v else if(cells.isNotEmpty())break};if(cells.isEmpty())cells+=candidates.filter{it>0}
        return BatterySnapshot(packVoltageV=voltage/100.0,currentA=cur/10.0,soc=soc,soh=l[53],maxTempC=maxT/10.0-40,minTempC=minT/10.0-40,mosTempC=mosT/10.0-40,maxCellMv=maxCell,minCellMv=minCell,deltaMv=delta,maxCellPos=l[34],minCellPos=l[35],cycleCount=l[57],capacityNowAh=l[54]/100.0,capacityFullAh=l[55]/100.0,capacityFactoryAh=l[56]/100.0,systemStatus=(st[0].toLong()and 0xFFFF)or((st[1].toLong()and 0xFFFF)shl 16),protect1=l[58],protect2=l[59],protect3=l[60],cells=cells)
    }

    private fun onFragment(f:ByteArray){
        synchronized(lock){if(pending==null){log("[MODBUS] unsolicited ${Modbus.hex(f)}");return};f.forEach{rx+=it};val expected=Modbus.inferLength(rx);log("[MODBUS] RX fragment=${f.size} total=${rx.size} expected=${expected?:-1}");if(expected!=null&&rx.size>=expected){val frame=rx.take(expected).toByteArray();try{Modbus.validate(frame);response=frame;log("RX ${Modbus.hex(frame)}")}catch(e:Exception){responseError=e};pending?.countDown()}}
    }
    override fun close(){session.dataListener=null}
}
