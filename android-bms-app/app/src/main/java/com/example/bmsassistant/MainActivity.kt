package com.example.bmsassistant

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.*
import com.example.bmsassistant.ble.BleOtaSession
import com.example.bmsassistant.ble.BmsBleSession
import com.example.bmsassistant.bms.*
import com.example.bmsassistant.export.XlsxWriter
import com.example.bmsassistant.model.*
import com.example.bmsassistant.ota.FirmwareImage
import com.example.bmsassistant.ota.TelinkOtaController
import java.text.SimpleDateFormat
import java.util.*
import java.util.concurrent.CopyOnWriteArrayList
import kotlin.concurrent.thread

class MainActivity:Activity(){
    private lateinit var pageHost:FrameLayout; private lateinit var navBar:LinearLayout
    private val pages=LinkedHashMap<String,View>(); private val discovered=LinkedHashMap<String,BluetoothDevice>(); private val deviceNames=mutableListOf<String>();private lateinit var deviceAdapter:ArrayAdapter<String>
    private val main=Handler(Looper.getMainLooper()); private var scanner:BluetoothLeScanner?=null;private var scanning=false;private var selected:BluetoothDevice?=null;private var session:BmsBleSession?=null;private var bms:BmsClient?=null;@Volatile private var polling=false;@Volatile private var connecting=false;@Volatile private var manualDisconnect=false;private var everConnected=false
    @Volatile private var lastSnapshot:BatterySnapshot?=null;@Volatile private var identity:DeviceIdentity?=null
    private val diagnostics=StringBuilder();private var debugText:TextView?=null
    private lateinit var deviceSpinner:Spinner;private lateinit var commText:TextView;private lateinit var cellText:TextView;private lateinit var basicText:TextView;private lateinit var tempText:TextView;private lateinit var systemText:TextView;private lateinit var protectText:TextView;private lateinit var monitorText:TextView
    private val protectParams=ProtectionCatalog.create();private val protectEdits=mutableListOf<EditText>();private val protectCurrent=mutableListOf<TextView>();private val afeModel=AfeModel();private val afeEdits=mutableListOf<EditText>();private val afeCurrent=mutableListOf<TextView>()
    private lateinit var eventText:TextView;private lateinit var identityText:TextView;private lateinit var btNameEdit:EditText;private lateinit var otaStatus:TextView;private var firmware:FirmwareImage?=null
    private val monitor=CopyOnWriteArrayList<MonitorRecord>();@Volatile private var monitorRunning=false;private var monitorIntervalSec=5;private var lastMonitorMs=0L
    private val df=SimpleDateFormat("HH:mm:ss.SSS",Locale.US)

    override fun onCreate(savedInstanceState:Bundle?){super.onCreate(savedInstanceState);setContentView(R.layout.activity_main);pageHost=findViewById(R.id.pageHost);navBar=findViewById(R.id.navBar);buildPages();requestBtPermissions();showPage("实时监控");log("APP","启动；连接后1秒自动刷新")}
    override fun onDestroy(){polling=false;monitorRunning=false;stopScan();closeBms();super.onDestroy()}

    private fun buildPages(){pages["实时监控"]=buildMainPage();pages["软件保护"]=buildSoftwarePage();pages["AFE硬件"]=buildAfePage();pages["事件日志"]=buildEventPage();pages["设备/OTA"]=buildDeviceOtaPage();pages["专业调试"]=buildDebugPage();pages.keys.forEach{name->val b=Button(this);b.text=name;b.setOnClickListener{showPage(name)};navBar.addView(b)}}
    private fun showPage(name:String){pageHost.removeAllViews();pageHost.addView(pages.getValue(name),FrameLayout.LayoutParams(-1,-1))}
    private fun scroll(content:View):ScrollView{val s=ScrollView(this);s.addView(content);return s}
    private fun vertical()=LinearLayout(this).apply{orientation=LinearLayout.VERTICAL;setPadding(14,10,14,18)}
    private fun title(t:String)=TextView(this).apply{text=t;textSize=18f;setTextColor(Color.BLACK);setPadding(0,10,0,6)}
    private fun section(t:String)=TextView(this).apply{text=t;textSize=16f;setTextColor(Color.rgb(30,70,30));setPadding(0,12,0,4)}
    private fun button(t:String,click:()->Unit)=Button(this).apply{text=t;setOnClickListener{click()}}
    private fun row(vararg v:View)=LinearLayout(this).apply{orientation=LinearLayout.HORIZONTAL;gravity=Gravity.CENTER_VERTICAL;v.forEach{addView(it,LinearLayout.LayoutParams(0,-2,1f))}}

