package com.example.bmsota.ble

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import java.io.IOException
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

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

    val isReady: Boolean get() = gatt != null && otaCharacteristic != null
    @Volatile var discoveryDescription: String = "OTA GATT not discovered"
        private set

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
                    log("BLE connected; discovering services...")
                    if (!hasConnectPermission()) {
                        connectError = "BLUETOOTH_CONNECT permission missing during service discovery"
                        connectLatch.countDown()
                        return
                    }
                    try {
                        if (!gatt.discoverServices()) {
                            connectError = "discoverServices() was rejected"
                            connectLatch.countDown()
                        }
                    } catch (e: SecurityException) {
                        connectError = "discoverServices permission error: ${e.message}"
                        connectLatch.countDown()
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
                // Preferred path: Telink's official OTA service UUID.
                val officialService = gatt.getService(TELINK_OTA_SERVICE_UUID)
                val officialCharacteristic = officialService?.getCharacteristic(TELINK_OTA_CHARACTERISTIC_UUID)
                if (officialCharacteristic != null && isWritable(officialCharacteristic)) {
                    selectCharacteristic(officialCharacteristic, "official Telink OTA service + characteristic")
                    connectLatch.countDown()
                    return
                }

                // Compatibility fallback: a product may customize the service UUID while
                // retaining Telink's fixed OTA data characteristic UUID.
                val fallback = gatt.services
                    .asSequence()
                    .mapNotNull { service ->
                        service.getCharacteristic(TELINK_OTA_CHARACTERISTIC_UUID)?.let { service to it }
                    }
                    .firstOrNull { (_, characteristic) -> isWritable(characteristic) }

                if (fallback != null) {
                    val (service, characteristic) = fallback
                    selectCharacteristic(characteristic, "OTA characteristic fallback under service ${service.uuid}")
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

        @Deprecated("Deprecated in API 33 but still invoked for the legacy overload")
        override fun onCharacteristicWrite(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            lastWriteStatus = status
            writeLatch?.countDown()
        }
    }

    private fun isWritable(characteristic: BluetoothGattCharacteristic): Boolean {
        val p = characteristic.properties
        return (p and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0 ||
            (p and BluetoothGattCharacteristic.PROPERTY_WRITE) != 0
    }

    private fun selectCharacteristic(characteristic: BluetoothGattCharacteristic, description: String) {
        otaCharacteristic = characteristic
        val p = characteristic.properties
        discoveryDescription = "$description; characteristic=${characteristic.uuid}; properties=0x${p.toString(16)}"
        log("OTA characteristic ready; $discoveryDescription")
    }

    @SuppressLint("MissingPermission")
    fun connectAndDiscover(timeoutMs: Long = 15_000) {
        close()
        connectError = null
        otaCharacteristic = null
        discoveryDescription = "OTA GATT not discovered"
        connectLatch = CountDownLatch(1)
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

        val accepted = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            currentGatt.writeCharacteristic(characteristic, data, writeType) == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            run {
                characteristic.writeType = writeType
                characteristic.value = data
                currentGatt.writeCharacteristic(characteristic)
            }
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
        writeLatch?.countDown()
        writeLatch = null
        try { gatt?.disconnect() } catch (_: SecurityException) { }
        try { gatt?.close() } catch (_: Exception) { }
        gatt = null
        discoveryDescription = "OTA GATT not discovered"
    }

    companion object {
        @JvmField
        val TELINK_OTA_SERVICE_UUID: UUID = UUID.fromString("00010203-0405-0607-0809-0a0b0c0d1912")

        @JvmField
        val TELINK_OTA_CHARACTERISTIC_UUID: UUID = UUID.fromString("00010203-0405-0607-0809-0a0b0c0d2b12")
    }
}
