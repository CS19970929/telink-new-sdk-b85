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

class BleOtaSession(private val context:Context,private val device:BluetoothDevice,private val log:(String)->Unit):AutoCloseable{
    @Volatile private var gatt:BluetoothGatt?=null
    @Volatile private var chr:BluetoothGattCharacteristic?=null
    @Volatile private var latch=CountDownLatch(1)
    @Volatile private var writeLatch:CountDownLatch?=null
    @Volatile private var writeStatus=BluetoothGatt.GATT_FAILURE
    @Volatile private var error:String?=null
    private val discovery=AtomicBoolean(false)
    @Volatile var mtu=23;private set
    @Volatile var notificationsEnabled=false;private set
    var notificationListener:((ByteArray)->Unit)?=null
    private fun permission()=Build.VERSION.SDK_INT<31||context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)==PackageManager.PERMISSION_GRANTED
    private val cb=object:BluetoothGattCallback(){
        @SuppressLint("MissingPermission") override fun onConnectionStateChange(g:BluetoothGatt,status:Int,newState:Int){if(status!=BluetoothGatt.GATT_SUCCESS){error="OTA GATT error $status";latch.countDown();return};if(newState==BluetoothProfile.STATE_CONNECTED){val ok=try{g.requestMtu(247)}catch(_:Exception){false};if(!ok)discover(g)else Thread{Thread.sleep(700);discover(g)}.start()}else if(newState==BluetoothProfile.STATE_DISCONNECTED&&chr==null){error="OTA ready前断开";latch.countDown()}}
        override fun onMtuChanged(g:BluetoothGatt,m:Int,status:Int){if(status==BluetoothGatt.GATT_SUCCESS)mtu=m;discover(g)}
        override fun onServicesDiscovered(g:BluetoothGatt,status:Int){if(status!=BluetoothGatt.GATT_SUCCESS){error="OTA服务发现失败 $status";latch.countDown();return};val c=g.getService(SERVICE)?.getCharacteristic(CHARACTERISTIC)?:g.services.asSequence().mapNotNull{it.getCharacteristic(CHARACTERISTIC)}.firstOrNull();if(c==null){error="未找到Telink OTA特征";latch.countDown();return};chr=c;enableNotify(g,c);latch.countDown()}
        @Deprecated("legacy") override fun onCharacteristicChanged(g:BluetoothGatt,c:BluetoothGattCharacteristic){notificationListener?.invoke(c.value?.clone()?:return)}
        override fun onCharacteristicChanged(g:BluetoothGatt,c:BluetoothGattCharacteristic,value:ByteArray){notificationListener?.invoke(value.clone())}
        @Deprecated("legacy") override fun onCharacteristicWrite(g:BluetoothGatt,c:BluetoothGattCharacteristic,status:Int){writeStatus=status;writeLatch?.countDown()}
    }
    @SuppressLint("MissingPermission") private fun discover(g:BluetoothGatt){if(!discovery.compareAndSet(false,true))return;if(!permission()){error="缺少蓝牙权限";latch.countDown();return};if(!g.discoverServices()){error="OTA discoverServices rejected";latch.countDown()}}
    @SuppressLint("MissingPermission") private fun enableNotify(g:BluetoothGatt,c:BluetoothGattCharacteristic){if((c.properties and BluetoothGattCharacteristic.PROPERTY_NOTIFY)==0)return;try{val local=g.setCharacteristicNotification(c,true);val d=c.getDescriptor(CCCD);if(local&&d!=null){val ok=if(Build.VERSION.SDK_INT>=33)g.writeDescriptor(d,BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)==BluetoothStatusCodes.SUCCESS else {@Suppress("DEPRECATION") d.value=BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE;@Suppress("DEPRECATION") g.writeDescriptor(d)};notificationsEnabled=ok}}catch(_:Exception){notificationsEnabled=false}}
    @SuppressLint("MissingPermission") fun connect(timeoutMs:Long=15000){close();latch=CountDownLatch(1);discovery.set(false);error=null;chr=null;mtu=23;notificationsEnabled=false;gatt=if(Build.VERSION.SDK_INT>=23)device.connectGatt(context,false,cb,BluetoothDevice.TRANSPORT_LE)else device.connectGatt(context,false,cb);if(gatt==null)throw IOException("connectGatt=null");if(!latch.await(timeoutMs,TimeUnit.MILLISECONDS)){close();throw IOException("OTA连接超时")};error?.let{close();throw IOException(it)};if(chr==null)throw IOException("OTA特征未就绪");log("OTA ready MTU=$mtu notify=$notificationsEnabled")}
    @SuppressLint("MissingPermission") @Synchronized fun write(data:ByteArray,timeoutMs:Long=5000){val g=gatt?:throw IOException("OTA未连接");val c=chr?:throw IOException("OTA特征未就绪");val noRsp=(c.properties and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE)!=0;val type=if(noRsp)BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE else BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT;val wl=if(noRsp)null else CountDownLatch(1);writeLatch=wl;writeStatus=BluetoothGatt.GATT_FAILURE;var ok=false;repeat(if(noRsp)8 else 1){if(!ok){ok=if(Build.VERSION.SDK_INT>=33)g.writeCharacteristic(c,data,type)==BluetoothStatusCodes.SUCCESS else {@Suppress("DEPRECATION") c.writeType=type;@Suppress("DEPRECATION") c.value=data;@Suppress("DEPRECATION") g.writeCharacteristic(c)};if(!ok&&noRsp)Thread.sleep(2)}};if(!ok)throw IOException("OTA写被系统拒绝");if(wl!=null){if(!wl.await(timeoutMs,TimeUnit.MILLISECONDS))throw IOException("OTA写响应超时");if(writeStatus!=BluetoothGatt.GATT_SUCCESS)throw IOException("OTA写失败 $writeStatus")}}
    @SuppressLint("MissingPermission") override fun close(){chr=null;notificationListener=null;notificationsEnabled=false;try{gatt?.disconnect()}catch(_:Exception){};try{gatt?.close()}catch(_:Exception){};gatt=null}
    companion object{val SERVICE:UUID=UUID.fromString("00010203-0405-0607-0809-0a0b0c0d1912");val CHARACTERISTIC:UUID=UUID.fromString("00010203-0405-0607-0809-0a0b0c0d2b12");val CCCD:UUID=UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")}
}
