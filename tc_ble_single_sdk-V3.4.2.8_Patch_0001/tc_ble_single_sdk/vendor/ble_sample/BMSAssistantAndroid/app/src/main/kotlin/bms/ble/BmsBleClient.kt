package com.telink.bmsassistant.ble

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import com.telink.bmsassistant.domain.ConnectionStatus
import com.telink.bmsassistant.domain.DiscoveredDevice
import com.telink.bmsassistant.protocol.BMSGeneratedUUIDs
import java.util.UUID

class BmsBleClient(private val context: Context) {
    var onDeviceDiscovered: (DiscoveredDevice) -> Unit = {}
    var onConnectionChanged: (ConnectionStatus, String, String?) -> Unit = { _, _, _ -> }
    var onReady: () -> Unit = {}
    var onData: (ByteArray) -> Unit = {}
    var onError: (String) -> Unit = {}

    private val mainHandler = Handler(Looper.getMainLooper())
    private val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter?
        get() = bluetoothManager.adapter
    private val scanner: BluetoothLeScanner?
        get() = bluetoothAdapter?.bluetoothLeScanner

    private var currentGatt: BluetoothGatt? = null
    private var requestCharacteristic: BluetoothGattCharacteristic? = null
    private var responseCharacteristic: BluetoothGattCharacteristic? = null
    private var isScanning = false

