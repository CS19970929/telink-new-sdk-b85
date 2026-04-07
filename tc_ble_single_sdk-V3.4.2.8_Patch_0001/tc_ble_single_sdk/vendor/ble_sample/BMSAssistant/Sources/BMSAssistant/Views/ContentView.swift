import SwiftUI

private enum AppPalette {
    static let backgroundTop = Color(red: 0.97, green: 0.98, blue: 0.95)
    static let backgroundMiddle = Color(red: 0.93, green: 0.97, blue: 0.99)
    static let backgroundBottom = Color(red: 0.99, green: 0.96, blue: 0.92)
    static let cardFill = Color.white.opacity(0.92)
    static let cardStroke = Color(red: 0.75, green: 0.82, blue: 0.82).opacity(0.38)
    static let groupFill = Color(red: 0.10, green: 0.20, blue: 0.18).opacity(0.05)
    static let codeFill = Color(red: 0.08, green: 0.16, blue: 0.15).opacity(0.06)
    static let accent = Color(red: 0.05, green: 0.58, blue: 0.50)
    static let accentSoft = Color(red: 0.92, green: 0.58, blue: 0.20)
    static let success = Color(red: 0.15, green: 0.63, blue: 0.34)
    static let warning = Color(red: 0.89, green: 0.53, blue: 0.14)
}

private enum BatteryRefreshInterval: String, CaseIterable, Identifiable {
    case oneSecond = "1s"
    case twoSeconds = "2s"
    case fiveSeconds = "5s"

    var id: String { rawValue }

    var duration: Duration {
        switch self {
        case .oneSecond:
            .seconds(1)
        case .twoSeconds:
            .seconds(2)
        case .fiveSeconds:
            .seconds(5)
        }
    }
}

struct ContentView: View {
    var body: some View {
        NavigationSplitView {
            SidebarPane()
        } detail: {
            DetailPane()
        }
        .navigationSplitViewStyle(.balanced)
        .preferredColorScheme(.light)
        .tint(AppPalette.accent)
        .background(
            LinearGradient(
                colors: [
                    AppPalette.backgroundTop,
                    AppPalette.backgroundMiddle,
                    AppPalette.backgroundBottom,
                ],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
        )
    }
}

private struct SidebarPane: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        @Bindable var model = model

        VStack(spacing: 16) {
            VStack(alignment: .leading, spacing: 8) {
                Text("BMS Assistant")
                    .font(.system(size: 28, weight: .bold, design: .rounded))

                Text("BLE 调试上位机")
                    .font(.headline)
                    .foregroundStyle(.secondary)
            }
            .frame(maxWidth: .infinity, alignment: .leading)

            PanelCard(title: "扫描控制", systemImage: "antenna.radiowaves.left.and.right") {
                Picker("扫描模式", selection: $model.scanMode) {
                    ForEach(ScanMode.allCases) { mode in
                        Text(mode.rawValue).tag(mode)
                    }
                }
                .pickerStyle(.segmented)

                TextField("设备名过滤，例如 BT", text: $model.searchText)
                    .textFieldStyle(.roundedBorder)

                Toggle("只显示疑似 BMS 设备", isOn: $model.showOnlyLikelyBMS)

                Text("当前固件名称在 scan response 里，广告 UUID 通常包含 `180F` / `1812`。推荐先用“当前固件”模式，再切到“全部设备”做兜底。")
                    .font(.footnote)
                    .foregroundStyle(.secondary)

                HStack {
                    Button(model.isScanning ? "停止扫描" : "开始扫描") {
                        model.toggleScan()
                    }
                    .buttonStyle(.borderedProminent)

                    Button("清空列表") {
                        model.clearDevices()
                    }
                    .buttonStyle(.bordered)

                    Button("连接所选设备") {
                        model.connectSelected()
                    }
                    .buttonStyle(.bordered)
                    .disabled(model.selectedDeviceID == nil)

                    Button("断开") {
                        model.disconnect()
                    }
                    .buttonStyle(.bordered)
                }

                HStack {
                    StatusBadge(title: "Bluetooth", value: model.bluetoothStateLabel, tint: model.bluetoothState == .poweredOn ? .green : .orange)
                    StatusBadge(title: "链路", value: model.connectionStatus.rawValue, tint: model.connectionStatus == .ready ? .green : .blue)
                }

                Text("已发现 \(model.devices.count) 台设备")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }

            PanelCard(title: "设备列表", systemImage: "dot.radiowaves.left.and.right") {
                List(selection: $model.selectedDeviceID) {
                    ForEach(model.filteredDevices) { device in
                        DeviceRow(device: device)
                            .tag(device.id)
                    }
                }
                .listStyle(.plain)
                .scrollContentBackground(.hidden)
                .background(AppPalette.groupFill, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
                .frame(minHeight: 340)
            }

            PanelCard(title: "当前状态", systemImage: "text.bubble") {
                Text(model.statusMessage)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .font(.body)
                    .textSelection(.enabled)
            }
        }
        .padding(20)
    }
}

