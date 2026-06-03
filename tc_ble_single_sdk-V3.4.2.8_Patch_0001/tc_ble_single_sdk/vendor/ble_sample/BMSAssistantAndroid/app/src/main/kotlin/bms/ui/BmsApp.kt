@file:OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)

package com.telink.bmsassistant.ui

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Send
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.AssistChip
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.telink.bmsassistant.data.BmsRepository
import com.telink.bmsassistant.domain.BmsUiState
import com.telink.bmsassistant.domain.ConnectionStatus
import com.telink.bmsassistant.domain.DetailPage
import com.telink.bmsassistant.domain.DiscoveredDevice
import com.telink.bmsassistant.domain.ScanMode
import com.telink.bmsassistant.domain.batterySnapshotJson
import com.telink.bmsassistant.domain.exchangeLogCsv
import com.telink.bmsassistant.domain.registerBlockText
import kotlinx.coroutines.delay

private data class PendingConfirm(
    val title: String,
    val message: String,
    val action: () -> Unit,
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun BmsApp(repository: BmsRepository) {
    val state = repository.state
    var selectedTab by rememberSaveable { mutableIntStateOf(0) }
    var autoRefresh by rememberSaveable { mutableStateOf(true) }
    var refreshMs by rememberSaveable { mutableIntStateOf(2000) }
    var pendingConfirm by remember { mutableStateOf<PendingConfirm?>(null) }
    val snackbarHostState = remember { SnackbarHostState() }
    val context = LocalContext.current

    val jsonLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument("application/json"),
    ) { uri: Uri? ->
        uri?.let {
            context.contentResolver.openOutputStream(it)?.use { stream ->
                stream.write(batterySnapshotJson(state).toByteArray(Charsets.UTF_8))
            }
        }
    }
    val csvLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument("text/csv"),
    ) { uri: Uri? ->
        uri?.let {
            context.contentResolver.openOutputStream(it)?.use { stream ->
                stream.write(exchangeLogCsv(state.logs).toByteArray(Charsets.UTF_8))
            }
        }
    }

    LaunchedEffect(state.statusMessage) {
        if (state.statusMessage.isNotBlank()) {
            snackbarHostState.showSnackbar(state.statusMessage)
        }
    }

    LaunchedEffect(autoRefresh, refreshMs, selectedTab, state.canSendCommands) {
        while (autoRefresh && selectedTab == 0 && state.canSendCommands) {
            delay(refreshMs.toLong())
            repository.refreshBatteryStatus()
        }
    }

    pendingConfirm?.let { confirm ->
        AlertDialog(
            onDismissRequest = { pendingConfirm = null },
            title = { Text(confirm.title) },
            text = { Text(confirm.message) },
            confirmButton = {
                Button(
                    onClick = {
                        pendingConfirm = null
                        confirm.action()
                    },
                ) {
                    Text("确认执行")
                }
            },
            dismissButton = {
                TextButton(onClick = { pendingConfirm = null }) {
                    Text("取消")
                }
            },
        )
    }

    Scaffold(
        snackbarHost = { SnackbarHost(snackbarHostState) },
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text("BMS Assistant Android", fontWeight = FontWeight.Bold)
                        Text("Modbus RTU over BLE 工程版", style = MaterialTheme.typography.bodySmall)
                    }
                },
                actions = {
                    IconButton(onClick = { repository.sendEchoTest() }, enabled = state.canSendCommands) {
                        Icon(Icons.Default.Send, contentDescription = "Echo 测试")
                    }
                    IconButton(onClick = { jsonLauncher.launch("BMSAssistantAndroid-battery.json") }) {
                        Icon(Icons.Default.Download, contentDescription = "导出电池快照")
                    }
                },
            )
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
        ) {
            DevicePanel(state, repository)
            TabRow(selectedTabIndex = selectedTab) {
                DetailPage.entries.forEachIndexed { index, page ->
                    Tab(
                        selected = selectedTab == index,
                        onClick = { selectedTab = index },
                        text = { Text(page.title) },
                    )
                }
            }
            when (selectedTab) {
                0 -> BatteryPage(
                    state = state,
                    autoRefresh = autoRefresh,
                    refreshMs = refreshMs,
                    onAutoRefreshChanged = { autoRefresh = it },
                    onRefreshMsChanged = { refreshMs = it },
                    onRefresh = repository::refreshBatteryStatus,
                    onExportJson = { jsonLauncher.launch("BMSAssistantAndroid-battery.json") },
                )
                else -> DebugPage(
                    state = state,
                    repository = repository,
                    onExportCsv = { csvLauncher.launch("BMSAssistantAndroid-logs.csv") },
                    onConfirm = { pendingConfirm = it },
                )
            }
        }
    }
}

