package com.example.bmsota.ble

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

class BleOtaSession(
    private val context: Context,
    private val device: BluetoothDevice,
    private val log: (String) -> Unit
) : AutoCloseable {

    @Volatile private var gatt: BluetoothGatt? = null
    @Volatile private var otaCharacteristic: BluetoothGattCharacteristic? = null
    @Volatile private var connectError: String? = null
    @Volatile private var connectLatch = CountDownLatch(1)
    @Volatile private var writeLatch: CountDownLatch? = null
    @Volatile private var lastWriteStatus: Int = BluetoothGatt.GATT_FAILURE
    private val discoveryStarted = AtomicBoolean(false)

    @Volatile var negotiatedMtu: Int = 23
        private set
    @Volatile var notificationsEnabled: Boolean = false
        private set
    @Volatile var discoveryDescription: String = "OTA GATT not discovered"
        private set

    var notificationListener: ((ByteArray) -> Unit)? = null

    val isReady: Boolean get() = gatt != null && otaCharacteristic != null

    private fun hasConnectPermission(): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
            context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED

    private val callback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                connectError = "GATT connection error status=$status"
                connectLatch.countDown()
                return
            }
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    log("BLE connected; requesting MTU...")
                    val requested = try { gatt.requestMtu(247) } catch (_: Exception) { false }
                    if (!requested) {
                        discoverServicesOnce(gatt)
                    } else {
                        Thread {
                            Thread.sleep(800)
                            discoverServicesOnce(gatt)
                        }.start()
                    }
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    log("BLE disconnected")
                    if (otaCharacteristic == null) {
                        connectError = "BLE disconnected before OTA characteristic was ready"
                        connectLatch.countDown()
                    }
                }
            }
        }

        @SuppressLint("MissingPermission")
        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS && mtu >= 23) {
                negotiatedMtu = mtu
                log("MTU negotiated: $mtu")
            } else {
                log("MTU request not accepted; using $negotiatedMtu")
            }
            discoverServicesOnce(gatt)
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                connectError = "Service discovery failed status=$status"
                connectLatch.countDown()
                return
            }
            if (!hasConnectPermission()) {
                connectError = "BLUETOOTH_CONNECT permission missing while reading GATT services"
                connectLatch.countDown()
                return
            }

            try {
                val officialService = gatt.getService(TELINK_OTA_SERVICE_UUID)
                val officialCharacteristic = officialService?.getCharacteristic(TELINK_OTA_CHARACTERISTIC_UUID)
                if (officialCharacteristic != null && isWritable(officialCharacteristic)) {
                    selectCharacteristic(gatt, officialCharacteristic, "official Telink OTA service + characteristic")
                    connectLatch.countDown()
                    return
                }

                val fallback = gatt.services
                    .asSequence()
                    .mapNotNull { service ->
                        service.getCharacteristic(TELINK_OTA_CHARACTERISTIC_UUID)?.let { service to it }
                    }
                    .firstOrNull { (_, characteristic) -> isWritable(characteristic) }

                if (fallback != null) {
                    val (service, characteristic) = fallback
                    selectCharacteristic(gatt, characteristic, "OTA characteristic fallback under service ${service.uuid}")
                    connectLatch.countDown()
                    return
                }

                connectError = "Telink OTA characteristic not found: $TELINK_OTA_CHARACTERISTIC_UUID"
                connectLatch.countDown()
            } catch (e: SecurityException) {
                connectError = "GATT discovery permission error: ${e.message}"
                connectLatch.countDown()
            }
        }

        @Deprecated("Legacy callback remains required on older Android versions")
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            notificationListener?.invoke(characteristic.value?.clone() ?: return)
        }

        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
            notificationListener?.invoke(value.clone())
        }

        @Deprecated("Deprecated in API 33 but still invoked for the legacy overload")
        override fun onCharacteristicWrite(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            lastWriteStatus = status
            writeLatch?.countDown()
        }
    }

    @SuppressLint("MissingPermission")
    private fun discoverServicesOnce(gatt: BluetoothGatt) {
        if (!discoveryStarted.compareAndSet(false, true)) return
        if (!hasConnectPermission()) {
            connectError = "BLUETOOTH_CONNECT permission missing during service discovery"
            connectLatch.countDown()
            return
        }
        try {
            log("Discovering GATT services...")
            if (!gatt.discoverServices()) {
                connectError = "discoverServices() was rejected"
                connectLatch.countDown()
            }
        } catch (e: SecurityException) {
            connectError = "discoverServices permission error: ${e.message}"
            connectLatch.countDown()
        }
    }

    private fun isWritable(characteristic: BluetoothGattCharacteristic): Boolean {
        val p = characteristic.properties
        return (p and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0 ||
            (p and BluetoothGattCharacteristic.PROPERTY_WRITE) != 0
    }

    @SuppressLint("MissingPermission")
    private fun selectCharacteristic(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, description: String) {
        otaCharacteristic = characteristic
        notificationsEnabled = false
        val p = characteristic.properties

        if ((p and BluetoothGattCharacteristic.PROPERTY_NOTIFY) != 0) {
            try {
                val localEnabled = gatt.setCharacteristicNotification(characteristic, true)
                val cccd = characteristic.getDescriptor(CCCD_UUID)
                val descriptorAccepted = if (localEnabled && cccd != null) {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        gatt.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) == BluetoothStatusCodes.SUCCESS
                    } else {
                        @Suppress("DEPRECATION")
                        run {
                            cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                            gatt.writeDescriptor(cccd)
                        }
                    }
                } else false
                notificationsEnabled = localEnabled && descriptorAccepted
            } catch (_: Exception) {
                notificationsEnabled = false
            }
        }

        discoveryDescription = "$description; characteristic=${characteristic.uuid}; MTU=$negotiatedMtu; notify=${if (notificationsEnabled) "on" else "off"}; write=${if ((p and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0) "without-response" else "with-response"}"
        log("OTA characteristic ready; $discoveryDescription")
    }

    @SuppressLint("MissingPermission")
    fun connectAndDiscover(timeoutMs: Long = 15_000) {
        close()
        connectError = null
        otaCharacteristic = null
        negotiatedMtu = 23
        notificationsEnabled = false
        discoveryDescription = "OTA GATT not discovered"
        connectLatch = CountDownLatch(1)
        discoveryStarted.set(false)
        log("Connecting ${device.address} ...")
        gatt = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            device.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE)
        } else {
            device.connectGatt(context, false, callback)
        }
        if (gatt == null) throw IOException("connectGatt returned null")
        if (!connectLatch.await(timeoutMs, TimeUnit.MILLISECONDS)) {
            close()
            throw IOException("Timed out waiting for BLE connection/service discovery")
        }
        connectError?.let {
            close()
            throw IOException(it)
        }
        if (!isReady) {
            close()
            throw IOException("BLE connected but OTA characteristic is not ready")
        }
    }

    @SuppressLint("MissingPermission")
    @Synchronized
    fun write(data: ByteArray, timeoutMs: Long = 5_000) {
        val currentGatt = gatt ?: throw IOException("BLE GATT is not connected")
        val characteristic = otaCharacteristic ?: throw IOException("OTA characteristic is not ready")
        val props = characteristic.properties
        val noResponse = (props and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0
        val writeType = if (noResponse) BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE else BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT

        val latch = if (noResponse) null else CountDownLatch(1)
        writeLatch = latch
        lastWriteStatus = BluetoothGatt.GATT_FAILURE

        var accepted = false
        var attempt = 0
        val maxAttempts = if (noResponse) 8 else 1
        while (!accepted && attempt < maxAttempts) {
            accepted = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                currentGatt.writeCharacteristic(characteristic, data, writeType) == BluetoothStatusCodes.SUCCESS
            } else {
                @Suppress("DEPRECATION")
                run {
                    characteristic.writeType = writeType
                    characteristic.value = data
                    currentGatt.writeCharacteristic(characteristic)
                }
            }
            if (!accepted && noResponse && attempt < maxAttempts - 1) Thread.sleep(2)
            attempt++
        }

        if (!accepted) {
            writeLatch = null
            throw IOException("Android BLE stack rejected write request")
        }

        if (latch != null) {
            if (!latch.await(timeoutMs, TimeUnit.MILLISECONDS)) {
                writeLatch = null
                throw IOException("Timed out waiting for GATT write response")
            }
            writeLatch = null
            if (lastWriteStatus != BluetoothGatt.GATT_SUCCESS) {
                throw IOException("GATT write failed status=$lastWriteStatus")
            }
        }
    }

    @SuppressLint("MissingPermission")
    override fun close() {
        otaCharacteristic = null
        notificationsEnabled = false
        notificationListener = null
        writeLatch?.countDown()
        writeLatch = null
        try { gatt?.disconnect() } catch (_: SecurityException) { }
        try { gatt?.close() } catch (_: Exception) { }
        gatt = null
        discoveryDescription = "OTA GATT not discovered"
    }

    companion object {
        @JvmField val TELINK_OTA_SERVICE_UUID: UUID = UUID.fromString("00010203-0405-0607-0809-0a0b0c0d1912")
        @JvmField val TELINK_OTA_CHARACTERISTIC_UUID: UUID = UUID.fromString("00010203-0405-0607-0809-0a0b0c0d2b12")
        private val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}