private struct DetailPane: View {
    @State private var selectedPage: DetailPage = .batteryStatus

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Picker("页面", selection: $selectedPage) {
                    ForEach(DetailPage.allCases) { page in
                        Text(page.rawValue).tag(page)
                    }
                }
                .pickerStyle(.segmented)
                .frame(width: 280)

                Spacer()
            }
            .padding(.horizontal, 20)
            .padding(.top, 20)
            .padding(.bottom, 8)

            Group {
                switch selectedPage {
                case .batteryStatus:
                    BatteryStatusPane()
                case .debugWorkbench:
                    DebugWorkbenchPane()
                }
            }
        }
    }
}

private struct BatteryStatusPane: View {
    @Environment(AppModel.self) private var model
    @State private var autoRefreshEnabled = true
    @State private var refreshInterval: BatteryRefreshInterval = .twoSeconds

    var body: some View {
        @Bindable var model = model

        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                PanelCard(title: "电池状态总览", systemImage: "battery.100.bolt") {
                    HStack(alignment: .top) {
                        VStack(alignment: .leading, spacing: 8) {
                            Text(model.connectedDeviceName)
                                .font(.system(size: 30, weight: .bold, design: .rounded))

                            Text("这一页只展示业务数据，不放寄存器调试控件。上位机优先读取 `0xD120~0xD12A`，如果板子还是旧固件，则自动退回到 `0xD000~0xD01F` 和 `0xD115~0xD116`。")
                                .foregroundStyle(.secondary)
                        }

                        Spacer()

                        VStack(alignment: .trailing, spacing: 10) {
                            HStack(spacing: 10) {
                                Button("刷新状态") {
                                    model.refreshBatteryStatus()
                                }
                                .buttonStyle(.borderedProminent)
                                .disabled(!model.canSendCommands)

                                Toggle("自动刷新", isOn: $autoRefreshEnabled)
                                    .toggleStyle(.switch)
                                    .labelsHidden()

                                Picker("刷新间隔", selection: $refreshInterval) {
                                    ForEach(BatteryRefreshInterval.allCases) { interval in
                                        Text(interval.rawValue).tag(interval)
                                    }
                                }
                                .pickerStyle(.segmented)
                                .frame(width: 150)
                                .disabled(!autoRefreshEnabled)
                            }

                            StatusBadge(
                                title: "方向",
                                value: model.batteryStatus.currentDirectionText,
                                tint: currentDirectionTint(model.batteryStatus)
                            )

                            StatusBadge(
                                title: "数据源",
                                value: model.batteryStatus.sourceTitle,
                                tint: sourceTint(model.batteryStatus)
                            )

                            Text("更新时间：\(model.batteryStatus.updatedAtText)")
                                .font(.footnote)
                                .foregroundStyle(.secondary)
                        }
                    }

                    Text(model.batteryStatus.sourceNote)
                        .font(.footnote)
                        .foregroundStyle(.secondary)

                    Text("自动刷新只会在当前页运行，不影响调试工作台。")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }

                PanelCard(title: "关键指标", systemImage: "gauge.with.dots.needle.50percent") {
                    LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())], spacing: 14) {
                        BatteryMetricCard(title: "Pack Voltage", value: model.batteryStatus.packVoltageText, detail: model.batteryStatus.packVoltageDetailText, tint: AppPalette.accent)
                        BatteryMetricCard(title: "Pack Current", value: model.batteryStatus.currentText, detail: model.batteryStatus.currentDirectionText, tint: currentDirectionTint(model.batteryStatus))
                        BatteryMetricCard(title: "SOC", value: model.batteryStatus.socText, detail: model.batteryStatus.supportsRealtimeWindow ? "版本 \(model.batteryStatus.protocolVersion)" : "旧布局 `SocElement.u16Soc`", tint: AppPalette.accentSoft)
                        BatteryMetricCard(title: "Max Temp", value: model.batteryStatus.maxTempText, detail: "raw \(model.batteryStatus.maxTempRaw)", tint: .red)
                        BatteryMetricCard(title: "Min Temp", value: model.batteryStatus.minTempText, detail: "raw \(model.batteryStatus.minTempRaw)", tint: .blue)
                        BatteryMetricCard(title: "MOS Temp", value: model.batteryStatus.mosTempText, detail: "raw \(model.batteryStatus.mosTempRaw)", tint: .orange)
                    }
                }

                PanelCard(title: "辅助指标", systemImage: "chart.xyaxis.line") {
                    LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())], spacing: 14) {
                        BatteryMetricCard(title: "Cell Max", value: model.batteryStatus.maxCellVoltageText, detail: model.batteryStatus.maxCellPositionText, tint: .pink)
                        BatteryMetricCard(title: "Cell Min", value: model.batteryStatus.minCellVoltageText, detail: model.batteryStatus.minCellPositionText, tint: .mint)
                        BatteryMetricCard(title: "Cell Delta", value: model.batteryStatus.cellDeltaText, detail: "单体压差", tint: .purple)
                    }
                }

                PanelCard(title: "SOC 与容量", systemImage: "battery.75") {
                    LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())], spacing: 14) {
                        BatteryMetricCard(title: "SOH", value: model.batteryStatus.sohText, detail: "健康度", tint: .cyan)
                        BatteryMetricCard(title: "Cycle Count", value: model.batteryStatus.cycleCountText, detail: "循环次数", tint: .indigo)
                        BatteryMetricCard(title: "Capacity Now", value: model.batteryStatus.capacityNowText, detail: "当前剩余", tint: .teal)
                        BatteryMetricCard(title: "Capacity Full", value: model.batteryStatus.capacityFullText, detail: "当前满充", tint: .green)
                        BatteryMetricCard(title: "Capacity Factory", value: model.batteryStatus.capacityFactoryText, detail: "出厂额定", tint: .brown)
                    }
                }

                PanelCard(title: "单串电压", systemImage: "square.split.2x2") {
                    if model.batteryStatus.cellVoltages.isEmpty {
                        Text("当前还没有读取到单串电压。")
                            .foregroundStyle(.secondary)
                    } else {
                        let cellMin = model.batteryStatus.cellVoltages.map(\.millivolts).min() ?? 0
                        let cellMax = model.batteryStatus.cellVoltages.map(\.millivolts).max() ?? 0

                        LazyVGrid(columns: Array(repeating: GridItem(.flexible()), count: 5), spacing: 12) {
                            ForEach(model.batteryStatus.cellVoltages) { cell in
                                CellVoltageCard(sample: cell, minMillivolts: cellMin, maxMillivolts: cellMax)
                            }
                        }
                    }
                }

                PanelCard(title: "系统状态字", systemImage: "switch.2") {
                    VStack(alignment: .leading, spacing: 14) {
                        HStack {
                            InfoField(label: "SystemStatus", value: model.batteryStatus.systemStatusHexText)
                            InfoField(label: "活动标志数", value: "\(model.batteryStatus.activeStatusFlags.count)")
                        }

                        LazyVGrid(columns: Array(repeating: GridItem(.flexible()), count: 4), spacing: 10) {
                            ForEach(model.batteryStatus.statusFlags) { flag in
                                StatusFlagCard(flag: flag)
                            }
                        }
                    }
                }

                PanelCard(title: "兼容原始测量", systemImage: "waveform.badge.magnifyingglass") {
                    LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
                        InfoField(label: "Pack Voltage Mirror", value: model.batteryStatus.packVoltageDetailText)
                        InfoField(label: "Battery Temp ADC", value: model.batteryStatus.legacyBatteryTempADCText)
                        InfoField(label: "MOS Temp ADC", value: model.batteryStatus.legacyMosTempADCText)
                    }
                }

                PanelCard(title: "连接与版本", systemImage: "person.crop.rectangle.badge.checkmark") {
                    LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
                        InfoField(label: "连接设备", value: model.connectedDeviceName)
                        InfoField(label: "连接状态", value: model.connectionStatus.rawValue)
                        InfoField(label: "显示名称", value: model.identity.displayName)
                        InfoField(label: "软件版本", value: model.identity.softwareVersion)
                        InfoField(label: "序列号", value: model.identity.serialNumber)
                        InfoField(label: "MAC", value: model.identity.macAddress)
                    }
                }

                PanelCard(title: "寄存器快照", systemImage: "rectangle.grid.2x2") {
                    let blocks = [model.latestCellArrayBlock, model.latestStatusBlock, model.latestRealtimeBlock].compactMap { $0 }
                    if blocks.isEmpty {
                        Text("尚未读取电池状态。连接设备后会自动刷新一次，也可以手动点“刷新状态”。")
                            .foregroundStyle(.secondary)
                    } else {
                        VStack(alignment: .leading, spacing: 14) {
                            ForEach(blocks) { block in
                                RegisterBlockView(block: block)
                            }
                        }
                    }
                }
            }
            .padding(20)
        }
        .task(id: autoRefreshTaskID) {
            await runAutoRefreshLoop()
        }
    }

    private func currentDirectionTint(_ snapshot: BatteryStatusSnapshot) -> Color {
        if snapshot.signedCurrentRaw > 0 {
            return AppPalette.success
        }
        if snapshot.signedCurrentRaw < 0 {
            return AppPalette.warning
        }
        return .gray
    }

    private func sourceTint(_ snapshot: BatteryStatusSnapshot) -> Color {
        switch snapshot.source {
        case .unavailable:
            .gray
        case .legacyRegisters:
            .blue
        case .realtimeWindow:
            AppPalette.success
        }
    }

    private var autoRefreshTaskID: String {
        "\(autoRefreshEnabled)-\(refreshInterval.rawValue)-\(model.connectionStatus.rawValue)"
    }

    private func runAutoRefreshLoop() async {
        guard autoRefreshEnabled else { return }

        if model.canSendCommands {
            model.refreshBatteryStatus()
        }

        while !Task.isCancelled {
            try? await Task.sleep(for: refreshInterval.duration)

            if Task.isCancelled {
                break
            }

            if model.canSendCommands {
                model.refreshBatteryStatus()
            }
        }
    }
}