    private val serviceUuid = UUID.fromString(BMSGeneratedUUIDs.SERVICE_UUID)
    private val requestUuid = UUID.fromString(BMSGeneratedUUIDs.REQUEST_CHARACTERISTIC_UUID)
    private val responseUuid = UUID.fromString(BMSGeneratedUUIDs.RESPONSE_CHARACTERISTIC_UUID)
    private val cccdUuid = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            emitScanResult(result)
        }

        override fun onBatchScanResults(results: MutableList<ScanResult>) {
            results.forEach(::emitScanResult)
        }

        override fun onScanFailed(errorCode: Int) {
            isScanning = false
            onError("扫描失败: errorCode=$errorCode")
            onConnectionChanged(ConnectionStatus.Failed, "扫描失败", null)
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            mainHandler.post {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    onConnectionChanged(ConnectionStatus.Failed, "连接失败: status=$status", gatt.device.address)
                    closeGatt()
                    return@post
                }
                when (newState) {
                    BluetoothProfile.STATE_CONNECTED -> {
                        currentGatt = gatt
                        onConnectionChanged(ConnectionStatus.Connected, "连接完成，正在发现服务", gatt.device.address)
                        discoverServices(gatt)
                    }
                    BluetoothProfile.STATE_DISCONNECTED -> {
                        onConnectionChanged(ConnectionStatus.Disconnected, "连接已断开", gatt.device.address)
                        closeGatt()
                    }
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            mainHandler.post {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    onError("服务发现失败: status=$status")
                    return@post
                }
                val service = gatt.getService(serviceUuid)
                if (service == null) {
                    onError("未发现目标 SPP 服务 ${BMSGeneratedUUIDs.SERVICE_UUID}")
                    return@post
                }
                bindService(gatt, service)
            }
        }

        override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            mainHandler.post {
                if (descriptor.uuid == cccdUuid && status == BluetoothGatt.GATT_SUCCESS) {
                    onConnectionChanged(ConnectionStatus.Ready, "SPP 通道已就绪，可直接收发 Modbus RTU", gatt.device.address)
                    onReady()
                } else if (descriptor.uuid == cccdUuid) {
                    onError("订阅响应特征失败: status=$status")
                }
            }
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            if (characteristic.uuid == responseUuid) {
                mainHandler.post { onData(characteristic.value ?: byteArrayOf()) }
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            if (characteristic.uuid == responseUuid) {
                mainHandler.post { onData(value) }
            }
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                mainHandler.post { onError("写入请求特征失败: status=$status") }
            }
        }
    }

    @SuppressLint("MissingPermission")
    fun startScan() {
        if (!hasScanPermission()) {
            onError("缺少蓝牙扫描权限")
            return
        }
        val adapter = bluetoothAdapter
        if (adapter == null || !adapter.isEnabled) {
            onError("系统蓝牙未开启")
            return
        }
        if (isScanning) return

        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        scanner?.startScan(null, settings, scanCallback)
        isScanning = true
        onConnectionChanged(ConnectionStatus.Scanning, "正在扫描附近 BLE 设备", null)
    }

    @SuppressLint("MissingPermission")
    fun stopScan() {
        if (!isScanning) return
        if (hasScanPermission()) scanner?.stopScan(scanCallback)
        isScanning = false
        onConnectionChanged(ConnectionStatus.Idle, "已停止扫描", null)
    }

    @SuppressLint("MissingPermission")
    fun connect(deviceId: String) {
        if (!hasConnectPermission()) {
            onError("缺少蓝牙连接权限")
            return
        }
        val adapter = bluetoothAdapter
        if (adapter == null || !adapter.isEnabled) {
            onError("系统蓝牙未开启")
            return
        }
        stopScan()
        closeGatt()
        val device = adapter.getRemoteDevice(deviceId)
        onConnectionChanged(ConnectionStatus.Connecting, "正在建立 BLE 连接", deviceId)
        currentGatt = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
        } else {
            device.connectGatt(context, false, gattCallback)
        }
    }

    @SuppressLint("MissingPermission")
    fun disconnect() {
        if (hasConnectPermission()) currentGatt?.disconnect()
        closeGatt()
        onConnectionChanged(ConnectionStatus.Disconnected, "连接已断开", null)
    }

    @SuppressLint("MissingPermission")
    fun send(data: ByteArray) {
        if (!hasConnectPermission()) {
            throw IllegalStateException("缺少蓝牙连接权限")
        }
        val gatt = currentGatt ?: throw IllegalStateException("BLE 通道尚未连接")
        val characteristic = requestCharacteristic ?: throw IllegalStateException("请求特征尚未就绪")
        characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        val ok = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            gatt.writeCharacteristic(
                characteristic,
                data,
                BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
            ) == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            characteristic.value = data
            @Suppress("DEPRECATION")
            gatt.writeCharacteristic(characteristic)
        }
        if (!ok) throw IllegalStateException("写入请求特征失败")
    }

    @SuppressLint("MissingPermission")
    private fun discoverServices(gatt: BluetoothGatt) {
        if (!hasConnectPermission()) return
        gatt.discoverServices()
    }

    @SuppressLint("MissingPermission")
    private fun bindService(gatt: BluetoothGatt, service: BluetoothGattService) {
        requestCharacteristic = service.getCharacteristic(requestUuid)
        responseCharacteristic = service.getCharacteristic(responseUuid)
        if (requestCharacteristic == null) {
            onError("未找到请求特征 ${BMSGeneratedUUIDs.REQUEST_CHARACTERISTIC_UUID}")
            return
        }
        val notifyChar = responseCharacteristic
        if (notifyChar == null) {
            onError("未找到响应特征 ${BMSGeneratedUUIDs.RESPONSE_CHARACTERISTIC_UUID}")
            return
        }

        if (!gatt.setCharacteristicNotification(notifyChar, true)) {
            onError("启用响应特征通知失败")
            return
        }
        val descriptor = notifyChar.getDescriptor(cccdUuid)
        if (descriptor == null) {
            onError("当前设备未暴露 CCCD，无法订阅响应特征")
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
        if (!ok) onError("写 CCCD 失败")
    }

    @SuppressLint("MissingPermission")
    private fun closeGatt() {
        requestCharacteristic = null
        responseCharacteristic = null
        if (hasConnectPermission()) {
            currentGatt?.close()
        }
        currentGatt = null
    }

    @SuppressLint("MissingPermission")
    private fun emitScanResult(result: ScanResult) {
        val device = result.device ?: return
        val serviceUuids = result.scanRecord?.serviceUuids
            ?.map { normalizeUuid(it.uuid.toString()) }
            ?: emptyList()
        val localName = result.scanRecord?.deviceName
            ?: if (hasConnectPermission()) device.name else null
            ?: ""
        onDeviceDiscovered(
            DiscoveredDevice(
                id = device.address,
                name = localName,
                rssi = result.rssi,
                advertisedServices = serviceUuids,
            ),
        )
    }

    private fun normalizeUuid(text: String): String {
        val upper = text.uppercase()
        return if (upper.startsWith("0000") && upper.endsWith("-0000-1000-8000-00805F9B34FB")) {
            upper.substring(4, 8)
        } else {
            upper
        }
    }

    private fun hasScanPermission(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            context.checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED
        } else {
            context.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        }
    }

    private fun hasConnectPermission(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            context.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
        } else {
            true
        }
    }
}