@Composable
private fun DevicePanel(state: BmsUiState, repository: BmsRepository) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(12.dp),
        shape = RoundedCornerShape(8.dp),
    ) {
        Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Icon(Icons.Default.Bluetooth, contentDescription = null)
                Text(state.connectionStatus.title, fontWeight = FontWeight.Bold)
                StatusChip(state.bluetoothStateLabel)
                StatusChip(state.connectedDeviceName)
                Spacer(modifier = Modifier.weight(1f))
                Button(onClick = repository::startScan) { Text("扫描") }
                Button(onClick = repository::stopScan) { Text("停止") }
                Button(onClick = repository::connectSelected, enabled = state.selectedDeviceId != null) { Text("连接") }
                Button(
                    onClick = repository::disconnect,
                    enabled = state.connectionStatus in setOf(
                        ConnectionStatus.Connecting,
                        ConnectionStatus.Connected,
                        ConnectionStatus.Ready,
                    ),
                ) {
                    Text("断开")
                }
            }
            ScanControls(state, repository)
            LazyColumn(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(190.dp),
                contentPadding = PaddingValues(vertical = 4.dp),
            ) {
                items(state.filteredDevices, key = { it.id }) { device ->
                    DeviceRow(
                        device = device,
                        selected = state.selectedDeviceId == device.id,
                        onClick = { repository.selectDevice(device.id) },
                    )
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ScanControls(state: BmsUiState, repository: BmsRepository) {
    var expanded by remember { mutableStateOf(false) }
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        ExposedDropdownMenuBox(expanded = expanded, onExpandedChange = { expanded = !expanded }) {
            OutlinedTextField(
                modifier = Modifier
                    .menuAnchor()
                    .width(170.dp),
                value = state.scanMode.title,
                onValueChange = {},
                readOnly = true,
                label = { Text("扫描模式") },
                trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = expanded) },
            )
            ExposedDropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                ScanMode.entries.forEach { mode ->
                    DropdownMenuItem(
                        text = { Text(mode.title) },
                        onClick = {
                            repository.setScanMode(mode)
                            expanded = false
                        },
                    )
                }
            }
        }
        OutlinedTextField(
            modifier = Modifier.weight(1f),
            value = state.searchText,
            onValueChange = repository::setSearchText,
            singleLine = true,
            label = { Text("设备名/地址过滤") },
        )
        Row(verticalAlignment = Alignment.CenterVertically) {
            Checkbox(
                checked = state.showOnlyLikelyBms,
                onCheckedChange = repository::setShowOnlyLikelyBms,
            )
            Text("只显示疑似 BMS")
        }
    }
}

@Composable
private fun DeviceRow(device: DiscoveredDevice, selected: Boolean, onClick: () -> Unit) {
    val bg = if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.10f) else Color.Transparent
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(bg, RoundedCornerShape(8.dp))
            .clickable(onClick = onClick)
            .padding(10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(device.displayName, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(device.id, style = MaterialTheme.typography.bodySmall)
        }
        Text("${device.rssi} dBm", modifier = Modifier.width(78.dp))
        Text(device.advertisedServicesSummary, modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis)
        if (device.isLikelyBms) StatusChip("BMS")
    }
}