private struct DebugWorkbenchPane: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        @Bindable var model = model

        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                header

                PanelCard(title: "快捷调试动作", systemImage: "bolt.circle") {
                    LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
                        ActionTile(title: "刷新身份", subtitle: "MAC / SN / HW / SW", systemImage: "person.text.rectangle") {
                            model.refreshIdentity()
                        }
                        .disabled(!model.canSendCommands)

                        ActionTile(title: "系统状态", subtitle: "读取 0xD115~0xD116", systemImage: "waveform.path.ecg") {
                            model.readSystemStatus()
                        }
                        .disabled(!model.canSendCommands)

                        ActionTile(title: "保护参数", subtitle: "读取 0x2100 起始块", systemImage: "shield.lefthalf.filled") {
                            model.readProtectPreview()
                        }
                        .disabled(!model.canSendCommands)

                        ActionTile(title: "事件日志", subtitle: "读取 0xC008 起始窗口", systemImage: "clock.arrow.circlepath") {
                            model.readEventLogPreview()
                        }
                        .disabled(!model.canSendCommands)

                        ActionTile(title: "手动读寄存器", subtitle: "根据输入地址与数量", systemImage: "doc.text.magnifyingglass") {
                            model.readManualBlock()
                        }
                        .disabled(!model.canSendCommands)

                        ActionTile(title: "Echo 测试", subtitle: "验证 SPP 收发链路", systemImage: "arrow.left.arrow.right.circle") {
                            model.sendEchoTest()
                        }
                        .disabled(!model.canSendCommands)
                    }
                }

                PanelCard(title: "设备身份", systemImage: "person.crop.rectangle") {
                    LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
                        InfoField(label: "连接设备", value: model.connectedDeviceName)
                        InfoField(label: "显示名称", value: model.identity.displayName)
                        InfoField(label: "MAC", value: model.identity.macAddress)
                        InfoField(label: "序列号", value: model.identity.serialNumber)
                        InfoField(label: "硬件版本", value: model.identity.hardwareVersion)
                        InfoField(label: "软件版本", value: model.identity.softwareVersion)
                    }
                }

                HStack(alignment: .top, spacing: 18) {
                    PanelCard(title: "寄存器读写工作台", systemImage: "slider.horizontal.3") {
                        VStack(alignment: .leading, spacing: 14) {
                            Text("读取保持寄存器")
                                .font(.headline)

                            HStack {
                                TextField("起始地址，例如 0x0000", text: $model.manualReadAddress)
                                    .textFieldStyle(.roundedBorder)
                                TextField("数量，例如 3", text: $model.manualReadQuantity)
                                    .textFieldStyle(.roundedBorder)
                                    .frame(width: 140)
                                Button("执行读取") {
                                    model.readManualBlock()
                                }
                                .buttonStyle(.borderedProminent)
                                .disabled(!model.canSendCommands)
                            }

                            Divider()

                            Text("写寄存器")
                                .font(.headline)

                            TextField("写入地址，例如 0x1005", text: $model.manualWriteAddress)
                                .textFieldStyle(.roundedBorder)

                            TextField("写入值，支持逗号/空格分隔，例如 0x0001, 0x0002", text: $model.manualWriteWords)
                                .textFieldStyle(.roundedBorder)

                            HStack {
                                Button("执行写入") {
                                    model.writeManualWords()
                                }
                                .buttonStyle(.borderedProminent)
                                .disabled(!model.canSendCommands)

                                Text("当前固件 BLE 单包安全长度为 20 byte，`0x10` 写多寄存器建议不超过 5 words。")
                                    .font(.footnote)
                                    .foregroundStyle(.secondary)
                            }

                            Divider()

                            Text("快捷写入")
                                .font(.headline)

                            HStack {
                                TextField("SOC，例如 60", text: $model.quickSOCValue)
                                    .textFieldStyle(.roundedBorder)
                                    .frame(width: 180)

                                Button("写 SOC -> 0x1005") {
                                    model.writeSOCValue()
                                }
                                .buttonStyle(.borderedProminent)
                                .disabled(!model.canSendCommands)

                                Button("写 0x1103 = 0x0003") {
                                    model.writeDebug1103Shortcut()
                                }
                                .buttonStyle(.bordered)
                                .disabled(!model.canSendCommands)
                            }

                            Text("`0x1005` 当前用于内部 `set_soc_param()`；`0x1103 = 0x0003` 当前用于关闭 `enable_current_test`。")
                                .font(.footnote)
                                .foregroundStyle(.secondary)
                        }
                    }

                    PanelCard(title: "原始帧与蓝牙名", systemImage: "terminal") {
                        VStack(alignment: .leading, spacing: 14) {
                            Text("原始 Modbus RTU 帧")
                                .font(.headline)

                            TextEditor(text: $model.rawHexCommand)
                                .font(.system(.body, design: .monospaced))
                                .frame(minHeight: 96)
                                .padding(8)
                                .background(AppPalette.codeFill, in: RoundedRectangle(cornerRadius: 12, style: .continuous))

                            HStack {
                                Button("发送原始帧") {
                                    model.sendRawCommand()
                                }
                                .buttonStyle(.borderedProminent)
                                .disabled(!model.canSendCommands)

                                Text("示例：`01 03 00 00 00 03 05 CB`")
                                    .font(.footnote)
                                    .foregroundStyle(.secondary)
                            }

                            Divider()

                            Text("蓝牙名后缀")
                                .font(.headline)

                            TextField("例如 FD1901A", text: $model.btNameSuffix)
                                .textFieldStyle(.roundedBorder)

                            HStack {
                                Button("写入蓝牙名") {
                                    model.writeBluetoothNameSuffix()
                                }
                                .buttonStyle(.borderedProminent)
                                .disabled(!model.canSendCommands)

                                Text("当前固件通过 BLE 写蓝牙名时，suffix 建议不超过 10 个 ASCII 字节。")
                                    .font(.footnote)
                                    .foregroundStyle(.secondary)
                            }
                        }
                    }
                }

                PanelCard(title: "最近响应", systemImage: "shippingbox") {
                    Text(model.responsePreview.isEmpty ? "暂无响应" : model.responsePreview)
                        .font(.system(.body, design: .monospaced))
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(10)
                        .background(AppPalette.codeFill, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
                        .textSelection(.enabled)
                }

                PanelCard(title: "最近寄存器块", systemImage: "rectangle.grid.2x2") {
                    let blocks = [model.latestStatusBlock, model.latestProtectBlock, model.latestEventLogBlock, model.latestManualBlock].compactMap { $0 }
                    if blocks.isEmpty {
                        Text("尚未读取寄存器块")
                            .foregroundStyle(.secondary)
                    } else {
                        VStack(alignment: .leading, spacing: 14) {
                            ForEach(blocks) { block in
                                RegisterBlockView(block: block)
                            }
                        }
                    }
                }

                PanelCard(title: "报文日志", systemImage: "list.bullet.rectangle.portrait") {
                    HStack {
                        Text("最近 \(model.logs.count) 条")
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                        Spacer()
                        Button("清空日志") {
                            model.clearLogs()
                        }
                        .buttonStyle(.bordered)
                    }

                    Divider()

                    if model.logs.isEmpty {
                        Text("暂无报文日志")
                            .foregroundStyle(.secondary)
                    } else {
                        ScrollView {
                            LazyVStack(spacing: 10) {
                                ForEach(model.logs) { entry in
                                    LogRow(entry: entry)
                                }
                            }
                        }
                        .frame(minHeight: 260)
                    }
                }
            }
            .padding(20)
        }
    }

    private var header: some View {
        PanelCard(title: "会话概览", systemImage: "desktopcomputer.and.arrow.down") {
            HStack(alignment: .top) {
                VStack(alignment: .leading, spacing: 8) {
                    Text(model.connectedDeviceName)
                        .font(.system(size: 30, weight: .bold, design: .rounded))

                    Text("面向 `vendor/ble_sample` 的 BLE 调试上位机。当前业务链路为 `Modbus RTU over Telink SPP`。")
                        .foregroundStyle(.secondary)
                }

                Spacer()

                VStack(alignment: .trailing, spacing: 8) {
                    StatusBadge(title: "Bluetooth", value: model.bluetoothStateLabel, tint: model.bluetoothState == .poweredOn ? .green : .orange)
                    StatusBadge(title: "连接", value: model.connectionStatus.rawValue, tint: model.connectionStatus == .ready ? .green : .blue)

                    if let busyCommandName = model.busyCommandName {
                        Text("执行中：\(busyCommandName)")
                            .font(.footnote)
                            .foregroundStyle(.secondary)
                    }
                }
            }
        }
    }
}

