package com.cjsh.bmsassistant

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.GridLayout
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import com.cjsh.bmsassistant.ble.BleBmsSession
import com.cjsh.bmsassistant.feature.AfeHardwareCatalog
import com.cjsh.bmsassistant.feature.EventLogs
import com.cjsh.bmsassistant.feature.ProtectionCatalog
import com.cjsh.bmsassistant.feature.ProtectionParameter
import com.cjsh.bmsassistant.feature.XlsxWriter
import com.cjsh.bmsassistant.model.BatterySnapshot
import com.cjsh.bmsassistant.model.DeviceIdentity
import com.cjsh.bmsassistant.model.MonitorRecord
import com.cjsh.bmsassistant.ota.FirmwareImage
import com.cjsh.bmsassistant.ota.TelinkOtaController
import com.cjsh.bmsassistant.protocol.BmsClient
import com.cjsh.bmsassistant.util.AppLogger
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

class MainActivity : Activity() {
    companion object {
        private const val REQ_PERMISSIONS = 100
        private const val REQ_BIN = 101
        private const val REQ_XLSX = 102
    }

    private lateinit var logger: AppLogger
    private lateinit var btAdapter: BluetoothAdapter
    private val mainHandler = Handler(Looper.getMainLooper())
    private val io = Executors.newSingleThreadExecutor()

    private data class FoundDevice(val device: BluetoothDevice, val name: String, var rssi: Int)
    private val foundDevices = linkedMapOf<String, FoundDevice>()
    private lateinit var deviceSpinner: Spinner
    private lateinit var deviceSpinnerAdapter: ArrayAdapter<String>
    private var scanCallback: ScanCallback? = null
    private var pendingPermissionAction: (() -> Unit)? = null

    @Volatile private var session: BleBmsSession? = null
    @Volatile private var bms: BmsClient? = null
    @Volatile private var connectedDevice: BluetoothDevice? = null
    @Volatile private var identity: DeviceIdentity? = null
    @Volatile private var lastSnapshot: BatterySnapshot? = null
    @Volatile private var isConnected = false
    @Volatile private var pollPaused = true
    @Volatile private var otaInProgress = false
    @Volatile private var manualDisconnect = false
    private val connecting = AtomicBoolean(false)
    private val reconnectLoop = AtomicBoolean(false)
    private val pollInFlight = AtomicBoolean(false)
    private var consecutivePollErrors = 0

    private lateinit var pageHost: FrameLayout
    private val pages = mutableListOf<View>()
    private val tabButtons = mutableListOf<Button>()

    private lateinit var commChip: TextView
    private lateinit var totalVoltageText: TextView
    private lateinit var currentText: TextView
    private lateinit var socText: TextView
    private lateinit var sohText: TextView
    private lateinit var workStateText: TextView
    private lateinit var capacityNowText: TextView
    private lateinit var capacityFullText: TextView
    private lateinit var cycleText: TextView
    private lateinit var cellMaxText: TextView
    private lateinit var cellMinText: TextView
    private lateinit var cellAvgText: TextView
    private lateinit var cellDeltaText: TextView
    private lateinit var tempMaxText: TextView
    private lateinit var tempMinText: TextView
    private lateinit var tempMosText: TextView
    private lateinit var mosText: TextView
    private lateinit var functionText: TextView
    private lateinit var protectionText: TextView
    private lateinit var cellsGrid: GridLayout
    private lateinit var identityText: TextView

    private val protectionParams = ProtectionCatalog.create()
    private data class ProtectViews(val current: TextView, val edit: EditText)
    private val protectionViews = mutableMapOf<Int, ProtectViews>()
    private lateinit var protectionStatus: TextView

    private val afeCatalog = AfeHardwareCatalog()
    private data class AfeViews(val current: TextView, val edit: EditText)
    private val afeViews = mutableMapOf<Int, AfeViews>()
    private lateinit var afeStatus: TextView

    private lateinit var eventLogStatus: TextView
    private lateinit var eventLogText: TextView

    private lateinit var btNameEdit: EditText
    private lateinit var socEdit: EditText
    private lateinit var cycleEdit: EditText
    private lateinit var firmwareText: TextView
    private lateinit var expectedVersionEdit: EditText
    private lateinit var otaProgress: ProgressBar
    private lateinit var otaStatus: TextView
    private lateinit var otaLogText: TextView
    @Volatile private var firmwareImage: FirmwareImage? = null
    private var firmwareFileName: String = ""

    private lateinit var debugLogText: TextView
    private lateinit var debugRawOutput: TextView