@Composable
private fun BatteryPage(
    state: BmsUiState,
    autoRefresh: Boolean,
    refreshMs: Int,
    onAutoRefreshChanged: (Boolean) -> Unit,
    onRefreshMsChanged: (Int) -> Unit,
    onRefresh: () -> Unit,
    onExportJson: () -> Unit,
) {
    val snapshot = state.batteryStatus
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Card(shape = RoundedCornerShape(8.dp)) {
            Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(state.connectedDeviceName, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
                    StatusChip("方向: ${snapshot.currentDirectionText}")
                    StatusChip("数据源: ${snapshot.source.title}")
                    Spacer(modifier = Modifier.weight(1f))
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Checkbox(checked = autoRefresh, onCheckedChange = onAutoRefreshChanged)
                        Text("自动刷新")
                    }
                    FilterChip(
                        selected = refreshMs == 1000,
                        onClick = { onRefreshMsChanged(1000) },
                        label = { Text("1s") },
                    )
                    FilterChip(
                        selected = refreshMs == 2000,
                        onClick = { onRefreshMsChanged(2000) },
                        label = { Text("2s") },
                    )
                    FilterChip(
                        selected = refreshMs == 5000,
                        onClick = { onRefreshMsChanged(5000) },
                        label = { Text("5s") },
                    )
                    IconButton(onClick = onRefresh, enabled = state.canSendCommands) {
                        Icon(Icons.Default.Refresh, contentDescription = "刷新")
                    }
                    IconButton(onClick = onExportJson) {
                        Icon(Icons.Default.Download, contentDescription = "导出")
                    }
                }
                Text("更新时间: ${snapshot.updatedAtText}")
            }
        }

        MetricGrid(
            listOf(
                "Pack Voltage" to snapshot.packVoltageText,
                "Pack Current" to snapshot.currentText,
                "SOC" to snapshot.socText,
                "Max Temp" to snapshot.maxTempText,
                "Min Temp" to snapshot.minTempText,
                "MOS Temp" to snapshot.mosTempText,
                "Cell Max" to snapshot.maxCellVoltageText,
                "Cell Min" to snapshot.minCellVoltageText,
                "Cell Delta" to snapshot.cellDeltaText,
                "SOH" to snapshot.sohText,
                "Cycle Count" to snapshot.cycleCountText,
                "Capacity Now" to snapshot.capacityNowText,
                "Capacity Full" to snapshot.capacityFullText,
                "Capacity Factory" to snapshot.capacityFactoryText,
                "SystemStatus" to snapshot.systemStatusHexText,
            ),
        )

        SectionCard("单串电压") {
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                snapshot.cellVoltages.forEach { cell ->
                    MetricCard(title = cell.title, value = cell.voltageText, detail = "${cell.millivolts} mV")
                }
            }
        }

        SectionCard("系统状态位") {
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                snapshot.statusFlags.forEach { flag ->
                    AssistChip(onClick = {}, label = { Text("${flag.title}: ${if (flag.isActive) "ON" else "OFF"}") })
                }
            }
        }

        SectionCard("寄存器快照") {
            Text(registerBlockText(state.batteryBlocks), style = MaterialTheme.typography.bodySmall)
        }
    }
}