private struct BatteryMetricCard: View {
    let title: String
    let value: String
    let detail: String
    let tint: Color

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title)
                .font(.caption)
                .foregroundStyle(.secondary)

            Text(value)
                .font(.system(size: 28, weight: .bold, design: .rounded))
                .foregroundStyle(tint)

            Text(detail)
                .font(.footnote)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, minHeight: 120, alignment: .leading)
        .padding(16)
        .background(AppPalette.cardFill, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 18, style: .continuous)
                .strokeBorder(tint.opacity(0.15))
        )
    }
}

private struct CellVoltageCard: View {
    let sample: CellVoltageSample
    let minMillivolts: UInt16
    let maxMillivolts: UInt16

    private var progress: Double {
        let span = max(Int(maxMillivolts) - Int(minMillivolts), 1)
        return Double(Int(sample.millivolts) - Int(minMillivolts)) / Double(span)
    }

    private var tint: Color {
        if sample.millivolts == maxMillivolts {
            return .pink
        }
        if sample.millivolts == minMillivolts {
            return .mint
        }
        return AppPalette.accent
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(sample.title)
                .font(.caption)
                .foregroundStyle(.secondary)

            Text(sample.voltageText)
                .font(.system(size: 20, weight: .bold, design: .rounded))
                .foregroundStyle(tint)

            ProgressView(value: progress)
                .tint(tint)

            Text(sample.detailText)
                .font(.caption2)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .background(AppPalette.cardFill, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 16, style: .continuous)
                .strokeBorder(tint.opacity(0.14))
        )
    }
}

