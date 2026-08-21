#if os(iOS)
import SwiftUI

struct MobileContentView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        TabView {
            NavigationStack { DeviceMobileView() }
                .tabItem { Label("设备", systemImage: "antenna.radiowaves.left.and.right") }
            NavigationStack { BatteryMobileView() }
                .tabItem { Label("电池", systemImage: "battery.100") }
            NavigationStack { EngineeringMobileView() }
                .tabItem { Label("工程", systemImage: "wrench.and.screwdriver") }
        }
        .tint(.teal)
    }
}

private struct DeviceMobileView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        @Bindable var model = model
        List {
            Section("连接状态") {
                LabeledContent("Bluetooth", value: model.bluetoothStateLabel)
                LabeledContent("BMS", value: model.connectionStatus.rawValue)
                Text(model.statusMessage).font(.footnote).foregroundStyle(.secondary)
            }
            Section("扫描") {
                Picker("扫描模式", selection: $model.scanMode) {
                    ForEach(ScanMode.allCases) { mode in Text(mode.rawValue).tag(mode) }
                }
                TextField("设备名过滤，例如 BT", text: $model.searchText)
                Toggle("只显示疑似 BMS", isOn: $model.showOnlyLikelyBMS)
                HStack {
                    Button(model.isScanning ? "停止扫描" : "开始扫描") { model.toggleScan() }
                        .buttonStyle(.borderedProminent)
                    Button("清空") { model.clearDevices() }.buttonStyle(.bordered)
                }
            }
            Section("附近设备") {
                if model.filteredDevices.isEmpty {
                    ContentUnavailableView(
                        "尚未发现 BMS",
                        systemImage: "antenna.radiowaves.left.and.right",
                        description: Text("启动扫描后，优先选择名称以 BT_ 开头的设备。")
                    )
                } else {
                    ForEach(model.filteredDevices) { device in
                        Button { model.connect(to: device.id) } label: {
                            HStack {
                                VStack(alignment: .leading, spacing: 4) {
                                    Text(device.displayName).font(.headline)
                                    Text("\(device.rssiSummary) · \(device.advertisedServicesSummary)")
                                        .font(.caption).foregroundStyle(.secondary).lineLimit(2)
                                }
                                Spacer()
                                if device.isConnected {
                                    Image(systemName: "checkmark.circle.fill").foregroundStyle(.green)
                                }
                            }
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
            if model.connectionStatus == .ready {
                Section { Button("断开连接", role: .destructive) { model.disconnect() } }
            }
        }
        .navigationTitle("BMS 设备")
    }
}

private struct BatteryMobileView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                HStack {
                    VStack(alignment: .leading, spacing: 4) {
                        Text(model.connectedDeviceName).font(.title2.bold())
                        Text(model.batteryStatus.sourceTitle).font(.caption).foregroundStyle(.secondary)
                    }
                    Spacer()
                    Button("刷新") { model.refreshBatteryStatus() }
                        .buttonStyle(.borderedProminent).disabled(!model.canSendCommands)
                }
                LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
                    MobileMetric(title: "总压", value: model.batteryStatus.packVoltageText)
                    MobileMetric(title: "电流", value: model.batteryStatus.currentText)
                    MobileMetric(title: "SOC", value: model.batteryStatus.socText)
                    MobileMetric(title: "SOH", value: model.batteryStatus.sohText)
                    MobileMetric(title: "最高温度", value: model.batteryStatus.maxTempText)
                    MobileMetric(title: "MOS 温度", value: model.batteryStatus.mosTempText)
                    MobileMetric(title: "最高单体", value: model.batteryStatus.maxCellVoltageText)
                    MobileMetric(title: "压差", value: model.batteryStatus.cellDeltaText)
                }
                GroupBox("单体电压") {
                    LazyVGrid(columns: [GridItem(.adaptive(minimum: 105))], spacing: 8) {
                        ForEach(model.batteryStatus.cellVoltages) { cell in
                            VStack(spacing: 3) {
                                Text(cell.title).font(.caption).foregroundStyle(.secondary)
                                Text(cell.voltageText).font(.headline.monospacedDigit())
                            }
                            .frame(maxWidth: .infinity).padding(10)
                            .background(.quaternary, in: RoundedRectangle(cornerRadius: 10))
                        }
                    }
                }
                GroupBox("系统状态") {
                    VStack(alignment: .leading, spacing: 8) {
                        Text(model.batteryStatus.systemStatusHexText).font(.body.monospaced())
                        if model.batteryStatus.activeStatusFlags.isEmpty {
                            Text("当前没有活动状态位").foregroundStyle(.secondary)
                        } else {
                            ForEach(model.batteryStatus.activeStatusFlags) { flag in
                                Label(flag.title, systemImage: "checkmark.circle.fill").foregroundStyle(.green)
                            }
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
                GroupBox("设备信息") {
                    VStack(alignment: .leading, spacing: 8) {
                        LabeledContent("SN", value: model.identity.serialNumber)
                        LabeledContent("HW", value: model.identity.hardwareVersion)
                        LabeledContent("SW", value: model.identity.softwareVersion)
                        LabeledContent("MAC", value: model.identity.macAddress)
                        Button("刷新设备信息") { model.refreshIdentity() }.disabled(!model.canSendCommands)
                    }
                }
            }
            .padding()
        }
        .navigationTitle("电池状态")
    }
}

private struct EngineeringMobileView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        @Bindable var model = model
        Form {
            Section("链路测试") {
                Button("发送 Echo 测试") { model.sendEchoTest() }.disabled(!model.canSendCommands)
            }
            Section("工程写操作") {
                TextField("SOC 0~100", text: $model.quickSOCValue).keyboardType(.numberPad)
                Button("写入 SOC") { model.writeSOCValue() }.disabled(!model.canSendCommands)
                TextField("蓝牙名后缀（最多 10 ASCII）", text: $model.btNameSuffix)
                    .textInputAutocapitalization(.characters)
                Button("写入蓝牙名称") { model.writeBluetoothNameSuffix() }.disabled(!model.canSendCommands)
            }
            Section("读取") {
                Button("读取保护参数预览") { model.readProtectPreview() }.disabled(!model.canSendCommands)
                Button("读取事件日志预览") { model.readEventLogPreview() }.disabled(!model.canSendCommands)
                Button("读取 SystemStatus") { model.readSystemStatus() }.disabled(!model.canSendCommands)
            }
            Section("安全提示") {
                Text("当前固件 BLE SMP 未启用。客户版 App 不应默认开放保护参数、调试寄存器、日志清除和 OTA 等危险写操作；本页定位为工程联调入口。")
                    .font(.footnote).foregroundStyle(.orange)
            }
        }
        .navigationTitle("工程工具")
    }
}

private struct MobileMetric: View {
    let title: String
    let value: String
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title).font(.caption).foregroundStyle(.secondary)
            Text(value).font(.title3.bold().monospacedDigit())
        }
        .frame(maxWidth: .infinity, alignment: .leading).padding(14)
        .background(.background, in: RoundedRectangle(cornerRadius: 14))
        .overlay(RoundedRectangle(cornerRadius: 14).stroke(.quaternary))
    }
}
#endif
