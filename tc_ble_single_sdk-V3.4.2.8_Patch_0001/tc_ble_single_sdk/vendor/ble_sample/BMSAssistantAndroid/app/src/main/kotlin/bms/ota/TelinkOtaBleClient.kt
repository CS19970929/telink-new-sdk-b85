package com.telink.bmsassistant.ota

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import java.util.UUID

class TelinkOtaBleClient(private val context: Context) {
    data class Device(val address: String, val name: String, val rssi: Int)

    var onDevice: (Device) -> Unit = {}
    var onState: (String) -> Unit = {}
    var onReady: () -> Unit = {}
    var onWriteComplete: () -> Unit = {}
    var onNotification: (ByteArray) -> Unit = {}
    var onError: (String) -> Unit = {}

    private val main = Handler(Looper.getMainLooper())
    private val manager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val adapter: BluetoothAdapter? get() = manager.adapter
    private var gatt: BluetoothGatt? = null
    private var otaCharacteristic: BluetoothGattCharacteristic? = null
    private var scanning = false

    private val serviceUuid = UUID.fromString(TelinkOtaCodec.OTA_SERVICE_UUID)
    private val characteristicUuid = UUID.fromString(TelinkOtaCodec.OTA_CHARACTERISTIC_UUID)
    private val cccdUuid = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) = emit(result)
        override fun onBatchScanResults(results: MutableList<ScanResult>) = results.forEach(::emit)
        override fun onScanFailed(errorCode: Int) {
            scanning = false
            main.post { onError("OTA scan failed: $errorCode") }
        }
    }

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            main.post {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    onError("OTA GATT connection failed: status=$status")
                    close()
                    return@post
                }
                when (newState) {
                    BluetoothProfile.STATE_CONNECTED -> {
                        this@TelinkOtaBleClient.gatt = gatt
                        onState("connected; discovering OTA service")
                        discoverServices(gatt)
                    }
                    BluetoothProfile.STATE_DISCONNECTED -> {
                        onState("disconnected")
                        close()
                    }
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            main.post {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    onError("OTA service discovery failed: status=$status")
                    return@post
                }
                val service = gatt.getService(serviceUuid)
                val characteristic = service?.getCharacteristic(characteristicUuid)
                if (service == null || characteristic == null) {
                    onError("OTA service/characteristic not found")
                    return@post
                }
                otaCharacteristic = characteristic
                subscribe(gatt, characteristic)
            }
        }

        override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            if (descriptor.uuid != cccdUuid) return
            main.post {
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    onState("OTA characteristic ready")
                    onReady()
                } else {
                    onError("OTA notify subscription failed: status=$status")
                }
            }
        }

        override fun onCharacteristicWrite(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            if (characteristic.uuid != characteristicUuid) return
            main.post {
                if (status == BluetoothGatt.GATT_SUCCESS) onWriteComplete()
                else onError("OTA packet write failed: status=$status")
            }
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            if (characteristic.uuid == characteristicUuid) {
                main.post { onNotification(characteristic.value ?: byteArrayOf()) }
            }
        }

        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
            if (characteristic.uuid == characteristicUuid) main.post { onNotification(value) }
        }
    }

    @SuppressLint("MissingPermission")
    fun startScan() {
        if (!hasScanPermission()) throw IllegalStateException("missing BLE scan permission")
        val currentAdapter = adapter ?: throw IllegalStateException("Bluetooth adapter unavailable")
        if (!currentAdapter.isEnabled) throw IllegalStateException("Bluetooth is disabled")
        if (scanning) return
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        currentAdapter.bluetoothLeScanner.startScan(null, settings, scanCallback)
        scanning = true
        onState("scanning")
    }

    @SuppressLint("MissingPermission")
    fun stopScan() {
        if (!scanning) return
        if (hasScanPermission()) adapter?.bluetoothLeScanner?.stopScan(scanCallback)
        scanning = false
    }

    @SuppressLint("MissingPermission")
    fun connect(address: String) {
        if (!hasConnectPermission()) throw IllegalStateException("missing BLE connect permission")
        val currentAdapter = adapter ?: throw IllegalStateException("Bluetooth adapter unavailable")
        stopScan()
        close()
        val device = currentAdapter.getRemoteDevice(address)
        onState("connecting ${device.address}")
        gatt = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            device.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE)
        } else {
            device.connectGatt(context, false, callback)
        }
    }

    @SuppressLint("MissingPermission")
    fun write(packet: ByteArray) {
        if (!hasConnectPermission()) throw IllegalStateException("missing BLE connect permission")
        val currentGatt = gatt ?: throw IllegalStateException("OTA GATT is not connected")
        val characteristic = otaCharacteristic ?: throw IllegalStateException("OTA characteristic is not ready")
        characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        val ok = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            currentGatt.writeCharacteristic(characteristic, packet, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            characteristic.value = packet
            @Suppress("DEPRECATION")
            currentGatt.writeCharacteristic(characteristic)
        }
        if (!ok) throw IllegalStateException("failed to queue OTA packet")
    }

    @SuppressLint("MissingPermission")
    fun disconnect() {
        if (hasConnectPermission()) gatt?.disconnect()
        close()
    }

    @SuppressLint("MissingPermission")
    private fun discoverServices(gatt: BluetoothGatt) {
        if (hasConnectPermission()) gatt.discoverServices()
    }

    @SuppressLint("MissingPermission")
    private fun subscribe(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
        if (!hasConnectPermission()) return
        if (!gatt.setCharacteristicNotification(characteristic, true)) {
            onError("failed to enable OTA notifications")
            return
        }
        val descriptor = characteristic.getDescriptor(cccdUuid)
        if (descriptor == null) {
            onState("OTA ready without CCCD")
            onReady()
            return
        }
        val ok = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            gatt.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            @Suppress("DEPRECATION")
            gatt.writeDescriptor(descriptor)
        }
        if (!ok) onError("failed to queue OTA CCCD write")
    }

    @SuppressLint("MissingPermission")
    private fun emit(result: ScanResult) {
        val device = result.device ?: return
        val name = result.scanRecord?.deviceName ?: if (hasConnectPermission()) device.name else null ?: device.address
        main.post { onDevice(Device(device.address, name, result.rssi)) }
    }

    @SuppressLint("MissingPermission")
    private fun close() {
        otaCharacteristic = null
        if (hasConnectPermission()) gatt?.close()
        gatt = null
    }

    private fun hasScanPermission(): Boolean = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        context.checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED
    } else {
        context.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
    }

    private fun hasConnectPermission(): Boolean = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
    } else true
}