@Composable
private fun DebugPage(
    state: BmsUiState,
    repository: BmsRepository,
    onExportCsv: () -> Unit,
    onConfirm: (PendingConfirm) -> Unit,
) {
    var manualReadAddress by rememberSaveable { mutableStateOf("0x0000") }
    var manualReadQuantity by rememberSaveable { mutableStateOf("3") }
    var manualWriteAddress by rememberSaveable { mutableStateOf("0x1005") }
    var manualWriteWords by rememberSaveable { mutableStateOf("0x0032") }
    var socValue by rememberSaveable { mutableStateOf("60") }
    var rawFrame by rememberSaveable { mutableStateOf("") }
    var btNameSuffix by rememberSaveable { mutableStateOf("") }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        SectionCard("快捷动作") {
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = repository::refreshIdentity, enabled = state.canSendCommands) { Text("刷新身份") }
                Button(onClick = repository::refreshBatteryStatus, enabled = state.canSendCommands) { Text("刷新状态") }
                Button(onClick = repository::sendEchoTest, enabled = state.canSendCommands) { Text("Echo") }
                Button(onClick = repository::readProtectPreview, enabled = state.canSendCommands) { Text("保护参数") }
                Button(onClick = repository::readEventLogPreview, enabled = state.canSendCommands) { Text("事件日志") }
                Button(
                    onClick = {
                        onConfirm(
                            PendingConfirm("写 0x1103", "将向设备写入 0x1103 = 0x0003。", repository::writeRegister1103),
                        )
                    },
                    enabled = state.canSendCommands,
                ) {
                    Text("写 0x1103")
                }
            }
        }

        SectionCard("手动读寄存器") {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(
                    value = manualReadAddress,
                    onValueChange = { manualReadAddress = it },
                    label = { Text("起始地址") },
                    singleLine = true,
                    modifier = Modifier.weight(1f),
                )
                OutlinedTextField(
                    value = manualReadQuantity,
                    onValueChange = { manualReadQuantity = it },
                    label = { Text("数量") },
                    singleLine = true,
                    modifier = Modifier.width(110.dp),
                )
                Button(
                    onClick = { repository.readManualBlock(manualReadAddress, manualReadQuantity) },
                    enabled = state.canSendCommands,
                ) {
                    Text("读取")
                }
            }
        }

        SectionCard("写寄存器") {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(
                    value = manualWriteAddress,
                    onValueChange = { manualWriteAddress = it },
                    label = { Text("地址") },
                    singleLine = true,
                    modifier = Modifier.weight(1f),
                )
                OutlinedTextField(
                    value = manualWriteWords,
                    onValueChange = { manualWriteWords = it },
                    label = { Text("值") },
                    singleLine = true,
                    modifier = Modifier.weight(1f),
                )
                Button(
                    onClick = {
                        onConfirm(
                            PendingConfirm(
                                "写寄存器",
                                "将写入 $manualWriteAddress = $manualWriteWords，请确认该操作符合当前调试目的。",
                            ) {
                                repository.writeManualWords(manualWriteAddress, manualWriteWords)
                            },
                        )
                    },
                    enabled = state.canSendCommands,
                ) {
                    Text("写入")
                }
            }
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(
                    value = socValue,
                    onValueChange = { socValue = it },
                    label = { Text("SOC") },
                    singleLine = true,
                    modifier = Modifier.width(140.dp),
                )
                Button(
                    onClick = {
                        onConfirm(PendingConfirm("写 SOC", "将写入 0x1005 = $socValue。") { repository.writeSOC(socValue) })
                    },
                    enabled = state.canSendCommands,
                ) {
                    Text("写 SOC")
                }
                OutlinedTextField(
                    value = btNameSuffix,
                    onValueChange = { btNameSuffix = it },
                    label = { Text("蓝牙名后缀") },
                    singleLine = true,
                    modifier = Modifier.weight(1f),
                )
                Button(
                    onClick = {
                        onConfirm(PendingConfirm("写蓝牙名", "将写入蓝牙名后缀 $btNameSuffix，写入后需要重新扫描。") {
                            repository.writeBluetoothNameSuffix(btNameSuffix)
                        })
                    },
                    enabled = state.canSendCommands,
                ) {
                    Text("写名称")
                }
            }
        }

        SectionCard("原始帧") {
            OutlinedTextField(
                value = rawFrame,
                onValueChange = { rawFrame = it },
                label = { Text("Modbus RTU 原始十六进制帧") },
                minLines = 3,
                modifier = Modifier.fillMaxWidth(),
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(
                    onClick = {
                        onConfirm(PendingConfirm("发送原始帧", "将直接发送原始 Modbus RTU 帧。") {
                            repository.sendRawFrame(rawFrame)
                        })
                    },
                    enabled = state.canSendCommands,
                ) {
                    Text("发送")
                }
                Text("请求必须完整落在单个 GATT Write 内，当前安全上限 20 byte。")
            }
        }

        SectionCard("响应与寄存器块") {
            Text("最近响应: ${state.responsePreview.ifBlank { "暂无响应" }}")
            HorizontalDivider(Modifier.padding(vertical = 8.dp))
            Text(registerBlockText(state.debugBlocks), style = MaterialTheme.typography.bodySmall)
        }

        SectionCard("报文日志") {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("最近 ${state.logs.size} 条")
                Spacer(Modifier.weight(1f))
                IconButton(onClick = repository::clearLogs) {
                    Icon(Icons.Default.Delete, contentDescription = "清空日志")
                }
                IconButton(onClick = onExportCsv) {
                    Icon(Icons.Default.Download, contentDescription = "导出 CSV")
                }
            }
            state.logs.take(80).forEach { entry ->
                Text("${entry.timestampText} ${entry.direction.title} ${entry.title} ${entry.payloadHex} ${entry.note}", style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}

@Composable
private fun MetricGrid(items: List<Pair<String, String>>) {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        items.forEach { (title, value) -> MetricCard(title = title, value = value) }
    }
}

@Composable
private fun MetricCard(title: String, value: String, detail: String = "") {
    Card(
        modifier = Modifier.width(170.dp),
        shape = RoundedCornerShape(8.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
    ) {
        Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(title, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.secondary)
            Text(value, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            if (detail.isNotBlank()) Text(detail, style = MaterialTheme.typography.bodySmall)
        }
    }
}

@Composable
private fun SectionCard(title: String, content: @Composable () -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(8.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
    ) {
        Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            content()
        }
    }
}

@Composable
private fun StatusChip(text: String) {
    AssistChip(onClick = {}, label = { Text(text, maxLines = 1, overflow = TextOverflow.Ellipsis) })
}