    private fun buildMainPage():View{
        val r=vertical();r.addView(title("BMS 实时监控"));deviceSpinner=Spinner(this);deviceAdapter=ArrayAdapter(this,android.R.layout.simple_spinner_dropdown_item,deviceNames);deviceSpinner.adapter=deviceAdapter
        r.addView(row(deviceSpinner,button("搜索"){startScan()},button("连接"){connectSelected()},button("断开"){manualDisconnect=true;polling=false;closeBms();setComm(false)}))
        commText=TextView(this).apply{text="";textSize=14f;setTextColor(Color.rgb(0,150,0));visibility=View.GONE};r.addView(commText)
        r.addView(section("单体电压"));cellText=TextView(this).apply{text="等待有效数据";textSize=15f};r.addView(cellText)
        r.addView(section("基础信息"));basicText=TextView(this).apply{textSize=16f};r.addView(basicText)
        r.addView(section("温度"));tempText=TextView(this).apply{textSize=15f};r.addView(tempText)
        r.addView(section("系统 / MOS 状态"));systemText=TextView(this).apply{textSize=15f};r.addView(systemText)
        r.addView(section("保护状态"));protectText=TextView(this).apply{textSize=15f};r.addView(protectText)
        r.addView(section("长期监控"));val interval=Spinner(this);val opts=listOf("1秒","5秒","10秒","30秒","60秒");interval.adapter=ArrayAdapter(this,android.R.layout.simple_spinner_dropdown_item,opts);interval.setSelection(1);interval.onItemSelectedListener=object:AdapterView.OnItemSelectedListener{override fun onNothingSelected(p:AdapterView<*>?){ } ;override fun onItemSelected(p:AdapterView<*>?,v:View?,pos:Int,id:Long){monitorIntervalSec=listOf(1,5,10,30,60)[pos]}}
        r.addView(row(interval,button("开始"){monitor.clear();lastMonitorMs=0;monitorRunning=true;updateMonitorText()},button("停止"){monitorRunning=false;updateMonitorText()},button("导出Excel"){exportMonitor()}));monitorText=TextView(this);r.addView(monitorText);updateMonitorText();return scroll(r)
    }

    private fun buildSoftwarePage():View{val r=vertical();r.addView(title("MCU 软件保护参数"));r.addView(TextView(this).apply{text="软件保护参数与AFE硬件保护参数相互独立。本页操作MCU软件保护。"});r.addView(row(button("读取全部"){readSoftware()},button("写入全部修改"){writeSoftwareChanged()}));protectParams.forEach{p->val cur=TextView(this).apply{text="—"};val edit=EditText(this).apply{hint=p.unit()};protectCurrent+=cur;protectEdits+=edit;val label=TextView(this).apply{text="${p.group} / ${p.stage} (${p.unit()})"};r.addView(label);r.addView(row(cur,edit,button("写入"){p.edit=edit.text.toString();runTask("软件参数写入"){requireBms().writeSoftwareParam(p);main.post{cur.text=p.display(p.device);edit.setText(p.edit)}}}))};return scroll(r)}
    private fun readSoftware()=runTask("读取软件保护"){val a=requireBms().readSoftwareProtection();protectParams.forEachIndexed{i,p->p.load(a[i])};main.post{protectParams.forEachIndexed{i,p->protectCurrent[i].text=p.display(p.device);protectEdits[i].setText(p.edit)}}}
    private fun writeSoftwareChanged()=runTask("保存软件保护"){protectParams.forEachIndexed{i,p->p.edit=protectEdits[i].text.toString()};protectParams.filter{runCatching{it.parse()!=it.device}.getOrDefault(false)}.forEach{requireBms().writeSoftwareParam(it)};main.post{protectParams.forEachIndexed{i,p->protectCurrent[i].text=p.display(p.device);protectEdits[i].setText(p.edit)}}}

