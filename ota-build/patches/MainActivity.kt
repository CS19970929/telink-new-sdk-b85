package com.example.bmsota

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.OpenableColumns
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.ProgressBar
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import com.example.bmsota.ble.BleOtaSession
import com.example.bmsota.model.ScanEntry
import com.example.bmsota.ota.FirmwareImage
import com.example.bmsota.ota.TelinkOtaController
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class MainActivity : Activity() {
    private lateinit var maxImageBytesBox: EditText
    private lateinit var packetDelayBox: EditText
    private lateinit var deviceSpinner: Spinner
    private lateinit var firmwareInfo: TextView
    private lateinit var progressBar: ProgressBar
    private lateinit var progressText: TextView
    private lateinit var logText: TextView
    private lateinit var startButton: Button
    private lateinit var cancelButton: Button

    private val entries = mutableListOf<ScanEntry>()
    private val byAddress = linkedMapOf<String, ScanEntry>()
    private lateinit var spinnerAdapter: ArrayAdapter<ScanEntry>

    private val bluetoothAdapter: BluetoothAdapter? by lazy {
        (getSystemService(BLUETOOTH_SERVICE) as BluetoothManager).adapter
    }

    @Volatile private var firmware: FirmwareImage? = null
    @Volatile private var session: BleOtaSession? = null
    @Volatile private var controller: TelinkOtaController? = null

    private val scanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val name = result.scanRecord?.deviceName ?: result.device.name ?: "(unnamed)"
            val entry = ScanEntry(result.device, name, result.rssi)
            runOnUiThread {
                byAddress[entry.device.address] = entry
                entries.clear()
                entries.addAll(byAddress.values.sortedByDescending { it.rssi })
                spinnerAdapter.notifyDataSetChanged()
            }
        }

        override fun onScanFailed(errorCode: Int) {
            appendLog("SCAN ERROR code=$errorCode")
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        maxImageBytesBox = findViewById(R.id.maxImageBytes)
        packetDelayBox = findViewById(R.id.packetDelayMs)
        deviceSpinner = findViewById(R.id.deviceSpinner)
        firmwareInfo = findViewById(R.id.firmwareInfo)
        progressBar = findViewById(R.id.progressBar)
        progressText = findViewById(R.id.progressText)
        logText = findViewById(R.id.logText)
        startButton = findViewById(R.id.startOtaButton)
        cancelButton = findViewById(R.id.cancelButton)

        spinnerAdapter = ArrayAdapter(this, android.R.layout.simple_spinner_item, entries)
        spinnerAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        deviceSpinner.adapter = spinnerAdapter

        findViewById<Button>(R.id.scanButton).setOnClickListener { startScan() }
        findViewById<Button>(R.id.stopScanButton).setOnClickListener { stopScan() }
        findViewById<Button>(R.id.connectButton).setOnClickListener { connectSelected() }
        findViewById<Button>(R.id.selectBinButton).setOnClickListener { selectBin() }
        startButton.setOnClickListener { startOta() }
        cancelButton.setOnClickListener { controller?.cancel() }

        appendLog("Ready. Telink OTA UUIDs are built in and GATT is discovered automatically.")
        appendLog("OTA service=${BleOtaSession.TELINK_OTA_SERVICE_UUID}")
        appendLog("OTA characteristic=${BleOtaSession.TELINK_OTA_CHARACTERISTIC_UUID}")
    }

    override fun onDestroy() {
        controller?.cancel()
        stopScanSilently()
        session?.close()
        super.onDestroy()
    }

    private fun hasBlePermissions(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED &&
                checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
        } else {
            checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        }
    }

    private fun requestBlePermissions(): Boolean {
        if (hasBlePermissions()) return true
        val perms = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            arrayOf(Manifest.permission.ACCESS_COARSE_LOCATION, Manifest.permission.ACCESS_FINE_LOCATION)
        }
        requestPermissions(perms, REQ_BLE_PERMISSIONS)
        toast("已请求 BLE 权限，授权后再次点击操作。")
        return false
    }

    @SuppressLint("MissingPermission")
    private fun startScan() {
        try {
            if (!requestBlePermissions()) return
            val adapter = bluetoothAdapter ?: error("设备不支持 Bluetooth")
            if (!adapter.isEnabled) error("请先打开手机蓝牙")
            entries.clear(); byAddress.clear(); spinnerAdapter.notifyDataSetChanged()
            adapter.bluetoothLeScanner?.startScan(scanCallback) ?: error("BluetoothLeScanner unavailable")
            appendLog("BLE scan started.")
        } catch (e: Exception) {
            showError(e)
        }
    }

    @SuppressLint("MissingPermission")
    private fun stopScan() {
        if (!requestBlePermissions()) return
        stopScanSilently()
        appendLog("BLE scan stopped.")
    }

    @SuppressLint("MissingPermission")
    private fun stopScanSilently() {
        try { bluetoothAdapter?.bluetoothLeScanner?.stopScan(scanCallback) } catch (_: Exception) { }
    }

    private fun connectSelected() {
        try {
            if (!requestBlePermissions()) return
            val selected = deviceSpinner.selectedItem as? ScanEntry ?: error("请先扫描并选择 BLE 设备")
            stopScanSilently()

            Thread {
                try {
                    appendLog("Connecting ${selected.device.address} ...")
                    session?.close()
                    val newSession = BleOtaSession(this, selected.device, ::appendLog)
                    newSession.connectAndDiscover()
                    session = newSession
                    appendLog("Connected. ${newSession.discoveryDescription}")
                    toast("OTA 已自动识别并连接")
                } catch (e: Exception) {
                    session?.close(); session = null
                    showError(e)
                }
            }.start()
        } catch (e: Exception) {
            showError(e)
        }
    }

    private fun selectBin() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "application/octet-stream"
            putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/octet-stream", "application/x-binary", "*/*"))
        }
        startActivityForResult(intent, REQ_OPEN_BIN)
    }

    @Deprecated("Using platform Activity API intentionally to keep the sample dependency-free")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQ_OPEN_BIN || resultCode != RESULT_OK) return
        val uri = data?.data ?: return
        try {
            val bytes = contentResolver.openInputStream(uri)?.use { it.readBytes() } ?: error("无法读取所选文件")
            val max = parseNonNegative(maxImageBytesBox.text.toString(), "Max image bytes")
            val name = queryName(uri) ?: uri.lastPathSegment ?: "firmware.bin"
            val image = FirmwareImage.fromBytes(name, bytes, max)
            firmware = image
            firmwareInfo.text = "$name\nImage=${image.imageSize} bytes, Packets=${image.packetCount}, header @0x18 validated"
            appendLog("Firmware loaded: ${image.imageSize} bytes, ${image.packetCount} packets.")
        } catch (e: Exception) {
            firmware = null
            firmwareInfo.text = "Firmware validation failed"
            showError(e)
        }
    }

    private fun startOta() {
        try {
            val image = firmware ?: error("请先选择并校验 BIN")
            val currentSession = session ?: error("请先连接 OTA GATT 特征")
            if (!currentSession.isReady) error("OTA GATT 未就绪，请重新连接")
            val delay = parseRange(packetDelayBox.text.toString(), "Packet delay", 0, 1000)

            startButton.isEnabled = false
            cancelButton.isEnabled = true
            progressBar.progress = 0
            progressText.text = "Starting"

            val newController = TelinkOtaController(
                currentSession,
                ::appendLog
            ) { done, total, imageBytes ->
                if (done == 1 || done == total || done % 8 == 0) {
                    runOnUiThread {
                        val permille = ((done.toLong() * 1000L) / total).toInt()
                        progressBar.progress = permille
                        progressText.text = String.format(Locale.US, "%.1f%%  %d/%d packets  %d/%d bytes", done * 100.0 / total, done, total, imageBytes, image.imageSize)
                    }
                }
            }
            controller = newController

            Thread {
                try {
                    appendLog("=== OTA START ===")
                    newController.upgrade(image, delay)
                    appendLog("=== OTA DATA COMPLETE ===")
                    runOnUiThread {
                        progressText.text = "Transfer complete; waiting for device reboot/version verification"
                        toast("OTA 数据发送完成；请等待设备重启")
                    }
                } catch (e: InterruptedException) {
                    appendLog(e.message ?: "OTA cancelled")
                } catch (e: Exception) {
                    appendLog("OTA ERROR: ${e.message}")
                    showError(e)
                } finally {
                    controller = null
                    runOnUiThread {
                        startButton.isEnabled = true
                        cancelButton.isEnabled = false
                    }
                }
            }.start()
        } catch (e: Exception) {
            showError(e)
        }
    }

    private fun queryName(uri: Uri): String? {
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) return cursor.getString(0)
        }
        return null
    }

    private fun parseNonNegative(value: String, field: String): Int {
        val v = value.trim().toIntOrNull() ?: error("$field 必须是整数")
        if (v < 0) error("$field 必须 >= 0")
        return v
    }

    private fun parseRange(value: String, field: String, min: Int, max: Int): Int {
        val v = value.trim().toIntOrNull() ?: error("$field 必须是整数")
        if (v !in min..max) error("$field 必须在 $min..$max 范围")
        return v
    }

    private fun appendLog(message: String) {
        runOnUiThread {
            val time = SimpleDateFormat("HH:mm:ss.SSS", Locale.US).format(Date())
            logText.append("$time  $message\n")
            val layout = logText.layout
            if (layout != null) {
                val scrollAmount = layout.getLineTop(logText.lineCount) - logText.height
                if (scrollAmount > 0) logText.scrollTo(0, scrollAmount) else logText.scrollTo(0, 0)
            }
        }
    }

    private fun showError(e: Exception) {
        appendLog("ERROR: ${e.message}")
        runOnUiThread { toast(e.message ?: e.javaClass.simpleName) }
    }

    private fun toast(message: String) {
        runOnUiThread { Toast.makeText(this, message, Toast.LENGTH_LONG).show() }
    }

    companion object {
        private const val REQ_BLE_PERMISSIONS = 100
        private const val REQ_OPEN_BIN = 101
    }
}
