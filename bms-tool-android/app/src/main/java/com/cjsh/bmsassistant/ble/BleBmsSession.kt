package com.cjsh.bmsassistant.ble

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import java.io.IOException
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

class BleBmsSession(
    private val context: Context,
    private val log: (String, String) -> Unit
) : AutoCloseable {
    @Volatile private var gatt: BluetoothGatt? = null
    @Volatile private var bmsTx: BluetoothGattCharacteristic? = null
    @Volatile private var bmsRx: BluetoothGattCharacteristic? = null
    @Volatile private var otaChar: BluetoothGattCharacteristic? = null
    @Volatile private var servicesLatch = CountDownLatch(1)
    @Volatile private var serviceError: String? = null
    @Volatile private var descriptorLatch: CountDownLatch? = null
    @Volatile private var descriptorStatus = BluetoothGatt.GATT_FAILURE
    @Volatile private var writeLatch: CountDownLatch? = null
    @Volatile private var writeStatus = BluetoothGatt.GATT_FAILURE
    @Volatile private var closing = true
    @Volatile var negotiatedMtu = 23
        private set
    @Volatile var device: BluetoothDevice? = null
        private set

    private val discoveryStarted = AtomicBoolean(false)
    private val gattOpLock = Any()

    var bmsNotificationListener: ((ByteArray) -> Unit)? = null
    var otaNotificationListener: ((ByteArray) -> Unit)? = null
    var unexpectedDisconnectListener: (() -> Unit)? = null

    val hasOtaCharacteristic: Boolean get() = otaChar != null

    private fun hasConnectPermission(): Boolean = Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
        context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED

    private val callback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            log("BLE", "STATE_CHANGE status=$status newState=$newState closing=$closing")
            if (status != BluetoothGatt.GATT_SUCCESS) {
                serviceError = "GATT connection status=$status"
                servicesLatch.countDown()
                if (!closing) unexpectedDisconnectListener?.invoke()
                return
            }
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    log("BLE", "CONNECTED name='${safeName(g.device)}' address=${g.device.address}; requesting MTU=247")
                    val accepted = try { g.requestMtu(247) } catch (_: Exception) { false }
                    if (!accepted) discoverOnce(g)
                    else Thread {
                        Thread.sleep(800)
                        discoverOnce(g)
                    }.start()
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    log("BLE", "DISCONNECTED address=${g.device.address}; closing=$closing")
                    if (!closing) unexpectedDisconnectListener?.invoke()
                }
            }
        }

        @SuppressLint("MissingPermission")
        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS && mtu >= 23) negotiatedMtu = mtu
            log("BLE", "MTU status=$status mtu=$mtu effective=$negotiatedMtu")
            discoverOnce(g)
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            log("BLE", "SERVICES_DISCOVERED status=$status count=${g.services?.size ?: 0}")
            if (status != BluetoothGatt.GATT_SUCCESS) {
                serviceError = "Service discovery failed status=$status"
                servicesLatch.countDown()
                return
            }
            val bmsService = g.getService(BMS_SERVICE_UUID)
            bmsTx = bmsService?.getCharacteristic(BMS_TX_UUID)
            bmsRx = bmsService?.getCharacteristic(BMS_RX_UUID)

            val officialOta = g.getService(OTA_SERVICE_UUID)?.getCharacteristic(OTA_CHAR_UUID)
            otaChar = officialOta ?: g.services.asSequence().mapNotNull { it.getCharacteristic(OTA_CHAR_UUID) }.firstOrNull()

            if (bmsTx == null || bmsRx == null) {
                serviceError = "BMS UART GATT service/characteristics not found"
            } else {
                log("BLE", "BMS_GATT_READY service=$BMS_SERVICE_UUID tx=${bmsTx!!.uuid} rx=${bmsRx!!.uuid} ota=${otaChar != null}")
            }
            servicesLatch.countDown()
        }

        @Deprecated("Required on pre-API33 Android")
        override fun onCharacteristicChanged(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            routeNotification(characteristic.uuid, characteristic.value?.clone() ?: return)
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
            routeNotification(characteristic.uuid, value.clone())
        }

        @Deprecated("Required for legacy Android callback")
        override fun onCharacteristicWrite(g: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            writeStatus = status
            writeLatch?.countDown()
        }

        @Deprecated("Required for legacy Android callback")
        override fun onDescriptorWrite(g: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            descriptorStatus = status
            descriptorLatch?.countDown()
        }
    }

    private fun routeNotification(uuid: UUID, value: ByteArray) {
        when (uuid) {
            BMS_RX_UUID -> bmsNotificationListener?.invoke(value)
            OTA_CHAR_UUID -> otaNotificationListener?.invoke(value)
            else -> log("BLE", "UNROUTED_NOTIFY uuid=$uuid len=${value.size}")
        }
    }

    @SuppressLint("MissingPermission")
    private fun discoverOnce(g: BluetoothGatt) {
        if (!discoveryStarted.compareAndSet(false, true)) return
        if (!hasConnectPermission()) {
            serviceError = "BLUETOOTH_CONNECT permission missing"
            servicesLatch.countDown()
            return
        }
        try {
            log("BLE", "DISCOVER_SERVICES begin")
            if (!g.discoverServices()) {
                serviceError = "discoverServices() rejected by Android BLE stack"
                servicesLatch.countDown()
            }
        } catch (e: Exception) {
            serviceError = "discoverServices failed: ${e.message}"
            servicesLatch.countDown()
        }
    }

    @SuppressLint("MissingPermission")
    fun connect(target: BluetoothDevice, timeoutMs: Long = 15_000) {
        closeInternal(true)
        if (!hasConnectPermission()) throw IOException("BLUETOOTH_CONNECT permission missing")
        device = target
        closing = false
        negotiatedMtu = 23
        bmsTx = null; bmsRx = null; otaChar = null
        serviceError = null
        servicesLatch = CountDownLatch(1)
        discoveryStarted.set(false)
        val started = System.nanoTime()
        log("CONNECT", "STEP_BEGIN connectGatt address=${target.address}")
        gatt = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            target.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE)
        } else {
            @Suppress("DEPRECATION") target.connectGatt(context, false, callback)
        }
        if (gatt == null) throw IOException("connectGatt returned null")
        if (!servicesLatch.await(timeoutMs, TimeUnit.MILLISECONDS)) {
            closeInternal(true)
            throw IOException("Timed out waiting for BLE/GATT service discovery")
        }
        serviceError?.let { err -> closeInternal(true); throw IOException(err) }
        if (bmsTx == null || bmsRx == null) {
            closeInternal(true)
            throw IOException("BLE connected but BMS GATT path is incomplete")
        }
        enableNotification(bmsRx!!, "BMS_RX")
        Thread.sleep(350)
        val elapsed = (System.nanoTime() - started) / 1_000_000
        log("CONNECT", "GATT_READY elapsed=${elapsed}ms mtu=$negotiatedMtu ota=${otaChar != null}")
    }

    @SuppressLint("MissingPermission")
    fun enableOtaNotifications(): Boolean {
        val ch = otaChar ?: return false
        return try {
            enableNotification(ch, "OTA")
            true
        } catch (e: Exception) {
            log("OTA", "OTA notify enable failed: ${e.message}")
            false
        }
    }

    @SuppressLint("MissingPermission")
    private fun enableNotification(ch: BluetoothGattCharacteristic, label: String) {
        val g = gatt ?: throw IOException("GATT not connected")
        if ((ch.properties and BluetoothGattCharacteristic.PROPERTY_NOTIFY) == 0) throw IOException("$label characteristic has no NOTIFY property")
        synchronized(gattOpLock) {
            if (!g.setCharacteristicNotification(ch, true)) throw IOException("setCharacteristicNotification($label) rejected")
            val cccd = ch.getDescriptor(CCCD_UUID) ?: throw IOException("$label CCCD 0x2902 not found")
            val latch = CountDownLatch(1)
            descriptorLatch = latch
            descriptorStatus = BluetoothGatt.GATT_FAILURE
            val accepted = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                g.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) == BluetoothStatusCodes.SUCCESS
            } else {
                @Suppress("DEPRECATION")
                run { cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE; g.writeDescriptor(cccd) }
            }
            if (!accepted) { descriptorLatch = null; throw IOException("CCCD write rejected for $label") }
            if (!latch.await(5, TimeUnit.SECONDS)) { descriptorLatch = null; throw IOException("CCCD write timeout for $label") }
            descriptorLatch = null
            if (descriptorStatus != BluetoothGatt.GATT_SUCCESS) throw IOException("CCCD write failed for $label status=$descriptorStatus")
            log("BLE", "CCCD_NOTIFY_OK label=$label")
        }
    }

    fun writeBms(data: ByteArray, timeoutMs: Long = 5_000) {
        val ch = bmsTx ?: throw IOException("BMS TX characteristic not ready")
        val withResponse = (ch.properties and BluetoothGattCharacteristic.PROPERTY_WRITE) != 0
        writeCharacteristic(ch, data, preferNoResponse = !withResponse, timeoutMs = timeoutMs)
    }

    fun writeOta(data: ByteArray, timeoutMs: Long = 5_000) {
        val ch = otaChar ?: throw IOException("Telink OTA characteristic not found")
        val noResponse = (ch.properties and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0
        writeCharacteristic(ch, data, preferNoResponse = noResponse, timeoutMs = timeoutMs)
    }

    @SuppressLint("MissingPermission")
    private fun writeCharacteristic(ch: BluetoothGattCharacteristic, data: ByteArray, preferNoResponse: Boolean, timeoutMs: Long) {
        val g = gatt ?: throw IOException("GATT not connected")
        synchronized(gattOpLock) {
            val noResponse = preferNoResponse && (ch.properties and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0
            val type = if (noResponse) BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE else BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            val latch = if (noResponse) null else CountDownLatch(1)
            writeLatch = latch
            writeStatus = BluetoothGatt.GATT_FAILURE
            var accepted = false
            val attempts = if (noResponse) 8 else 1
            for (attempt in 0 until attempts) {
                accepted = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    g.writeCharacteristic(ch, data, type) == BluetoothStatusCodes.SUCCESS
                } else {
                    @Suppress("DEPRECATION")
                    run { ch.writeType = type; ch.value = data; g.writeCharacteristic(ch) }
                }
                if (accepted) break
                if (noResponse && attempt < attempts - 1) Thread.sleep(2)
            }
            if (!accepted) { writeLatch = null; throw IOException("Android BLE stack rejected characteristic write") }
            if (latch != null) {
                if (!latch.await(timeoutMs, TimeUnit.MILLISECONDS)) { writeLatch = null; throw IOException("GATT write response timeout") }
                writeLatch = null
                if (writeStatus != BluetoothGatt.GATT_SUCCESS) throw IOException("GATT write failed status=$writeStatus")
            }
            log("GATT", "WRITE_OK uuid=${ch.uuid} len=${data.size} option=${if (noResponse) "WriteWithoutResponse" else "WriteWithResponse"}")
        }
    }

    fun closeForReconnect() = closeInternal(true)

    @SuppressLint("MissingPermission")
    private fun closeInternal(intentional: Boolean) {
        closing = intentional
        descriptorLatch?.countDown(); descriptorLatch = null
        writeLatch?.countDown(); writeLatch = null
        try { gatt?.disconnect() } catch (_: Exception) { }
        try { gatt?.close() } catch (_: Exception) { }
        gatt = null
        bmsTx = null; bmsRx = null; otaChar = null
        device = null
    }

    override fun close() = closeInternal(true)

    @SuppressLint("MissingPermission")
    private fun safeName(device: BluetoothDevice): String = try { device.name ?: "" } catch (_: Exception) { "" }

    companion object {
        val BMS_SERVICE_UUID: UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
        val BMS_TX_UUID: UUID = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
        val BMS_RX_UUID: UUID = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
        val OTA_SERVICE_UUID: UUID = UUID.fromString("00010203-0405-0607-0809-0a0b0c0d1912")
        val OTA_CHAR_UUID: UUID = UUID.fromString("00010203-0405-0607-0809-0a0b0c0d2b12")
        private val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}