    private fun buildAfePage():View{val r=vertical();r.addView(title("SH367309 AFE 硬件保护参数"));r.addView(TextView(this).apply{text="独立于软件保护。仅支持芯片离散档位；保存按关联参数组使用0x10写入并整块回读确认。"});r.addView(row(button("读取AFE"){readAfe()},button("保存修改"){writeAfe()}));afeModel.rows.forEach{p->val cur=TextView(this).apply{text="—"};val edit=EditText(this);afeCurrent+=cur;afeEdits+=edit;r.addView(TextView(this).apply{text="${p.group} / ${p.name} (${p.unit}) · ${p.hint}"});r.addView(row(cur,edit))};return scroll(r)}
    private fun readAfe()=runTask("读取AFE参数"){afeModel.load(requireBms().readAfe());main.post{afeModel.rows.forEachIndexed{i,p->afeCurrent[i].text=p.decode(p.device);afeEdits[i].setText(p.edit)}}}
    private fun writeAfe()=runTask("保存AFE参数"){afeModel.rows.forEachIndexed{i,p->p.edit=afeEdits[i].text.toString()};requireBms().writeAfe(afeModel);main.post{afeModel.rows.forEachIndexed{i,p->afeCurrent[i].text=p.decode(p.device);afeEdits[i].setText(p.edit)}}}

    private fun buildEventPage():View{val r=vertical();r.addView(title("设备事件日志"));r.addView(button("读取最近100条"){readEvents()});eventText=TextView(this).apply{setTextIsSelectable(true)};r.addView(eventText);return scroll(r)}
    private fun readEvents()=runTask("读取事件日志"){val w=requireBms().readRegisters(BmsRegisters.EVENT_LOG,100);val sb=StringBuilder();var valid=0;w.forEachIndexed{i,x->val id=x shr 8;val code=x and 0xFF;if(id!=0){valid++;sb.append("%03d  %-12s  %s\n".format(i+1,eventName(id),eventInterval(code)))}};main.post{eventText.text="有效 $valid/100，最新在前\n\n$sb"}}
    private fun eventName(id:Int)=when(id){1->"BMS启动";2->"进入休眠";3->"均衡开启";4->"加热开启";5->"制冷开启";6->"单体过压";7->"总压过压";8->"充电过流";9->"单体欠压";10->"总压欠压";11->"放电过流";12->"充电低温";13->"放电低温";14->"充电高温";15->"放电高温";16->"单体压差保护";17->"CBC错误";18->"AFE1错误";19->"AFE2错误";20->"EEPROM错误";else->"未知事件($id)"}
    private fun eventInterval(c:Int)=when{c==0->"启动记录/0";c==171->"≤1分钟";c==170->">168小时";c in 1..168->"约${c}小时";else->"未知间隔$c"}

    private fun buildDeviceOtaPage():View{val r=vertical();r.addView(title("设备 / 蓝牙名 / OTA"));identityText=TextView(this);r.addView(identityText);r.addView(button("读取设备信息"){readIdentity()});btNameEdit=EditText(this).apply{hint="蓝牙名后缀，例如 13s-007"};r.addView(row(btNameEdit,button("修改蓝牙名"){runTask("修改蓝牙名"){val n=requireBms().writeBluetoothName(btNameEdit.text.toString());identity=identity?.copy(bluetoothName=n);main.post{Toast.makeText(this,n,Toast.LENGTH_SHORT).show()}}}));r.addView(section("Telink OTA"));r.addView(row(button("选择BIN"){pickBin()},button("开始OTA"){startOta()}));otaStatus=TextView(this).apply{text="未选择固件"};r.addView(otaStatus);return scroll(r)}
    private fun showIdentity(){val x=identity;identityText.text=if(x==null)"未读取" else "蓝牙：${x.bluetoothName}\nMAC：${x.mac}\nSN：${x.serial}\n硬件：${x.hardware}\n软件：${x.software}"}