private struct StatusFlagCard: View {
    let flag: StatusFlagSample

    var body: some View {
        HStack {
            Text(flag.title)
                .font(.caption)
                .foregroundStyle(.secondary)

            Spacer()

            Text(flag.isActive ? "ON" : "OFF")
                .font(.caption.weight(.semibold))
                .padding(.horizontal, 8)
                .padding(.vertical, 4)
                .background((flag.isActive ? AppPalette.success : Color.gray).opacity(0.14), in: Capsule())
                .foregroundStyle(flag.isActive ? AppPalette.success : Color.gray)
        }
        .padding(10)
        .background(AppPalette.groupFill, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
    }
}

private struct DeviceRow: View {
    let device: DiscoveredDevice

    var body: some View {
        HStack(spacing: 12) {
            Circle()
                .fill(device.isConnected ? AppPalette.success : (device.isLikelyBMS ? AppPalette.accent : Color.secondary))
                .frame(width: 10, height: 10)

            VStack(alignment: .leading, spacing: 2) {
                Text(device.displayName)
                    .font(.headline)

                Text("\(device.rssiSummary) · \(device.advertisedServicesSummary)")
                    .font(.caption)
                    .foregroundStyle(.secondary)

                if !device.alternateName.isEmpty {
                    Text("别名: \(device.alternateName)")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                }

                if let isConnectable = device.isConnectable {
                    Text(isConnectable ? "Connectable" : "Not connectable")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                }
            }

            Spacer()

            if device.isConnected {
                Image(systemName: "link.circle.fill")
                    .foregroundStyle(AppPalette.success)
            }
        }
        .padding(.vertical, 4)
    }
}

private struct RegisterBlockView: View {
    let block: RegisterBlock