    @Volatile private var monitorActive = false
    @Volatile private var monitorIntervalSec = 5
    private val monitorRecords = mutableListOf<MonitorRecord>()
    private val monitorLock = Any()
    private var monitorStartedAt = 0L
    private var monitorLastRecordAt = 0L
    private lateinit var monitorIntervalSpinner: Spinner
    private lateinit var monitorStatus: TextView
    private var pendingExportRecords: List<MonitorRecord>? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        logger = AppLogger(this)
        val manager = getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        btAdapter = manager.adapter ?: run {
            Toast.makeText(this, "当前设备不支持蓝牙", Toast.LENGTH_LONG).show()
            finish(); return
        }
        buildUi()
        logger.listener = { line -> runOnUiThread { appendDebugLine(line) } }
        logger.log("APP", "应用启动；连接成功后电池数据自动按1秒周期刷新")
        logger.log("APP", "诊断日志：${logger.file.absolutePath}")
        mainHandler.post(pollRunnable)
    }

    // ---------- UI ----------
    private fun buildUi() {
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(6), dp(6), dp(6), dp(6)); setBackgroundColor(Color.rgb(245,245,245)) }
        val title = TextView(this).apply { text = "BMS Assistant"; textSize = 20f; setTypeface(typeface, Typeface.BOLD); setPadding(dp(6),dp(4),dp(6),dp(6)) }
        root.addView(title)

        val tabScroll = HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false }
        val tabBar = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        tabScroll.addView(tabBar)
        root.addView(tabScroll, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))

        pageHost = FrameLayout(this)
        root.addView(pageHost, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(root)

        addTab(tabBar, "实时监控", buildMainPage())
        addTab(tabBar, "软件保护", buildProtectionPage())
        addTab(tabBar, "AFE硬件保护", buildAfePage())
        addTab(tabBar, "事件日志", buildEventLogPage())
        addTab(tabBar, "设备 / OTA", buildDeviceOtaPage())
        addTab(tabBar, "专业调试", buildDebugPage())
        showTab(0)
    }

    private fun addTab(bar: LinearLayout, name: String, page: View) {
        val index = pages.size
        val button = Button(this).apply { text = name; setOnClickListener { showTab(index) } }
        bar.addView(button, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(42)))
        tabButtons.add(button)
        page.visibility = View.GONE
        pages.add(page); pageHost.addView(page, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
    }

    private fun showTab(index: Int) {
        pages.forEachIndexed { i, v -> v.visibility = if (i == index) View.VISIBLE else View.GONE }
        tabButtons.forEachIndexed { i, b -> b.isEnabled = i != index }
    }

    private fun buildMainPage(): View {
        val scroll = ScrollView(this)
        val root = vertical(); scroll.addView(root)
        section(root, "通信设置")
        val connRow = horizontal()
        deviceSpinnerAdapter = ArrayAdapter(this, android.R.layout.simple_spinner_item, mutableListOf<String>()).also { it.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item) }
        deviceSpinner = Spinner(this).apply { adapter = deviceSpinnerAdapter }
        connRow.addView(deviceSpinner, LinearLayout.LayoutParams(0, dp(48), 1f))
        connRow.addView(button("搜索") { ensureBlePermissions { startScan() } })
        connRow.addView(button("连接") { ensureBlePermissions { connectSelected() } })
        connRow.addView(button("断开") { manualDisconnect() })
        root.addView(connRow)
        val quickRow = horizontal()
        quickRow.addView(button("改蓝牙名") { showTab(4) })
        quickRow.addView(button("OTA升级") { showTab(4) })
        commChip = TextView(this).apply { text = "正在通讯"; textSize = 15f; gravity = Gravity.CENTER; setTextColor(Color.WHITE); setPadding(dp(14),dp(8),dp(14),dp(8)); visibility = View.INVISIBLE; background = rounded(Color.rgb(20,180,60)) }
        quickRow.addView(commChip)
        root.addView(quickRow)

        section(root, "基础信息")
        totalVoltageText = addValue(root, "总压")
        currentText = addValue(root, "电流")
        socText = addValue(root, "SOC")
        sohText = addValue(root, "SOH")
        workStateText = addValue(root, "工作状态")
        capacityNowText = addValue(root, "剩余容量")
        capacityFullText = addValue(root, "满充容量")
        cycleText = addValue(root, "循环次数")

        section(root, "单体电压")
        cellMaxText = addValue(root, "最高电压")
        cellMinText = addValue(root, "最低电压")
        cellAvgText = addValue(root, "平均电压")
        cellDeltaText = addValue(root, "最大压差")
        cellsGrid = GridLayout(this).apply { columnCount = 2; setPadding(dp(4),dp(4),dp(4),dp(8)) }
        root.addView(cellsGrid)

        section(root, "温度")
        tempMaxText = addValue(root, "最高温度")
        tempMinText = addValue(root, "最低温度")
        tempMosText = addValue(root, "MOS温度")

        section(root, "系统状态")
        mosText = addValue(root, "MOS")
        functionText = addValue(root, "功能状态")
        protectionText = addValue(root, "保护状态")

        section(root, "长期监控")
        val monRow = horizontal()
        monitorIntervalSpinner = Spinner(this)
        val intervalLabels = listOf("1秒/条","5秒/条","10秒/条","30秒/条","60秒/条")
        monitorIntervalSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_item, intervalLabels).also { it.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item) }
        monitorIntervalSpinner.setSelection(1)
        monRow.addView(monitorIntervalSpinner, LinearLayout.LayoutParams(0, dp(48), 1f))
        monRow.addView(button("开始") { startMonitoring() })
        monRow.addView(button("停止") { stopMonitoring() })
        monRow.addView(button("导出Excel") { exportMonitor() })
        root.addView(monRow)
        monitorStatus = TextView(this).apply { text = "未开始"; setPadding(dp(8),dp(4),dp(8),dp(8)) }
        root.addView(monitorStatus)

        identityText = TextView(this).apply { text = "硬件版本：—    软件版本：—    BMS序列号：—"; textSize = 12f; setPadding(dp(8),dp(10),dp(8),dp(12)); setTextColor(Color.DKGRAY) }
        root.addView(identityText)
        return scroll
    }

    private fun buildProtectionPage(): View {
        val scroll = ScrollView(this); val root = vertical(); scroll.addView(root)
        section(root, "MCU软件保护参数")
        root.addView(TextView(this).apply { text = "这是0x2100~0x2140的软件保护参数，与AFE硬件保护参数独立。修改后逐项回读验证。"; setPadding(dp(6),dp(4),dp(6),dp(8)) })
        val top = horizontal()
        top.addView(button("读取全部") { readProtection() })
        top.addView(button("保存修改") { confirmSaveProtection() })
        protectionStatus = TextView(this).apply { text = "未读取"; gravity = Gravity.CENTER_VERTICAL; setPadding(dp(8),0,0,0) }
        top.addView(protectionStatus, LinearLayout.LayoutParams(0, dp(48), 1f)); root.addView(top)
        protectionParams.forEach { p ->
            val box = vertical().apply { background = rounded(Color.WHITE); setPadding(dp(8),dp(6),dp(8),dp(6)) }
            box.addView(TextView(this).apply { text = p.name; setTypeface(typeface, Typeface.BOLD) })
            val row = horizontal(); val current = TextView(this).apply { text = "当前：— ${p.unit}"; gravity = Gravity.CENTER_VERTICAL }
            val edit = EditText(this).apply { hint = "修改值"; inputType = InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_DECIMAL or InputType.TYPE_NUMBER_FLAG_SIGNED; setSingleLine(true) }
            row.addView(current, LinearLayout.LayoutParams(0, dp(48), 1f)); row.addView(edit, LinearLayout.LayoutParams(0, dp(48), 1f)); box.addView(row)
            root.addView(box, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { setMargins(0,dp(2),0,dp(2)) })
            protectionViews[p.index] = ProtectViews(current, edit)
        }
        return scroll
    }

    private fun buildAfePage(): View {
        val scroll=ScrollView(this); val root=vertical(); scroll.addView(root)
        section(root,"SH367309 AFE硬件保护参数")
        root.addView(TextView(this).apply { text="这是0x2400~0x2417的AFE硬件保护参数，不是软件保护。按芯片离散档位校验，按关联组0x10写入并全块回读确认。"; setPadding(dp(6),dp(4),dp(6),dp(8)) })
        val top=horizontal(); top.addView(button("读取全部") { readAfe() }); top.addView(button("保存修改") { confirmSaveAfe() })
        afeStatus=TextView(this).apply { text="未读取"; gravity=Gravity.CENTER_VERTICAL; setPadding(dp(8),0,0,0) }; top.addView(afeStatus,LinearLayout.LayoutParams(0,dp(48),1f)); root.addView(top)
        afeCatalog.rows.forEach { p ->
            val box=vertical().apply { background=rounded(Color.WHITE); setPadding(dp(8),dp(6),dp(8),dp(6)) }
            box.addView(TextView(this).apply { text=p.title; setTypeface(typeface,Typeface.BOLD) })
            box.addView(TextView(this).apply { text=p.hint; textSize=11f; setTextColor(Color.DKGRAY) })
            val row=horizontal(); val current=TextView(this).apply { text="当前：— ${p.unit}"; gravity=Gravity.CENTER_VERTICAL }
            val edit=EditText(this).apply { hint="修改值(${p.unit})"; inputType=InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_DECIMAL or InputType.TYPE_NUMBER_FLAG_SIGNED; setSingleLine(true) }
            row.addView(current,LinearLayout.LayoutParams(0,dp(48),1f)); row.addView(edit,LinearLayout.LayoutParams(0,dp(48),1f)); box.addView(row); root.addView(box)
            afeViews[p.wireIndex]=AfeViews(current,edit)
        }
        return scroll
    }

    private fun buildEventLogPage(): View {
        val scroll=ScrollView(this); val root=vertical(); scroll.addView(root)
        section(root,"设备事件日志")
        val row=horizontal(); row.addView(button("读取100条日志") { readEventLogs() }); eventLogStatus=TextView(this).apply { text="未读取"; gravity=Gravity.CENTER_VERTICAL; setPadding(dp(8),0,0,0) }; row.addView(eventLogStatus,LinearLayout.LayoutParams(0,dp(48),1f)); root.addView(row)
        root.addView(TextView(this).apply { text="最新记录在前。设备日志保存事件类型和与上一事件的时间间隔，不保存绝对日期时间。"; textSize=12f; setTextColor(Color.DKGRAY); setPadding(dp(6),0,dp(6),dp(8)) })
        eventLogText=TextView(this).apply { typeface=Typeface.MONOSPACE; textSize=13f; setTextIsSelectable(true); setPadding(dp(8),dp(8),dp(8),dp(8)); background=rounded(Color.WHITE) }
        root.addView(eventLogText)
        return scroll
    }

    private fun buildDeviceOtaPage(): View {
        val scroll=ScrollView(this); val root=vertical(); scroll.addView(root)
        section(root,"设备设置")
        btNameEdit=EditText(this).apply { hint="蓝牙名，例如 BT_TEST01"; setSingleLine(true) }; root.addView(btNameEdit)
        val nameRow=horizontal(); nameRow.addView(button("读取蓝牙名") { readBtName() }); nameRow.addView(button("写入并回读") { writeBtName() }); root.addView(nameRow)
        val socRow=horizontal(); socEdit=EditText(this).apply { hint="SOC 0~100"; inputType=InputType.TYPE_CLASS_NUMBER; setSingleLine(true) }; socRow.addView(socEdit,LinearLayout.LayoutParams(0,dp(48),1f)); socRow.addView(button("SOC校准并验证") { setSoc() }); root.addView(socRow)
        val cycRow=horizontal(); cycleEdit=EditText(this).apply { hint="循环次数"; inputType=InputType.TYPE_CLASS_NUMBER; setSingleLine(true) }; cycRow.addView(cycleEdit,LinearLayout.LayoutParams(0,dp(48),1f)); cycRow.addView(button("写循环次数并验证") { setCycle() }); root.addView(cycRow)

        section(root,"Telink OTA")
        val fileRow=horizontal(); fileRow.addView(button("选择BIN") { chooseFirmware() }); firmwareText=TextView(this).apply { text="未选择固件"; gravity=Gravity.CENTER_VERTICAL; setPadding(dp(8),0,0,0) }; fileRow.addView(firmwareText,LinearLayout.LayoutParams(0,dp(48),1f)); root.addView(fileRow)
        expectedVersionEdit=EditText(this).apply { hint="目标软件版本（可选，例如 V1.1）"; setSingleLine(true) }; root.addView(expectedVersionEdit)
        otaProgress=ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal).apply { max=100; progress=0 }; root.addView(otaProgress,LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,dp(22)))
        otaStatus=TextView(this).apply { text="等待升级"; setPadding(dp(6),dp(6),dp(6),dp(6)); setTypeface(typeface,Typeface.BOLD) }; root.addView(otaStatus)
        root.addView(button("开始OTA升级") { startOta() },LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,dp(52)))
        otaLogText=TextView(this).apply { typeface=Typeface.MONOSPACE; textSize=11f; setTextIsSelectable(true); setPadding(dp(8),dp(8),dp(8),dp(8)); background=rounded(Color.WHITE) }; root.addView(otaLogText)
        return scroll
    }

    private fun buildDebugPage(): View {
        val scroll=ScrollView(this); val root=vertical(); scroll.addView(root)
        section(root,"原始寄存器调试")
        val addr=EditText(this).apply { hint="地址 0xD120"; setSingleLine(true) }
        val qty=EditText(this).apply { hint="数量"; inputType=InputType.TYPE_CLASS_NUMBER; setText("1"); setSingleLine(true) }
        val value=EditText(this).apply { hint="写入值 0x0000/十进制"; setSingleLine(true) }
        val r1=horizontal(); r1.addView(addr,LinearLayout.LayoutParams(0,dp(48),1f)); r1.addView(qty,LinearLayout.LayoutParams(dp(90),dp(48))); r1.addView(button("读取") { debugRead(addr,qty) }); root.addView(r1)
        val r2=horizontal(); r2.addView(value,LinearLayout.LayoutParams(0,dp(48),1f)); r2.addView(button("写单寄存器") { debugWrite(addr,value) }); root.addView(r2)
        debugRawOutput=TextView(this).apply { typeface=Typeface.MONOSPACE; setTextIsSelectable(true); background=rounded(Color.WHITE); setPadding(dp(8),dp(8),dp(8),dp(8)) }; root.addView(debugRawOutput)
        section(root,"完整BLE / Modbus诊断日志")
        val clr=button("清空屏幕日志") { debugLogText.text="" }; root.addView(clr)
        debugLogText=TextView(this).apply { typeface=Typeface.MONOSPACE; textSize=10f; setTextIsSelectable(true); background=rounded(Color.rgb(248,248,248)); setPadding(dp(6),dp(6),dp(6),dp(6)) }; root.addView(debugLogText)
        root.addView(TextView(this).apply { text="日志文件保存在应用私有目录：${logger.file.absolutePath}"; textSize=11f; setTextColor(Color.DKGRAY) })
        return scroll
    }

    // ---------- BLE scan / connection ----------
    private fun ensureBlePermissions(action: () -> Unit) {
        val required = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT) else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        val missing = required.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (missing.isEmpty()) action() else { pendingPermissionAction=action; requestPermissions(missing.toTypedArray(),REQ_PERMISSIONS) }
    }

    override fun onRequestPermissionsResult(requestCode:Int,permissions:Array<out String>,grantResults:IntArray) {
        super.onRequestPermissionsResult(requestCode,permissions,grantResults)
        if(requestCode==REQ_PERMISSIONS){ val ok=grantResults.isNotEmpty() && grantResults.all { it==PackageManager.PERMISSION_GRANTED }; val action=pendingPermissionAction; pendingPermissionAction=null; if(ok) action?.invoke() else toast("需要蓝牙权限才能扫描和连接BMS") }
    }

    @SuppressLint("MissingPermission")
    private fun startScan() {
        stopScan()
        foundDevices.clear(); refreshDeviceSpinner()
        val scanner=btAdapter.bluetoothLeScanner ?: run { toast("蓝牙扫描器不可用"); return }
        val callback=object:ScanCallback(){
            override fun onScanResult(callbackType:Int,result:ScanResult){
                val name=result.scanRecord?.deviceName ?: try { result.device.name ?: "" } catch(_:Exception){""}
                if(!name.startsWith("BT_",true)) return
                foundDevices[result.device.address]=FoundDevice(result.device,name,result.rssi)
                logger.log("SCAN","DISCOVER name='$name' address=${result.device.address} rssi=${result.rssi}dBm")
                runOnUiThread { refreshDeviceSpinner() }
            }
            override fun onScanFailed(errorCode:Int){ logger.log("SCAN","FAILED errorCode=$errorCode"); runOnUiThread { toast("蓝牙扫描失败：$errorCode") } }
        }
        scanCallback=callback
        val settings=ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        try { scanner.startScan(null,settings,callback); logger.log("SCAN","START filter=BT_*"); mainHandler.postDelayed({ stopScan() },8000) }
        catch(e:Exception){ logger.log("SCAN","START_FAIL ${e.javaClass.simpleName}: ${e.message}"); toast("无法开始蓝牙扫描") }
    }

    @SuppressLint("MissingPermission")
    private fun stopScan(){ val cb=scanCallback ?: return; try{ btAdapter.bluetoothLeScanner?.stopScan(cb) }catch(_:Exception){}; scanCallback=null; logger.log("SCAN","STOP") }

    private fun refreshDeviceSpinner(){
        val list=foundDevices.values.sortedBy { it.name.lowercase(Locale.getDefault()) }
        deviceSpinnerAdapter.clear(); deviceSpinnerAdapter.addAll(list.map { "${it.name}    ${it.rssi} dBm" }); deviceSpinnerAdapter.notifyDataSetChanged()
    }

    private fun selectedDevice(): BluetoothDevice? {
        val list=foundDevices.values.sortedBy { it.name.lowercase(Locale.getDefault()) }
        val pos=deviceSpinner.selectedItemPosition
        return if(pos in list.indices) list[pos].device else null
    }

    private fun connectSelected(){ val d=selectedDevice() ?: run { toast("请先搜索并选择BT_设备"); return }; manualDisconnect=false; stopScan(); io.execute { connectBlocking(d,false) } }

    private fun connectBlocking(target:BluetoothDevice,silent:Boolean):Boolean {
        if(!connecting.compareAndSet(false,true)) return false
        pollPaused=true
        try {
            var last:Exception?=null
            for(attempt in 1..3){
                logger.log("CONNECT","FULL_ATTEMPT_BEGIN $attempt/3 address=${target.address}")
                closeCurrent()
                val s=BleBmsSession(this){cat,msg->logger.log(cat,msg)}
                s.unexpectedDisconnectListener={ onUnexpectedDisconnect(s,target) }
                var client:BmsClient?=null
                try{
                    s.connect(target)
                    client=BmsClient(s){cat,msg->logger.log(cat,msg)}
                    logger.log("CONNECT","ModbusProbe begin after GATT ready")
                    client.probe()
                    val id=client.readIdentity()
                    val snap=client.readBattery()
                    session=s; bms=client; connectedDevice=target; identity=id; lastSnapshot=snap; isConnected=true; manualDisconnect=false; consecutivePollErrors=0
                    runOnUiThread { applyIdentity(id); applySnapshot(snap); commChip.visibility=View.VISIBLE; btNameEdit.setText(id.bluetoothName) }
                    logger.log("CONNECT","BMS_READY attempt=$attempt software='${id.software}'")
                    pollPaused=false
                    return true
                }catch(e:Exception){
                    last=e; logger.log("CONNECT","FULL_ATTEMPT_FAIL $attempt/3 type=${e.javaClass.name} message=${e.message}")
                    try{client?.close()}catch(_:Exception){}; try{s.close()}catch(_:Exception){}
                    if(attempt<3) Thread.sleep(700)
                }
            }
            isConnected=false
            runOnUiThread { commChip.visibility=View.INVISIBLE }
            if(!silent) runOnUiThread { toast("连接BMS失败，详细原因已写入专业调试日志") }
            logger.log("CONNECT","FAILED after full retries: ${last?.message}")
            return false
        } finally { connecting.set(false); if(isConnected) pollPaused=false }
    }

    private fun onUnexpectedDisconnect(source:BleBmsSession,target:BluetoothDevice){
        if(session !== source || manualDisconnect || otaInProgress) return
        logger.log("CONNECT","UNEXPECTED_DISCONNECT; customer page keeps last valid data; starting silent reconnect")
        isConnected=false; pollPaused=true
        runOnUiThread { commChip.visibility=View.INVISIBLE }
        requestSilentReconnect(target)
    }

    private fun requestSilentReconnect(target:BluetoothDevice){
        if(!reconnectLoop.compareAndSet(false,true)) return
        io.execute {
            try{
                var cycle=0
                while(!manualDisconnect && !otaInProgress && !isConnected){
                    cycle++; logger.log("RECONNECT","CYCLE $cycle begin")
                    if(connectBlocking(target,true)) break
                    Thread.sleep(3000)
                }
            }finally{ reconnectLoop.set(false) }
        }
    }

    private fun manualDisconnect(){
        manualDisconnect=true; pollPaused=true; isConnected=false; reconnectLoop.set(false); stopScan()
        io.execute { closeCurrent() }
        commChip.visibility=View.INVISIBLE
        logger.log("CONNECT","MANUAL_DISCONNECT")
    }

    private fun closeCurrent(){
        val c=bms; bms=null; try{c?.close()}catch(_:Exception){}
        val s=session; session=null; try{s?.close()}catch(_:Exception){}
    }

    // ---------- automatic polling ----------
    private val pollRunnable=object:Runnable{
        override fun run(){
            mainHandler.postDelayed(this,1000)
            if(!isConnected || pollPaused || otaInProgress || !pollInFlight.compareAndSet(false,true)) return
            io.execute {
                try{
                    val snap=bms?.readBattery() ?: return@execute
                    lastSnapshot=snap; consecutivePollErrors=0
                    maybeRecordMonitor(snap)
                    runOnUiThread { applySnapshot(snap); commChip.visibility=View.VISIBLE }
                }catch(e:Exception){
                    consecutivePollErrors++; logger.log("POLL","FAIL $consecutivePollErrors/3 ${e.javaClass.simpleName}: ${e.message}")
                    if(consecutivePollErrors>=3){
                        val target=connectedDevice; isConnected=false; pollPaused=true; runOnUiThread { commChip.visibility=View.INVISIBLE }
                        closeCurrent(); if(target!=null && !manualDisconnect && !otaInProgress) requestSilentReconnect(target)
                    }
                }finally{ pollInFlight.set(false) }
            }
        }
    }

    private fun applyIdentity(id:DeviceIdentity){ identityText.text="硬件版本：${id.hardware.ifBlank { "—" }}    软件版本：${id.software.ifBlank { "—" }}    BMS序列号：${id.serial.ifBlank { "—" }}" }

    private fun applySnapshot(s:BatterySnapshot){
        totalVoltageText.text="%.2f V".format(Locale.US,s.packVoltageV); currentText.text="%.1f A".format(Locale.US,s.currentA); socText.text="${s.socPercent}%"; sohText.text="${s.sohPercent}%"; workStateText.text=s.workState
        capacityNowText.text="%.2f Ah".format(Locale.US,s.capacityNowAh); capacityFullText.text="%.2f Ah".format(Locale.US,s.capacityFullAh); cycleText.text=s.cycleCount.toString()
        cellMaxText.text="${s.maxCellMv} mV（${s.maxCellPosition}串）"; cellMinText.text="${s.minCellMv} mV（${s.minCellPosition}串）"; cellAvgText.text="%.1f mV".format(Locale.US,s.averageCellMv); cellDeltaText.text="${s.cellDeltaMv} mV"
        tempMaxText.text="%.1f ℃".format(Locale.US,s.maxTempC); tempMinText.text="%.1f ℃".format(Locale.US,s.minTempC); tempMosText.text="%.1f ℃".format(Locale.US,s.mosTempC)
        mosText.text="充电MOS：${onOff(s.chargeMosOn)}    放电MOS：${onOff(s.dischargeMosOn)}    预充MOS：${onOff(s.prechargeMosOn)}"
        functionText.text="AFE：${if(s.afe1On) "工作" else "关闭"}    均衡：${onOff(s.balancingOn)}    加热：${onOff(s.heatingOn)}    制冷：${onOff(s.coolingOn)}    系统：${if(s.preparingSleep) "准备休眠" else "运行中"}"
        protectionText.text="${s.protectionSummary}\n一级：${s.protectionLevel1Text}\n二级：${s.protectionLevel2Text}\n三级：${s.protectionLevel3Text}"
        rebuildCellGrid(s)
    }

    private fun rebuildCellGrid(s:BatterySnapshot){
        cellsGrid.removeAllViews()
        s.cellMillivolts.forEachIndexed { i,mv ->
            val t=TextView(this).apply { text="%02d    %d mV".format(i+1,mv); textSize=15f; gravity=Gravity.CENTER_VERTICAL; setPadding(dp(12),dp(8),dp(12),dp(8)); background=rounded(when(i+1){s.maxCellPosition->Color.rgb(255,225,225);s.minCellPosition->Color.rgb(220,245,220);else->Color.WHITE}) }
            cellsGrid.addView(t,GridLayout.LayoutParams().apply { width=0; height=ViewGroup.LayoutParams.WRAP_CONTENT; columnSpec=GridLayout.spec(GridLayout.UNDEFINED,1f); setMargins(dp(2),dp(2),dp(2),dp(2)) })
        }
    }

    // ---------- software protection ----------
    private fun readProtection(){
        pollPaused=true; runOnUiThread { protectionStatus.text="正在读取..." }
        io.execute { try{ val c=requireBms(); val raw=c.readProtectionAll(); protectionParams.forEach { it.deviceRaw=raw[it.index] }; runOnUiThread { protectionParams.forEach { p->protectionViews[p.index]?.let { v->v.current.text="当前：${p.display()} ${p.unit}"; v.edit.setText(p.display()) } }; protectionStatus.text="读取完成 · ${timeNow()}" } }
        catch(e:Exception){ logger.log("PARAM", "软件保护读取失败: ${e.message}"); runOnUiThread { protectionStatus.text="读取失败"; toast("软件保护参数读取失败") } } finally{ pollPaused=false } }
    }

    private fun confirmSaveProtection(){
        val edits=protectionParams.associate { it.index to (protectionViews[it.index]?.edit?.text?.toString() ?: "") }
        val changed=try{ protectionParams.mapNotNull { p-> val raw=p.parse(edits[p.index] ?: ""); if(raw!=p.deviceRaw) p to raw else null } }catch(e:Exception){ toast(e.message ?: "参数格式错误"); return }
        if(changed.isEmpty()){ toast("没有修改项"); return }
        AlertDialog.Builder(this).setTitle("保存软件保护参数").setMessage("将写入 ${changed.size} 项软件保护参数，并逐项回读验证。是否继续？").setNegativeButton("取消",null).setPositiveButton("写入") { _,_-> saveProtection(changed) }.show()
    }

    private fun saveProtection(changed:List<Pair<ProtectionParameter,Int>>){
        pollPaused=true; protectionStatus.text="正在写入 ${changed.size} 项..."
        io.execute { try{ val c=requireBms(); changed.forEachIndexed { i,(p,raw)-> c.writeSingleVerify(p.address,raw); logger.log("PARAM","SW_PROTECT_WRITE_OK ${p.name} value=$raw (${i+1}/${changed.size})") }; val all=c.readProtectionAll(); protectionParams.forEach { it.deviceRaw=all[it.index] }; runOnUiThread { protectionParams.forEach { p->protectionViews[p.index]?.let { v->v.current.text="当前：${p.display()} ${p.unit}";v.edit.setText(p.display()) } }; protectionStatus.text="保存成功并回读一致 · ${timeNow()}"; toast("软件保护参数保存成功") } }
        catch(e:Exception){ logger.log("PARAM","软件保护写入失败: ${e.message}"); runOnUiThread { protectionStatus.text="写入失败"; toast("写入失败：${e.message}") } } finally{ pollPaused=false } }
    }

    // ---------- AFE ----------
    private fun readAfe(){
        pollPaused=true; afeStatus.text="正在读取..."
        io.execute { try{ val raw=requireBms().readAfeAll(); afeCatalog.load(raw); runOnUiThread { afeCatalog.rows.forEach { p->afeViews[p.wireIndex]?.let { v->val text=afeCatalog.display(p);v.current.text="当前：$text ${p.unit}";v.edit.setText(text) } }; afeStatus.text="读取完成 · ${timeNow()}" } }
        catch(e:Exception){ logger.log("AFE","读取失败: ${e.message}"); runOnUiThread { afeStatus.text="读取失败";toast("AFE参数读取失败") } } finally{pollPaused=false} }
    }

    private fun confirmSaveAfe(){
        val edits=afeCatalog.rows.associate { it.wireIndex to (afeViews[it.wireIndex]?.edit?.text?.toString() ?: "") }
        val candidate=try{afeCatalog.buildCandidate(edits)}catch(e:Exception){toast(e.message ?: "AFE参数格式错误");return}
        val groups=afeCatalog.changedGroups(candidate); if(groups.isEmpty()){toast("没有修改项");return}
        AlertDialog.Builder(this).setTitle("保存AFE硬件保护参数").setMessage("将写入 ${groups.joinToString("、"){it.name}}。这是SH367309硬件保护参数，是否继续？").setNegativeButton("取消",null).setPositiveButton("写入") { _,_->saveAfe(candidate) }.show()
    }

    private fun saveAfe(candidate:IntArray){
        pollPaused=true; afeStatus.text="正在保存AFE参数..."
        io.execute { try{ val c=requireBms(); val groups=afeCatalog.changedGroups(candidate); groups.forEach { g->c.writeMultiple(g.startRegister,g.values);logger.log("AFE","WRITE_GROUP_OK ${g.name} start=0x%04X qty=${g.values.size}".format(g.startRegister)) }; val rb=c.readAfeAll();afeCatalog.verify(rb,candidate);runOnUiThread { afeCatalog.rows.forEach { p->afeViews[p.wireIndex]?.let {v->val text=afeCatalog.display(p);v.current.text="当前：$text ${p.unit}";v.edit.setText(text)} };afeStatus.text="保存成功并回读一致；AFE同步由固件异步完成 · ${timeNow()}";toast("AFE硬件参数保存成功") } }
        catch(e:Exception){logger.log("AFE","WRITE_FAIL ${e.message}");runOnUiThread{afeStatus.text="保存失败";toast("AFE参数保存失败：${e.message}")}}finally{pollPaused=false} }
    }

    // ---------- event logs ----------
    private fun readEventLogs(){
        pollPaused=true;eventLogStatus.text="正在读取100条..."
        io.execute { try{ val rows=EventLogs.decode(requireBms().readEventLogs()); val valid=rows.count{it.populated}; val text=buildString { rows.forEach { r->append("%03d  %-12s  %s\n".format(r.position,r.eventName,r.intervalText)) } }; runOnUiThread { eventLogText.text=text;eventLogStatus.text="读取完成 · 有效 $valid/100 · ${timeNow()}" } }
        catch(e:Exception){logger.log("LOG","EVENT_LOG_READ_FAIL ${e.message}");runOnUiThread{eventLogStatus.text="读取失败";toast("读取事件日志失败")}}finally{pollPaused=false} }
    }

    // ---------- device settings ----------
    private fun readBtName(){ pollPaused=true;io.execute { try{val name=requireBms().readBluetoothName();runOnUiThread{btNameEdit.setText(name);toast("蓝牙名：$name")}}catch(e:Exception){runOnUiThread{toast("读取失败：${e.message}")}}finally{pollPaused=false} } }
    private fun writeBtName(){ val text=btNameEdit.text.toString(); pollPaused=true;io.execute { try{val name=requireBms().writeBluetoothNameSuffix(text);identity=identity?.copy(bluetoothName=name);runOnUiThread{btNameEdit.setText(name);toast("蓝牙名已写入并回读：$name")}}catch(e:Exception){runOnUiThread{toast("改名失败：${e.message}")}}finally{pollPaused=false} } }
    private fun setSoc(){ val v=socEdit.text.toString().toIntOrNull() ?: run{toast("请输入0~100 SOC");return};pollPaused=true;io.execute{try{val s=requireBms().setSocAndVerify(v);lastSnapshot=s;runOnUiThread{applySnapshot(s);toast("SOC写入并验证成功")}}catch(e:Exception){runOnUiThread{toast("SOC写入失败：${e.message}")}}finally{pollPaused=false}} }
    private fun setCycle(){ val v=cycleEdit.text.toString().toIntOrNull() ?: run{toast("请输入循环次数");return};pollPaused=true;io.execute{try{val s=requireBms().setCycleAndVerify(v);lastSnapshot=s;runOnUiThread{applySnapshot(s);toast("循环次数写入并验证成功")}}catch(e:Exception){runOnUiThread{toast("写入失败：${e.message}")}}finally{pollPaused=false}} }

    // ---------- long-term monitoring ----------
    private fun startMonitoring(){
        monitorIntervalSec=when(monitorIntervalSpinner.selectedItemPosition){0->1;1->5;2->10;3->30;else->60}
        synchronized(monitorLock){monitorRecords.clear()}; monitorActive=true;monitorStartedAt=System.currentTimeMillis();monitorLastRecordAt=0
        monitorStatus.text="监控中 · ${monitorIntervalSec}秒/条 · 0条";logger.log("MONITOR","START interval=${monitorIntervalSec}s")
    }
    private fun stopMonitoring(){monitorActive=false;updateMonitorStatus();logger.log("MONITOR","STOP records=${synchronized(monitorLock){monitorRecords.size}}")}
    private fun maybeRecordMonitor(s:BatterySnapshot){
        if(!monitorActive)return;val now=System.currentTimeMillis();if(monitorLastRecordAt!=0L && now-monitorLastRecordAt<monitorIntervalSec*1000L)return
        val rec=MonitorRecord(now,identity?.bluetoothName ?: "",s,identity);synchronized(monitorLock){if(monitorRecords.size<1_000_000)monitorRecords.add(rec) else monitorActive=false};monitorLastRecordAt=now;runOnUiThread{updateMonitorStatus()}
    }
    private fun updateMonitorStatus(){ val count=synchronized(monitorLock){monitorRecords.size};val elapsed=if(monitorStartedAt==0L)0 else (System.currentTimeMillis()-monitorStartedAt)/1000;monitorStatus.text="${if(monitorActive)"监控中" else "已停止"} · ${monitorIntervalSec}秒/条 · ${count}条 · ${formatDuration(elapsed)}" }
    private fun exportMonitor(){ val data=synchronized(monitorLock){monitorRecords.toList()};if(data.isEmpty()){toast("没有可导出的监控数据");return};pendingExportRecords=data;val intent=Intent(Intent.ACTION_CREATE_DOCUMENT).apply{addCategory(Intent.CATEGORY_OPENABLE);type="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";putExtra(Intent.EXTRA_TITLE,"BMS_Monitor_${SimpleDateFormat("yyyyMMdd_HHmmss",Locale.US).format(Date())}.xlsx")};startActivityForResult(intent,REQ_XLSX) }

    // ---------- OTA ----------
    private fun chooseFirmware(){ val i=Intent(Intent.ACTION_OPEN_DOCUMENT).apply{addCategory(Intent.CATEGORY_OPENABLE);type="application/octet-stream"};startActivityForResult(i,REQ_BIN) }
    private fun startOta(){
        val image=firmwareImage ?: run{toast("请先选择Telink BIN固件");return}; val target=connectedDevice ?: run{toast("请先连接BMS");return}; if(!isConnected){toast("BMS当前未连接");return}
        val expected=expectedVersionEdit.text.toString().trim();pollPaused=true;otaInProgress=true;otaProgress.progress=0;otaStatus.text="正在升级...";otaLogText.text=""
        io.execute {
            val before=identity?.software ?: ""
            try{
                val s=session ?: throw IllegalStateException("BLE会话不存在")
                val controller=TelinkOtaController(s,{m->logger.log("OTA",m);runOnUiThread{appendOtaLine(m)}},{done,total,bytes,bps->runOnUiThread{otaProgress.progress=(done*100/total).coerceIn(0,100);otaStatus.text="发送中 ${done}/${total} · %.1f KB/s".format(Locale.US,bps/1024.0)}})
                controller.upgrade(image)
                runOnUiThread{otaStatus.text="OTA已被设备接受，等待重启并验证..."}
                logger.log("OTA","TRANSFER_COMPLETE beforeVersion='$before'; waiting reboot")
                isConnected=false;runOnUiThread{commChip.visibility=View.INVISIBLE};closeCurrent();Thread.sleep(1800)
                otaInProgress=false
                var ok=false
                for(i in 1..6){logger.log("OTA","POST_REBOOT_RECONNECT $i/6");if(connectBlocking(target,true)){ok=true;break};Thread.sleep(2000)}
                if(!ok)throw IllegalStateException("升级后无法重新连接BMS")
                val after=identity?.software ?: ""
                val snap=lastSnapshot ?: requireBms().readBattery()
                if(expected.isNotEmpty() && after!=expected)throw IllegalStateException("版本校验失败：期望 '$expected'，设备 '$after'")
                logger.log("OTA","VERIFIED before='$before' after='$after' appAlive=true")
                runOnUiThread{applySnapshot(snap);otaProgress.progress=100;otaStatus.text="升级成功 · VERIFIED · 软件版本 ${after.ifBlank{"可读但为空"}}";toast("OTA升级并重连验证成功")}
            }catch(e:Exception){logger.log("OTA","FAILED ${e.javaClass.simpleName}: ${e.message}");runOnUiThread{otaStatus.text="升级失败：${e.message}";toast("OTA失败，详情见日志")};if(!isConnected){otaInProgress=false;requestSilentReconnect(target)}}finally{otaInProgress=false;if(isConnected)pollPaused=false}
        }
    }

    override fun onActivityResult(requestCode:Int,resultCode:Int,data:Intent?){
        super.onActivityResult(requestCode,resultCode,data);if(resultCode!=RESULT_OK)return;val uri=data?.data ?: return
        when(requestCode){
            REQ_BIN->io.execute{try{val bytes=contentResolver.openInputStream(uri)?.use{it.readBytes()} ?: throw IllegalStateException("无法读取文件");val image=FirmwareImage.parse(bytes);val name=queryDisplayName(uri);firmwareImage=image;firmwareFileName=name;runOnUiThread{firmwareText.text="$name · ${image.imageSize} bytes · ${image.packetCount}包";toast("固件已加载")};logger.log("OTA","BIN_LOADED name='$name' size=${image.imageSize} packets=${image.packetCount}")}catch(e:Exception){runOnUiThread{toast("固件无效：${e.message}")}}}
            REQ_XLSX->{val records=pendingExportRecords ?: return;pendingExportRecords=null;io.execute{try{contentResolver.openOutputStream(uri,"w")?.use{XlsxWriter.write(it,records,monitorIntervalSec)} ?: throw IllegalStateException("无法创建Excel");logger.log("MONITOR","XLSX_EXPORT_OK records=${records.size}");runOnUiThread{toast("Excel导出完成")}}catch(e:Exception){logger.log("MONITOR","XLSX_EXPORT_FAIL ${e.message}");runOnUiThread{toast("Excel导出失败：${e.message}")}}}}
        }
    }

    // ---------- professional debug ----------
    private fun debugRead(addr:EditText,qty:EditText){ val a=parseNumber(addr.text.toString()) ?: run{toast("地址格式错误");return};val q=qty.text.toString().toIntOrNull() ?: 1;pollPaused=true;io.execute{try{val words=requireBms().readRegisters(a,q);val text=words.mapIndexed{i,v->"0x%04X = %5d  0x%04X".format(a+i,v,v)}.joinToString("\n");runOnUiThread{debugRawOutput.text=text}}catch(e:Exception){runOnUiThread{toast("读取失败：${e.message}")}}finally{pollPaused=false}} }
    private fun debugWrite(addr:EditText,value:EditText){val a=parseNumber(addr.text.toString())?:run{toast("地址格式错误");return};val v=parseNumber(value.text.toString())?:run{toast("值格式错误");return};AlertDialog.Builder(this).setTitle("专业寄存器写入").setMessage("写入 0x%04X = %d (0x%04X)？".format(a,v,v)).setNegativeButton("取消",null).setPositiveButton("写入"){_,_->pollPaused=true;io.execute{try{requireBms().writeSingle(a,v);runOnUiThread{toast("Modbus ACK成功")}}catch(e:Exception){runOnUiThread{toast("写入失败：${e.message}")}}finally{pollPaused=false}}}.show() }

    // ---------- helpers ----------
    private fun requireBms():BmsClient = bms ?: throw IllegalStateException("请先连接BMS")
    private fun parseNumber(text:String):Int?{val s=text.trim();return if(s.startsWith("0x",true))s.substring(2).toIntOrNull(16) else s.toIntOrNull()}
    private fun queryDisplayName(uri:android.net.Uri):String{ return try{contentResolver.query(uri,arrayOf(OpenableColumns.DISPLAY_NAME),null,null,null)?.use{if(it.moveToFirst())it.getString(0)else"firmware.bin"}?:"firmware.bin"}catch(_:Exception){"firmware.bin"} }
    private fun appendDebugLine(line:String){ val old=debugLogText.text?.toString() ?: "";val combined=if(old.length>60000)old.takeLast(45000)+"\n"+line else if(old.isEmpty())line else "$old\n$line";debugLogText.text=combined }
    private fun appendOtaLine(line:String){val old=otaLogText.text?.toString()?:"";otaLogText.text=if(old.length>20000)old.takeLast(15000)+"\n"+line else if(old.isEmpty())line else "$old\n$line"}
    private fun toast(text:String)=Toast.makeText(this,text,Toast.LENGTH_SHORT).show()
    private fun timeNow()=SimpleDateFormat("HH:mm:ss",Locale.US).format(Date())
    private fun formatDuration(seconds:Long)=String.format(Locale.US,"%02d:%02d:%02d",seconds/3600,(seconds%3600)/60,seconds%60)
    private fun onOff(v:Boolean)=if(v)"开启" else "关闭"
    private fun dp(v:Int)=(v*resources.displayMetrics.density).toInt()
    private fun vertical()=LinearLayout(this).apply{orientation=LinearLayout.VERTICAL;setPadding(dp(4),dp(4),dp(4),dp(4))}
    private fun horizontal()=LinearLayout(this).apply{orientation=LinearLayout.HORIZONTAL;gravity=Gravity.CENTER_VERTICAL}
    private fun button(text:String,onClick:()->Unit)=Button(this).apply{this.text=text;setOnClickListener{onClick()}}
    private fun section(parent:LinearLayout,title:String){parent.addView(TextView(this).apply{text=title;textSize=17f;setTypeface(typeface,Typeface.BOLD);setTextColor(Color.rgb(30,80,120));setPadding(dp(4),dp(12),dp(4),dp(6))})}
    private fun addValue(parent:LinearLayout,label:String):TextView{val row=horizontal();row.addView(TextView(this).apply{text=label;textSize=14f;setPadding(dp(8),dp(7),dp(8),dp(7))},LinearLayout.LayoutParams(0,ViewGroup.LayoutParams.WRAP_CONTENT,1f));val value=TextView(this).apply{text="—";textSize=15f;setTypeface(typeface,Typeface.BOLD);gravity=Gravity.END;setPadding(dp(8),dp(7),dp(8),dp(7))};row.addView(value,LinearLayout.LayoutParams(0,ViewGroup.LayoutParams.WRAP_CONTENT,1f));parent.addView(row);return value}
    private fun rounded(color:Int)=GradientDrawable().apply{setColor(color);cornerRadius=dp(6).toFloat();setStroke(dp(1),Color.rgb(220,220,220))}

    override fun onDestroy(){ manualDisconnect=true;monitorActive=false;mainHandler.removeCallbacksAndMessages(null);stopScan();try{closeCurrent()}catch(_:Exception){};io.shutdownNow();super.onDestroy() }
}