    private fun buildDebugPage():View{val r=vertical();r.addView(title("专业调试"));val addr=EditText(this).apply{hint="地址，如 D120"};val qty=EditText(this).apply{hint="数量"};val writeAddr=EditText(this).apply{hint="写地址"};val value=EditText(this).apply{hint="写入值"};r.addView(row(addr,qty,button("原始读取"){runTask("调试读取"){val a=parseHex(addr.text.toString());val q=qty.text.toString().toIntOrNull()?:1;val w=requireBms().readRegisters(a,q);log("DEBUG","READ 0x%04X: %s".format(a,w.joinToString{"0x%04X".format(it)}))}}));r.addView(row(writeAddr,value,button("0x06写入"){runTask("调试写入"){requireBms().writeSingle(parseHex(writeAddr.text.toString()),parseNumber(value.text.toString()))}}));debugText=TextView(this).apply{setTextIsSelectable(true);text=diagnostics.toString()};r.addView(debugText);return scroll(r)}

    private fun requestBtPermissions(){if(Build.VERSION.SDK_INT>=31){val missing=listOf(Manifest.permission.BLUETOOTH_SCAN,Manifest.permission.BLUETOOTH_CONNECT).filter{checkSelfPermission(it)!=PackageManager.PERMISSION_GRANTED};if(missing.isNotEmpty())requestPermissions(missing.toTypedArray(),44)}else if(checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)!=PackageManager.PERMISSION_GRANTED)requestPermissions(arrayOf(Manifest.permission.ACCESS_FINE_LOCATION),44)}
    @SuppressLint("MissingPermission") private fun startScan(){if(!hasScanPermission()){requestBtPermissions();return};stopScan();discovered.clear();deviceNames.clear();deviceAdapter.notifyDataSetChanged();scanner=(getSystemService(BLUETOOTH_SERVICE)as BluetoothManager).adapter.bluetoothLeScanner;scanner?.startScan(scanCb);scanning=true;log("SCAN","start filter=BT_*");main.postDelayed({stopScan()},8000)}
    @SuppressLint("MissingPermission") private fun stopScan(){if(scanning)try{scanner?.stopScan(scanCb)}catch(_:Exception){};scanning=false}
    private val scanCb=object:ScanCallback(){override fun onScanResult(type:Int,r:ScanResult){val name=if(Build.VERSION.SDK_INT>=31&&checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)!=PackageManager.PERMISSION_GRANTED)return else r.scanRecord?.deviceName?:runCatching{r.device.name}.getOrNull();if(name?.startsWith("BT_",true)!=true)return;val key=r.device.address;if(!discovered.containsKey(key)){discovered[key]=r.device;deviceNames.add("$name  ${r.rssi}dBm  $key");main.post{deviceAdapter.notifyDataSetChanged()}}}}
    private fun connectSelected(){val pos=deviceSpinner.selectedItemPosition;if(pos<0||pos>=discovered.size){Toast.makeText(this,"请先搜索并选择BMS",Toast.LENGTH_SHORT).show();return};selected=discovered.values.elementAt(pos);manualDisconnect=false;thread(name="BmsConnect"){connectFull(showError=true)}}
    private fun connectFull(showError:Boolean=false){if(connecting)return;connecting=true;try{stopScan();closeBms();val dev=selected?:return;var last:Exception?=null;for(attempt in 1..3){try{log("CONNECT","attempt $attempt/3");val s=BmsBleSession(this,dev,{log("BLE",it)}){onUnexpectedDisconnect()};s.connect();val c=BmsClient(s){log("MODBUS",it)};c.probe();val id=runCatching{c.readIdentity()}.getOrNull();val snap=c.readBattery();session=s;bms=c;identity=id;everConnected=true;lastSnapshot=snap;main.post{applySnapshot(snap);showIdentity();setComm(true)};startPolling();return}catch(e:Exception){last=e;log("CONNECT","attempt $attempt fail ${e.javaClass.simpleName}: ${e.message}");closeBms();Thread.sleep(500)}};throw last?:IllegalStateException("连接失败") }catch(e:Exception){log("CONNECT","final fail ${e.message}");if(showError)main.post{Toast.makeText(this,"连接失败，详细原因见专业调试",Toast.LENGTH_LONG).show()};setComm(false)}finally{connecting=false}}
    private fun onUnexpectedDisconnect(){if(manualDisconnect)return;polling=false;setComm(false);log("BLE","unexpected disconnect; silent reconnect");main.postDelayed({if(!manualDisconnect)thread{connectFull(false)}},1500)}
    private fun startPolling(){if(polling)return;polling=true;thread(name="BmsPoll"){while(polling&&!manualDisconnect){try{val s=requireBms().readBattery();lastSnapshot=s;main.post{applySnapshot(s);setComm(true)};recordMonitor(s);Thread.sleep(1000)}catch(e:Exception){log("POLL","${e.javaClass.simpleName}: ${e.message}");polling=false;setComm(false);if(!manualDisconnect)main.postDelayed({thread{connectFull(false)}},1200)}}}}
    private fun closeBms(){try{bms?.close()}catch(_:Exception){};bms=null;try{session?.close()}catch(_:Exception){};session=null}
    private fun requireBms()=bms?:throw IllegalStateException("请先连接BMS")
    private fun setComm(ok:Boolean){main.post{if(ok){commText.text="通信：正在通讯";commText.visibility=View.VISIBLE}else{commText.visibility=View.GONE}}}

    private fun applySnapshot(s:BatterySnapshot){val avg=if(s.cells.isEmpty())0.0 else s.cells.average();cellText.text="最高 ${s.maxCellMv}mV（${s.maxCellPos}）    最低 ${s.minCellMv}mV（${s.minCellPos}）\n平均 %.1fmV    最大压差 ${s.deltaMv}mV\n\n".format(avg)+s.cells.mapIndexed{i,v->"%02d  %dmV".format(i+1,v)}.chunked(2).joinToString("\n"){it.joinToString("        ")};basicText.text="总压 %.2f V    电流 %.1f A\nSOC ${s.soc}%    SOH ${s.soh}%    ${s.workState}\n剩余 %.2f Ah    满充 %.2f Ah\n循环 ${s.cycleCount}".format(s.packVoltageV,s.currentA,s.capacityNowAh,s.capacityFullAh);tempText.text="最高 %.1f℃    最低 %.1f℃    MOS %.1f℃".format(s.maxTempC,s.minTempC,s.mosTempC);systemText.text="充电MOS：${onOff(s.chargeMos)}    放电MOS：${onOff(s.dischargeMos)}    预充MOS：${onOff(s.prechargeMos)}\n加热：${onOff(s.heating)}    制冷：${onOff(s.cooling)}    均衡：${onOff(s.balancing)}\nAFE1：${if(s.afe1)"工作"else"关闭"}    AFE2：${if(s.afe2)"工作"else"关闭"}    系统：${if(s.preparingSleep)"准备休眠"else"运行中"}";protectText.text="保护状态：${s.protectionSummary}\n一级：${s.protectText(s.protect1)}\n二级：${s.protectText(s.protect2)}\n三级：${s.protectText(s.protect3)}"}
    private fun onOff(v:Boolean)=if(v)"开启"else"关闭"

    private fun recordMonitor(s:BatterySnapshot){if(!monitorRunning)return;val now=System.currentTimeMillis();if(lastMonitorMs==0L||now-lastMonitorMs>=monitorIntervalSec*1000L){lastMonitorMs=now;if(monitor.size<1_000_000)monitor+=MonitorRecord(s,identity);main.post{updateMonitorText()}}}
    private fun updateMonitorText(){if(::monitorText.isInitialized)monitorText.text="${if(monitorRunning)"监控中"else"已停止"} · ${monitorIntervalSec}s/条 · ${monitor.size}条"}
    private fun exportMonitor(){if(monitor.isEmpty()){Toast.makeText(this,"没有监控记录",Toast.LENGTH_SHORT).show();return};val i=Intent(Intent.ACTION_CREATE_DOCUMENT).apply{type="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";putExtra(Intent.EXTRA_TITLE,"BMS_Monitor_${SimpleDateFormat("yyyyMMdd_HHmmss",Locale.US).format(Date())}.xlsx")};startActivityForResult(i,REQ_XLSX)}
    private fun pickBin(){startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply{type="application/octet-stream";addCategory(Intent.CATEGORY_OPENABLE)},REQ_BIN)}
    private fun startOta(){val img=firmware?:run{Toast.makeText(this,"请先选择BIN",Toast.LENGTH_SHORT).show();return};val dev=selected?:run{Toast.makeText(this,"请先连接/选择BMS",Toast.LENGTH_SHORT).show();return};polling=false;thread(name="OTA"){try{val oldVersion=identity?.software.orEmpty();closeBms();main.post{otaStatus.text="OTA连接中..."};val os=BleOtaSession(this,dev){log("OTA",it)};os.connect();val ctl=TelinkOtaController(os,{log("OTA",it)}){done,total,_,bps->main.post{otaStatus.text="OTA ${done*100/total}% · %.1f KB/s".format(bps/1024)}};ctl.upgrade(img);os.close();main.post{otaStatus.text="OTA发送完成，等待重启并验证"};Thread.sleep(2200);connectFull(false);val newVersion=identity?.software.orEmpty();if(bms!=null){val snap=requireBms().readBattery();main.post{applySnapshot(snap);otaStatus.text="升级后已重连，APP通信正常\n软件版本：$oldVersion → $newVersion"}}else main.post{otaStatus.text="OTA发送完成，但自动重连验证失败"}}catch(e:Exception){log("OTA","FAIL ${e.message}");main.post{otaStatus.text="OTA失败：${e.message}"}}}}

    override fun onActivityResult(requestCode:Int,resultCode:Int,data:Intent?){super.onActivityResult(requestCode,resultCode,data);if(resultCode!=RESULT_OK)return;val uri=data?.data?:return;when(requestCode){REQ_BIN->runCatching{val bytes=contentResolver.openInputStream(uri)!!.use{it.readBytes()};firmware=FirmwareImage.fromBytes(uri.lastPathSegment?:"firmware.bin",bytes);otaStatus.text="已选择：${firmware!!.sourceName}\n${firmware!!.imageSize} bytes · ${firmware!!.packetCount} packets"}.onFailure{otaStatus.text="BIN无效：${it.message}"};REQ_XLSX->thread{runCatching{contentResolver.openOutputStream(uri)!!.use{XlsxWriter.write(monitor.toList(),it,monitorIntervalSec)}}.onSuccess{main.post{Toast.makeText(this,"Excel导出完成",Toast.LENGTH_SHORT).show()}}.onFailure{main.post{Toast.makeText(this,"导出失败：${it.message}",Toast.LENGTH_LONG).show()}}}}}

    private fun runTask(name:String,task:()->Unit){thread(name=name){try{task();log("TASK","$name OK");main.post{Toast.makeText(this,"$name 成功",Toast.LENGTH_SHORT).show()}}catch(e:Exception){log("TASK","$name FAIL ${e.javaClass.simpleName}: ${e.message}");main.post{Toast.makeText(this,"$name 失败：${e.message}",Toast.LENGTH_LONG).show()}}}}
    private fun readIdentity(){runTask("读取设备信息"){identity=requireBms().readIdentity();main.post{showIdentity()}}}
    private fun log(tag:String,msg:String){val line="${df.format(Date())} [$tag] $msg\n";synchronized(diagnostics){diagnostics.append(line);if(diagnostics.length>200_000)diagnostics.delete(0,50_000)};main.post{debugText?.text=diagnostics.toString()}}
    private fun hasScanPermission()=if(Build.VERSION.SDK_INT>=31)checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN)==PackageManager.PERMISSION_GRANTED&&checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)==PackageManager.PERMISSION_GRANTED else checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)==PackageManager.PERMISSION_GRANTED
    private fun parseHex(s:String):Int=s.trim().removePrefix("0x").removePrefix("0X").toInt(16)
    private fun parseNumber(s:String):Int{val t=s.trim();return if(t.startsWith("0x",true))t.substring(2).toInt(16)else t.toInt()}
    companion object{const val REQ_BIN=701;const val REQ_XLSX=702}
}