    private let columns = [
        GridItem(.flexible(minimum: 120)),
        GridItem(.flexible(minimum: 120)),
        GridItem(.flexible(minimum: 120)),
        GridItem(.flexible(minimum: 120)),
    ]

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text(block.title)
                    .font(.headline)
                Spacer()
                Text("起始地址 \(String(format: "0x%04X", block.startAddress))")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }

            LazyVGrid(columns: columns, alignment: .leading, spacing: 8) {
                ForEach(Array(block.words.enumerated()), id: \.offset) { index, word in
                    VStack(alignment: .leading, spacing: 2) {
                        Text(String(format: "0x%04X", block.startAddress + UInt16(index)))
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        Text(String(format: "0x%04X", word))
                            .font(.system(.body, design: .monospaced))
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(8)
                    .background(AppPalette.groupFill, in: RoundedRectangle(cornerRadius: 10, style: .continuous))
                }
            }

            Text(block.responseHex)
                .font(.system(.footnote, design: .monospaced))
                .foregroundStyle(.secondary)
                .textSelection(.enabled)
        }
        .padding(14)
        .background(AppPalette.groupFill, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
    }
}

private struct LogRow: View {
    let entry: ExchangeLogEntry

    private var tint: Color {
        switch entry.direction {
        case .tx:
            .blue
        case .rx:
            AppPalette.success
        case .info:
            AppPalette.warning
        case .error:
            .red
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text(entry.direction.label)
                    .font(.caption.weight(.bold))
                    .padding(.horizontal, 8)
                    .padding(.vertical, 4)
                    .background(tint.opacity(0.18), in: Capsule())
                    .foregroundStyle(tint)

                Text(entry.title)
                    .font(.headline)

                Spacer()

                Text(entry.timestamp.formatted(date: .omitted, time: .standard))
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            if !entry.payloadHex.isEmpty {
                Text(entry.payloadHex)
                    .font(.system(.body, design: .monospaced))
                    .textSelection(.enabled)
            }

            if !entry.note.isEmpty {
                Text(entry.note)
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(AppPalette.groupFill, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
    }
}

private struct ActionTile: View {
    let title: String
    let subtitle: String
    let systemImage: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            VStack(alignment: .leading, spacing: 10) {
                Image(systemName: systemImage)
                    .font(.system(size: 20, weight: .semibold))
                    .foregroundStyle(AppPalette.accent)

                Text(title)
                    .font(.headline)
                    .frame(maxWidth: .infinity, alignment: .leading)

                Text(subtitle)
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            .padding(14)
            .frame(maxWidth: .infinity, minHeight: 110, alignment: .leading)
            .background(AppPalette.cardFill, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: 16, style: .continuous)
                    .strokeBorder(AppPalette.cardStroke.opacity(0.6))
            )
        }
        .buttonStyle(.plain)
    }
}

private struct InfoField: View {
    let label: String
    let value: String

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(label)
                .font(.caption)
                .foregroundStyle(.secondary)
            Text(value)
                .font(.system(.body, design: .monospaced))
                .frame(maxWidth: .infinity, alignment: .leading)
                .textSelection(.enabled)
        }
        .padding(12)
        .background(AppPalette.cardFill, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 12, style: .continuous)
                .strokeBorder(AppPalette.cardStroke.opacity(0.6))
        )
    }
}

private struct StatusBadge: View {
    let title: String
    let value: String
    let tint: Color

    var body: some View {
        HStack(spacing: 8) {
            Text(title)
                .font(.caption)
                .foregroundStyle(.secondary)
            Text(value)
                .font(.caption.weight(.semibold))
                .padding(.horizontal, 10)
                .padding(.vertical, 5)
                .background(tint.opacity(0.16), in: Capsule())
                .foregroundStyle(tint)
        }
    }
}

private struct PanelCard<Content: View>: View {
    let title: String
    let systemImage: String
    let content: Content

    init(title: String, systemImage: String, @ViewBuilder content: () -> Content) {
        self.title = title
        self.systemImage = systemImage
        self.content = content()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Label(title, systemImage: systemImage)
                    .font(.headline)
                Spacer()
            }

            content
        }
        .padding(18)
        .background(AppPalette.cardFill, in: RoundedRectangle(cornerRadius: 20, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: 20, style: .continuous)
                .strokeBorder(AppPalette.cardStroke)
        )
        .shadow(color: .black.opacity(0.05), radius: 10, x: 0, y: 6)
    }
}
