package com.example.bmsassistant.ble

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.*
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import java.io.IOException
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

class BmsBleSession(
    private val context: Context,
    private val device: BluetoothDevice,
    private val log:(String)->Unit,
    private val disconnected:()->Unit
): AutoCloseable {
    @Volatile private var gatt:BluetoothGatt?=null
    @Volatile private var tx:BluetoothGattCharacteristic?=null
    @Volatile private var rx:BluetoothGattCharacteristic?=null
    @Volatile private var connectLatch=CountDownLatch(1)
    @Volatile private var writeLatch:CountDownLatch?=null
    @Volatile private var writeStatus=BluetoothGatt.GATT_FAILURE
    @Volatile private var error:String?=null
    private val discoverStarted=AtomicBoolean(false)
    @Volatile var mtu:Int=23; private set
    @Volatile var ready:Boolean=false; private set
    var dataListener:((ByteArray)->Unit)?=null

    private fun hasConnectPermission() = Build.VERSION.SDK_INT < Build.VERSION_CODES.S || context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)==PackageManager.PERMISSION_GRANTED

    private val cb=object:BluetoothGattCallback(){
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(g:BluetoothGatt,status:Int,newState:Int){
            log("[BLE] state status=$status new=$newState")
            if(status!=BluetoothGatt.GATT_SUCCESS){ error="GATT连接错误 status=$status"; connectLatch.countDown(); if(ready) disconnected(); return }
            if(newState==BluetoothProfile.STATE_CONNECTED){
                val ok=try{g.requestMtu(247)}catch(_:Exception){false}
                if(!ok) discoverOnce(g) else Thread{Thread.sleep(600); discoverOnce(g)}.start()
            }else if(newState==BluetoothProfile.STATE_DISCONNECTED){
                val was=ready; ready=false; if(!was){error="建立BMS通道前BLE已断开";connectLatch.countDown()} else disconnected()
            }
        }
        @SuppressLint("MissingPermission")
        override fun onMtuChanged(g:BluetoothGatt,m:Int,status:Int){ if(status==BluetoothGatt.GATT_SUCCESS) mtu=m; log("[BLE] MTU=$mtu status=$status"); discoverOnce(g) }
        override fun onServicesDiscovered(g:BluetoothGatt,status:Int){
            if(status!=BluetoothGatt.GATT_SUCCESS){error="服务发现失败 status=$status";connectLatch.countDown();return}
            val svc=g.getService(SERVICE)
            tx=svc?.getCharacteristic(TX); rx=svc?.getCharacteristic(RX)
            if(tx==null||rx==null){error="未找到BMS SPP服务/特征";connectLatch.countDown();return}
            enableNotify(g,rx!!)
        }
        override fun onDescriptorWrite(g:BluetoothGatt,d:BluetoothGattDescriptor,status:Int){
            log("[BLE] CCCD status=$status")
            if(status==BluetoothGatt.GATT_SUCCESS){ready=true}else error="Notify订阅失败 status=$status"
            connectLatch.countDown()
        }
        @Deprecated("legacy") override fun onCharacteristicChanged(g:BluetoothGatt,c:BluetoothGattCharacteristic){ dataListener?.invoke(c.value?.clone()?:return) }
        override fun onCharacteristicChanged(g:BluetoothGatt,c:BluetoothGattCharacteristic,value:ByteArray){ dataListener?.invoke(value.clone()) }
        @Deprecated("legacy") override fun onCharacteristicWrite(g:BluetoothGatt,c:BluetoothGattCharacteristic,status:Int){writeStatus=status;writeLatch?.countDown()}
    }

    @SuppressLint("MissingPermission") private fun discoverOnce(g:BluetoothGatt){
        if(!discoverStarted.compareAndSet(false,true))return
        if(!hasConnectPermission()){error="缺少BLUETOOTH_CONNECT权限";connectLatch.countDown();return}
        log("[BLE] discoverServices")
        if(!g.discoverServices()){error="discoverServices被系统拒绝";connectLatch.countDown()}
    }

    @SuppressLint("MissingPermission") private fun enableNotify(g:BluetoothGatt,c:BluetoothGattCharacteristic){
        try{
            if(!g.setCharacteristicNotification(c,true)){error="setCharacteristicNotification失败";connectLatch.countDown();return}
            val d=c.getDescriptor(CCCD)?:run{error="RX无CCCD";connectLatch.countDown();return}
            val accepted=if(Build.VERSION.SDK_INT>=33) g.writeDescriptor(d,BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)==BluetoothStatusCodes.SUCCESS else { @Suppress("DEPRECATION") d.value=BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE; @Suppress("DEPRECATION") g.writeDescriptor(d) }
            if(!accepted){error="写CCCD被系统拒绝";connectLatch.countDown()}
        }catch(e:Exception){error="Notify配置失败: ${e.message}";connectLatch.countDown()}
    }

    @SuppressLint("MissingPermission") fun connect(timeoutMs:Long=15000){
        close(); connectLatch=CountDownLatch(1); discoverStarted.set(false); error=null; ready=false; mtu=23
        log("[BLE] connect ${device.address}")
        gatt=if(Build.VERSION.SDK_INT>=23) device.connectGatt(context,false,cb,BluetoothDevice.TRANSPORT_LE) else device.connectGatt(context,false,cb)
        if(gatt==null)throw IOException("connectGatt返回null")
        if(!connectLatch.await(timeoutMs,TimeUnit.MILLISECONDS)){close();throw IOException("BLE/GATT连接超时")}
        error?.let{close();throw IOException(it)}
        if(!ready){close();throw IOException("BMS GATT未就绪")}
        Thread.sleep(300)
    }

    @SuppressLint("MissingPermission") @Synchronized fun write(data:ByteArray,timeoutMs:Long=4000){
        val g=gatt?:throw IOException("BLE未连接"); val c=tx?:throw IOException("BMS TX未就绪")
        val supportsWrite=(c.properties and BluetoothGattCharacteristic.PROPERTY_WRITE)!=0
        val type=if(supportsWrite) BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT else BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
        val latch=if(type==BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)CountDownLatch(1) else null; writeLatch=latch; writeStatus=BluetoothGatt.GATT_FAILURE
        val ok=if(Build.VERSION.SDK_INT>=33) g.writeCharacteristic(c,data,type)==BluetoothStatusCodes.SUCCESS else { @Suppress("DEPRECATION") c.writeType=type; @Suppress("DEPRECATION") c.value=data; @Suppress("DEPRECATION") g.writeCharacteristic(c) }
        if(!ok){writeLatch=null;throw IOException("Android BLE栈拒绝写入")}
        if(latch!=null){if(!latch.await(timeoutMs,TimeUnit.MILLISECONDS)){writeLatch=null;throw IOException("GATT Write With Response超时")};writeLatch=null;if(writeStatus!=BluetoothGatt.GATT_SUCCESS)throw IOException("GATT写失败 status=$writeStatus")}
    }

    @SuppressLint("MissingPermission") override fun close(){ready=false;tx=null;rx=null;dataListener=null;try{gatt?.disconnect()}catch(_:Exception){};try{gatt?.close()}catch(_:Exception){};gatt=null}

    companion object{
        val SERVICE:UUID=UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e")
        val TX:UUID=UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e")
        val RX:UUID=UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e")
        val CCCD:UUID=UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}
