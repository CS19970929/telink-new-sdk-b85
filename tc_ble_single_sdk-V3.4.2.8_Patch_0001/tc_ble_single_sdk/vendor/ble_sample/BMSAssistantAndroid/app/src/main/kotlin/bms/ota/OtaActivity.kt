package com.telink.bmsassistant.ota

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.telink.bmsassistant.ui.BmsTheme

class OtaActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            BmsTheme {
                Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    OtaPermissionGate { OtaScreen() }
                }
            }
        }
    }
}

@Composable
private fun OtaPermissionGate(content: @Composable () -> Unit) {
    val context = LocalContext.current
    var granted by remember { mutableStateOf(hasBlePermissions(context)) }
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        granted = hasBlePermissions(context)
    }
    LaunchedEffect(Unit) {
        if (!granted) launcher.launch(requiredBlePermissions())
    }
    if (granted) content() else {
        Column(modifier = Modifier.padding(24.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text("BMS OTA 需要蓝牙扫描和连接权限")
            Button(onClick = { launcher.launch(requiredBlePermissions()) }) { Text("授予权限") }
        }
    }
}

@Composable
private fun OtaScreen() {
    val context = LocalContext.current
    val client = remember { TelinkOtaBleClient(context.applicationContext) }
    val session = remember { TelinkOtaSession(client) }
    val devices = remember { mutableStateListOf<TelinkOtaBleClient.Device>() }
    var selectedAddress by remember { mutableStateOf<String?>(null) }
    var firmware by remember { mutableStateOf<TelinkOtaCodec.FirmwareImage?>(null) }
    var firmwareName by remember { mutableStateOf("未选择 firmware.bin") }
    var status by remember { mutableStateOf("等待操作") }
    var ready by remember { mutableStateOf(false) }
    var progress by remember { mutableIntStateOf(0) }
    var running by remember { mutableStateOf(false) }
    var confirmStart by remember { mutableStateOf(false) }

    val fileLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        try {
            val bytes = context.contentResolver.openInputStream(uri)?.use { it.readBytes() }
                ?: throw IllegalStateException("无法读取固件文件")
            val parsed = TelinkOtaCodec.parseFirmware(bytes)
            firmware = parsed
            firmwareName = "${uri.lastPathSegment ?: "firmware.bin"} | ${parsed.declaredSize} bytes | ${parsed.packetCount} packets"
            status = "固件校验通过"
        } catch (exc: Exception) {
            firmware = null
            status = "固件校验失败: ${exc.message}"
        }
    }

    LaunchedEffect(client, session) {
        client.onDevice = { device ->
            val old = devices.indexOfFirst { it.address == device.address }
            if (old >= 0) devices[old] = device else devices.add(device)
        }
        client.onState = { state ->
            status = state
            if (state == "disconnected") ready = false
        }
        client.onReady = {
            ready = true
            status = "OTA characteristic ready"
        }
        client.onError = { message ->
            status = message
            running = false
            session.cancel(message)
        }
        session.onProgress = { value, message ->
            progress = value
            status = message
        }
        session.onFinished = { success, message ->
            running = false
            status = message
            if (success) progress = 100
        }
    }

    if (confirmStart) {
        AlertDialog(
            onDismissRequest = { confirmStart = false },
            title = { Text("确认 OTA") },
            text = {
                Text("升级期间禁止断电。当前固件 BLE 安全未开启，请确认连接的是目标 BMS。")
            },
            confirmButton = {
                Button(onClick = {
                    confirmStart = false
                    val image = firmware ?: return@Button
                    running = true
                    progress = 0
                    try {
                        session.start(image)
                    } catch (exc: Exception) {
                        running = false
                        status = "OTA 启动失败: ${exc.message}"
                    }
                }) { Text("开始升级") }
            },
            dismissButton = { TextButton(onClick = { confirmStart = false }) { Text("取消") } },
        )
    }

    Column(
        modifier = Modifier.fillMaxSize().padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("BMS Telink OTA", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
        Text("Legacy OTA / 16-byte PDU / Write With Response", style = MaterialTheme.typography.bodySmall)

        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = {
                try {
                    devices.clear()
                    selectedAddress = null
                    client.startScan()
                } catch (exc: Exception) {
                    status = "扫描失败: ${exc.message}"
                }
            }, enabled = !running) { Text("扫描") }
            OutlinedButton(onClick = { client.stopScan() }, enabled = !running) { Text("停止") }
            Button(onClick = {
                val address = selectedAddress ?: return@Button
                try {
                    ready = false
                    client.connect(address)
                } catch (exc: Exception) {
                    status = "连接失败: ${exc.message}"
                }
            }, enabled = selectedAddress != null && !running) { Text("连接 OTA") }
        }

        Card(modifier = Modifier.fillMaxWidth().weight(1f)) {
            LazyColumn(modifier = Modifier.fillMaxSize().padding(8.dp)) {
                items(devices, key = { it.address }) { device ->
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { selectedAddress = device.address }
                            .padding(10.dp),
                    ) {
                        Text(if (selectedAddress == device.address) "✓ ${device.name}" else device.name, fontWeight = FontWeight.SemiBold)
                        Text("${device.address} | ${device.rssi} dBm", style = MaterialTheme.typography.bodySmall)
                    }
                }
            }
        }

        Button(onClick = { fileLauncher.launch(arrayOf("application/octet-stream", "application/x-binary", "*/*")) }, enabled = !running) {
            Text("选择 firmware.bin")
        }
        Text(firmwareName, style = MaterialTheme.typography.bodySmall)

        LinearProgressIndicator(
            progress = { progress / 100f },
            modifier = Modifier.fillMaxWidth(),
        )
        Text("$progress%  $status")

        Button(
            onClick = { confirmStart = true },
            enabled = ready && firmware != null && !running,
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text(if (running) "升级中..." else "开始 OTA")
        }
    }
}

private fun requiredBlePermissions(): Array<String> = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
    arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
} else {
    arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
}

private fun hasBlePermissions(context: Context): Boolean = requiredBlePermissions().all {
    context.checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED
}
