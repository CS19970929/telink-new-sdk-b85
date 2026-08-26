#if os(iOS)
import SwiftUI
import UniformTypeIdentifiers

struct OTAMobileView: View {
    @State private var model = OTAViewModel()
    @State private var importingFirmware = false

    var body: some View {
        List {
            Section("OTA 状态") {
                LabeledContent("Bluetooth", value: model.bluetoothState)
                LabeledContent("状态", value: model.status)
                ProgressView(value: Double(model.progress), total: 100)
                Text("\(model.progress)%").font(.caption.monospacedDigit())
            }

            Section("设备") {
                HStack {
                    Button(model.isScanning ? "停止扫描" : "开始扫描") { model.toggleScan() }
                        .buttonStyle(.borderedProminent)
                    Button("连接") { model.connectSelected() }
                        .buttonStyle(.bordered)
                        .disabled(model.selectedDeviceID == nil || model.isRunning)
                }
                if model.devices.isEmpty {
                    Text("尚未发现设备").foregroundStyle(.secondary)
                } else {
                    ForEach(model.devices.sorted(by: { $0.rssi > $1.rssi })) { device in
                        Button {
                            model.selectedDeviceID = device.id
                        } label: {
                            HStack {
                                VStack(alignment: .leading) {
                                    Text(device.name)
                                    Text("\(device.rssi) dBm").font(.caption).foregroundStyle(.secondary)
                                }
                                Spacer()
                                if model.selectedDeviceID == device.id {
                                    Image(systemName: "checkmark.circle.fill").foregroundStyle(.teal)
                                }
                            }
                        }
                        .buttonStyle(.plain)
                    }
                }
            }

            Section("Firmware") {
                Button("选择 firmware.bin") { importingFirmware = true }
                    .disabled(model.isRunning)
                LabeledContent("文件", value: model.firmwareName)
                if !model.firmwareDetail.isEmpty {
                    Text(model.firmwareDetail).font(.caption.monospaced()).foregroundStyle(.secondary)
                }
            }

            Section("升级") {
                Button(model.isRunning ? "升级中..." : "开始 OTA") { model.requestStart() }
                    .buttonStyle(.borderedProminent)
                    .disabled(!model.isReady || model.isRunning || model.firmwareDetail.isEmpty)
                Text("Legacy OTA / 16-byte PDU / Write With Response")
                    .font(.footnote).foregroundStyle(.secondary)
            }

            Section("安全提示") {
                Text("升级期间禁止断电。当前 BMS 固件 BLE SMP 未启用，OTA 页面当前定位为工程联调功能。")
                    .font(.footnote).foregroundStyle(.orange)
            }
        }
        .navigationTitle("固件升级")
        .onAppear { model.activate() }
        .fileImporter(
            isPresented: $importingFirmware,
            allowedContentTypes: [.data],
            allowsMultipleSelection: false
        ) { result in
            switch result {
            case .success(let urls):
                if let url = urls.first { model.loadFirmware(url: url) }
            case .failure(let error):
                model.status = "文件选择失败：\(error.localizedDescription)"
            }
        }
        .alert("确认 OTA", isPresented: $model.needsConfirmation) {
            Button("取消", role: .cancel) { model.cancelConfirmation() }
            Button("开始升级", role: .destructive) { model.confirmStart() }
        } message: {
            Text("升级期间禁止断电。请确认当前连接的是目标 BMS。")
        }
    }
}
#endif
