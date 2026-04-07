import CoreBluetooth
import Foundation
import Observation

@MainActor
@Observable
final class AppModel {
    var bluetoothState: CBManagerState = .unknown
    var connectionStatus: ConnectionStatus = .idle
    var statusMessage = "等待蓝牙初始化"
    var scanMode: ScanMode = .targetFirmware
    var searchText = ""
    var showOnlyLikelyBMS = false
    var isScanning = false
    var selectedDeviceID: UUID?
    var devices: [DiscoveredDevice] = []
    var identity = DeviceIdentitySnapshot()
    var batteryStatus = BatteryStatusSnapshot.empty
    var latestCellArrayBlock: RegisterBlock?
    var latestManualBlock: RegisterBlock?
    var latestProtectBlock: RegisterBlock?
    var latestEventLogBlock: RegisterBlock?
    var latestRealtimeBlock: RegisterBlock?
    var latestStatusBlock: RegisterBlock?
    var logs: [ExchangeLogEntry] = []
    var manualReadAddress = "0x0000"
    var manualReadQuantity = "3"
    var manualWriteAddress = "0x1005"
    var manualWriteWords = "0x0032"
    var quickSOCValue = "60"
    var rawHexCommand = ""
    var btNameSuffix = ""
    var responsePreview = ""
    var busyCommandName: String?

    private let transport = BLETransport()
    private var peripherals = [UUID: CBPeripheral]()
    private var connectedPeripheralID: UUID?
    private var accumulator = ResponseAccumulator()
    private var pendingExchange: PendingExchange?

    init() {
        wireTransport()
        transport.activate()
    }

    var filteredDevices: [DiscoveredDevice] {
        let normalizedSearch = searchText.trimmingCharacters(in: .whitespacesAndNewlines)

        return devices
            .filter { device in
                (!showOnlyLikelyBMS || device.isLikelyBMS) &&
                (
                    normalizedSearch.isEmpty ||
                    device.displayName.localizedCaseInsensitiveContains(normalizedSearch) ||
                    device.alternateName.localizedCaseInsensitiveContains(normalizedSearch) ||
                    device.advertisedServicesSummary.localizedCaseInsensitiveContains(normalizedSearch)
                )
            }
            .sorted { lhs, rhs in
                if lhs.isConnected != rhs.isConnected {
                    return lhs.isConnected
                }
                if lhs.isLikelyBMS != rhs.isLikelyBMS {
                    return lhs.isLikelyBMS
                }
                if lhs.rssi != rhs.rssi {
                    return lhs.rssi > rhs.rssi
                }
                return lhs.displayName < rhs.displayName
            }
    }

    var bluetoothStateLabel: String {
        switch bluetoothState {
        case .unknown:
            "未知"
        case .resetting:
            "重置中"
        case .unsupported:
            "不支持"
        case .unauthorized:
            "无权限"
        case .poweredOff:
            "已关闭"
        case .poweredOn:
            "已开启"
        @unknown default:
            "未来状态"
        }
    }

    var connectedDeviceName: String {
        guard let connectedPeripheralID,
              let device = devices.first(where: { $0.id == connectedPeripheralID }) else {
            return "未连接"
        }
        return device.displayName
    }

    var canSendCommands: Bool {
        connectionStatus == .ready && pendingExchange == nil
    }

    func toggleScan() {
        if isScanning {
            stopScan()
        } else {
            startScan()
        }
    }

    func startScan() {
        guard bluetoothState == .poweredOn else {
            appendLog(.error, title: "扫描失败", payloadHex: "", note: "蓝牙当前状态为 \(bluetoothStateLabel)")
            statusMessage = "蓝牙未就绪，无法扫描"
            return
        }

        isScanning = true
        connectionStatus = .scanning
        statusMessage = "正在扫描附近设备: \(scanMode.note)"
        transport.startScan(services: scanMode.serviceFilter)
    }

    func stopScan() {
        isScanning = false
        if connectionStatus == .scanning {
            connectionStatus = .idle
        }
        statusMessage = "已停止扫描"
        transport.stopScan()
    }

    func connectSelected() {
        guard let selectedDeviceID else { return }
        connect(to: selectedDeviceID)
    }

    func connect(to deviceID: UUID) {
        guard let peripheral = peripherals[deviceID] else { return }
        selectedDeviceID = deviceID
        statusMessage = "准备连接 \(devices.first(where: { $0.id == deviceID })?.displayName ?? deviceID.uuidString)"
        transport.connect(peripheral)
    }

    func disconnect() {
        transport.disconnectCurrent()
    }

    func refreshIdentity() {
        runTask(named: "刷新设备身份") { [weak self] in
            guard let self else { return }
            let macBlock = try await self.readRegisters(title: "读取 MAC 地址", start: RegisterCatalog.macAddressStart, quantity: RegisterCatalog.macAddressCount)
            let serialBlock = try await self.readRegisters(title: "读取序列号", start: RegisterCatalog.productSerialStart, quantity: RegisterCatalog.productTextCount)
            let hardwareBlock = try await self.readRegisters(title: "读取硬件版本", start: RegisterCatalog.productHardwareStart, quantity: RegisterCatalog.productTextCount)
            let softwareBlock = try await self.readRegisters(title: "读取软件版本", start: RegisterCatalog.productSoftwareStart, quantity: RegisterCatalog.productTextCount)

            self.identity.displayName = self.connectedDeviceName
            self.identity.macAddress = ModbusCodec.macString(from: macBlock.words)
            self.identity.serialNumber = ModbusCodec.asciiString(from: serialBlock.words)
            self.identity.hardwareVersion = ModbusCodec.asciiString(from: hardwareBlock.words)
            self.identity.softwareVersion = ModbusCodec.asciiString(from: softwareBlock.words)
            self.statusMessage = "设备身份信息已刷新"
        }
    }

    func refreshBatteryStatus() {
        runTask(named: "刷新电池状态") { [weak self] in
            guard let self else { return }
            let legacyCellBlock = try await self.readRegisters(
                title: "单串电压与兼容数据",
                start: RegisterCatalog.legacyCellArrayStart,
                quantity: RegisterCatalog.legacyCellArrayCount
            )
            let statusBlock = try await self.readRegisters(
                title: "系统状态",
                start: RegisterCatalog.systemStatusStart,
                quantity: RegisterCatalog.systemStatusCount
            )
            let realtimeBlock = try await self.readRegisters(
                title: "电池状态页",
                start: RegisterCatalog.realtimeStatusStart,
                quantity: RegisterCatalog.realtimeStatusCount
            )

            self.latestCellArrayBlock = legacyCellBlock
            self.latestStatusBlock = statusBlock
            self.latestRealtimeBlock = realtimeBlock

            let snapshot = BatteryStatusSnapshot.decode(
                realtimeWords: realtimeBlock.words,
                legacyCellWords: legacyCellBlock.words,
                systemStatusWords: statusBlock.words,
                updatedAt: .now
            )
            self.batteryStatus = snapshot
            self.statusMessage = snapshot.supportsRealtimeWindow
                ? "电池状态已刷新（实时窗口模式）"
                : "电池状态已刷新（旧寄存器兼容模式）"
        }
    }

    func readProtectPreview() {
        runTask(named: "读取保护参数预览") { [weak self] in
            guard let self else { return }
            self.latestProtectBlock = try await self.readRegisters(
                title: "保护参数预览",
                start: RegisterCatalog.protectStart,
                quantity: RegisterCatalog.protectPreviewCount
            )
            self.statusMessage = "已读取保护参数预览"
        }
    }

    func readSystemStatus() {
        runTask(named: "读取系统状态") { [weak self] in
            guard let self else { return }
            self.latestStatusBlock = try await self.readRegisters(
                title: "系统状态",
                start: RegisterCatalog.systemStatusStart,
                quantity: RegisterCatalog.systemStatusCount
            )
            self.statusMessage = "已读取系统状态"
        }
    }

    func readEventLogPreview() {
        runTask(named: "读取事件日志预览") { [weak self] in
            guard let self else { return }
            self.latestEventLogBlock = try await self.readRegisters(
                title: "事件日志预览",
                start: RegisterCatalog.eventLogStart,
                quantity: RegisterCatalog.eventLogPreviewCount
            )
            self.statusMessage = "已读取事件日志预览"
        }
    }

    func sendEchoTest() {
        runTask(named: "Echo 测试") { [weak self] in
            guard let self else { return }
            let request = ModbusCodec.echo(payload: Data([0x12, 0x34, 0x56, 0x78]))
            let response = try await self.transact(name: "Echo 测试", request: request, expectedLengthHint: request.count)
            let parsed = try ModbusCodec.parse(response)
            guard case .echo = parsed else {
                throw ModbusCodecError.unexpectedResponse("Echo 响应类型错误")
            }
            self.statusMessage = "Echo 成功，链路可收发"
        }
    }

    func readManualBlock() {
        runTask(named: "手动读取寄存器") { [weak self] in
            guard let self else { return }
            let start = try HexCodec.parseAddress(self.manualReadAddress)
            let quantity = try HexCodec.parseAddress(self.manualReadQuantity)
            self.latestManualBlock = try await self.readRegisters(title: "手动读取", start: start, quantity: quantity)
            self.statusMessage = "手动读取完成"
        }
    }

    func writeManualWords() {
        runTask(named: "手动写寄存器") { [weak self] in
            guard let self else { return }
            let register = try HexCodec.parseAddress(self.manualWriteAddress)
            let words = try HexCodec.parseWords(self.manualWriteWords)
            guard !words.isEmpty else {
                throw ModbusCodecError.invalidHex("写入值为空")
            }

            let request: Data
            if words.count == 1 {
                request = ModbusCodec.writeSingle(register: register, value: words[0])
            } else {
                request = ModbusCodec.writeMultiple(register: register, values: words)
            }

            try self.ensureSafeBLELength(request)
            let response = try await self.transact(name: "手动写寄存器", request: request)
            _ = try ModbusCodec.parse(response)
            self.statusMessage = "写入完成"
        }
    }

    func writeSOCValue() {
        runTask(named: "写入 SOC") { [weak self] in
            guard let self else { return }
            let value = try HexCodec.parseAddress(self.quickSOCValue)
            guard value <= 100 else {
                throw ModbusCodecError.invalidHex("SOC 建议范围 0~100")
            }

            let request = ModbusCodec.writeSingle(register: RegisterCatalog.socWriteRegister, value: value)
            try self.ensureSafeBLELength(request)
            let response = try await self.transact(name: "写入 SOC", request: request)
            _ = try ModbusCodec.parse(response)
            self.statusMessage = "SOC 写入完成，已写 `0x1005 = \(value)`"
            self.refreshBatteryStatus()
        }
    }

    func writeDebug1103Shortcut() {
        runTask(named: "写入 0x1103") { [weak self] in
            guard let self else { return }
            let request = ModbusCodec.writeSingle(register: RegisterCatalog.debugRegister1103, value: 0x0003)
            try self.ensureSafeBLELength(request)
            let response = try await self.transact(name: "写入 0x1103", request: request)
            _ = try ModbusCodec.parse(response)
            self.statusMessage = "已写 `0x1103 = 0x0003`"
        }
    }

    func sendRawCommand() {
        runTask(named: "发送原始 Modbus 帧") { [weak self] in
            guard let self else { return }
            let request = try HexCodec.parseRawBytes(self.rawHexCommand)
            let expectedLength = request.count >= 2 && request[1] == 0x7F ? request.count : nil
            try self.ensureSafeBLELength(request)
            let response = try await self.transact(name: "原始帧", request: request, expectedLengthHint: expectedLength)
            _ = try ModbusCodec.parse(response)
            self.statusMessage = "原始帧发送完成"
        }
    }

    func writeBluetoothNameSuffix() {
        runTask(named: "写入蓝牙名后缀") { [weak self] in
            guard let self else { return }
            let suffix = self.btNameSuffix.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !suffix.isEmpty else {
                throw ModbusCodecError.invalidHex("蓝牙名后缀不能为空")
            }
            guard suffix.utf8.count <= RegisterCatalog.btNameMaxWriteBytes else {
                throw ModbusCodecError.requestTooLong(9 + ((suffix.utf8.count + 1) / 2) * 2)
            }

            let words = ModbusCodec.encodeASCIIWords(suffix)
            let request = ModbusCodec.writeMultiple(register: RegisterCatalog.btNameStart, values: words)
            try self.ensureSafeBLELength(request)
            let response = try await self.transact(name: "写入蓝牙名", request: request)
            _ = try ModbusCodec.parse(response)
            self.statusMessage = "蓝牙名已写入，请重新扫描确认广播名刷新"
        }
    }

    func clearLogs() {
        logs.removeAll(keepingCapacity: true)
    }

    func clearDevices() {
        devices.removeAll(keepingCapacity: true)
        peripherals.removeAll(keepingCapacity: true)
        selectedDeviceID = nil
        statusMessage = "已清空扫描列表"
    }

    private func wireTransport() {
        transport.onBluetoothStateChange = { [weak self] state in
            guard let self else { return }
            bluetoothState = state
            if state == .poweredOn {
                statusMessage = "蓝牙已就绪，可以开始扫描"
            } else {
                statusMessage = "蓝牙状态: \(bluetoothStateLabel)"
            }
        }

        transport.onDiscovery = { [weak self] peripheral, rssi, snapshot in
            guard let self else { return }
            peripherals[peripheral.identifier] = peripheral
            upsertDevice(id: peripheral.identifier, snapshot: snapshot, rssi: rssi.intValue)
        }

        transport.onConnectionChange = { [weak self] peripheral, status, message in
            guard let self else { return }
            connectionStatus = status
            statusMessage = message
            if status == .disconnected || status == .failed {
                connectedPeripheralID = nil
                markConnectedDevice(nil)
                batteryStatus = .empty
                latestCellArrayBlock = nil
                latestRealtimeBlock = nil
                latestStatusBlock = nil
            }
            if status == .connected || status == .connecting {
                connectedPeripheralID = peripheral.identifier
                selectedDeviceID = peripheral.identifier
                markConnectedDevice(peripheral.identifier)
            }
        }

        transport.onReady = { [weak self] peripheral in
            guard let self else { return }
            connectedPeripheralID = peripheral.identifier
            markConnectedDevice(peripheral.identifier)
            connectionStatus = .ready
            statusMessage = "SPP 通道已就绪，可直接收发 Modbus RTU"
            appendLog(.info, title: "BLE 就绪", payloadHex: "", note: "已订阅响应特征 \(BMSUUIDs.responseCharacteristic.uuidString)")
            refreshBatteryStatus()
        }

        transport.onData = { [weak self] data in
            self?.handleIncoming(fragment: data)
        }

        transport.onError = { [weak self] message in
            guard let self else { return }
            appendLog(.error, title: "BLE 错误", payloadHex: "", note: message)
            statusMessage = message
        }
    }

    private func upsertDevice(id: UUID, snapshot: DiscoverySnapshot, rssi: Int) {
        if let index = devices.firstIndex(where: { $0.id == id }) {
            devices[index].name = snapshot.preferredName.isEmpty ? devices[index].name : snapshot.preferredName
            devices[index].alternateName = snapshot.alternateName.isEmpty ? devices[index].alternateName : snapshot.alternateName
            devices[index].rssi = rssi
            devices[index].lastSeen = .now
            if !snapshot.advertisedServices.isEmpty {
                devices[index].advertisedServices = snapshot.advertisedServices
            }
            devices[index].isConnectable = snapshot.isConnectable ?? devices[index].isConnectable
        } else {
            devices.append(
                DiscoveredDevice(
                    id: id,
                    name: snapshot.preferredName,
                    alternateName: snapshot.alternateName,
                    rssi: rssi,
                    lastSeen: .now,
                    isConnected: false,
                    advertisedServices: snapshot.advertisedServices,
                    isConnectable: snapshot.isConnectable
                )
            )
        }

        if selectedDeviceID == nil, let firstLikely = filteredDevices.first {
            selectedDeviceID = firstLikely.id
        }
    }

    private func markConnectedDevice(_ id: UUID?) {
        for index in devices.indices {
            devices[index].isConnected = devices[index].id == id
        }
    }

    private func handleIncoming(fragment: Data) {
        let event = accumulator.append(fragment)
        switch event {
        case .waiting(let expectedLength, let fragments):
            responsePreview = accumulator.buffer.spacedHexString
            statusMessage = expectedLength.map { "接收响应中：已收 \(accumulator.buffer.count)/\($0) byte，分片 \(fragments)" } ?? "接收响应中：已收 \(accumulator.buffer.count) byte"

        case .completed(let frame, let fragments):
            responsePreview = frame.spacedHexString
            appendLog(.rx, title: pendingExchange?.name ?? "收到响应", payloadHex: frame.spacedHexString, note: "assembled from \(fragments) notify")
            resolvePending(with: .success(frame))

        case .invalidCRC(let frame, let fragments):
            responsePreview = frame.spacedHexString
            appendLog(.error, title: pendingExchange?.name ?? "响应 CRC 错误", payloadHex: frame.spacedHexString, note: "assembled from \(fragments) notify")
            resolvePending(with: .failure(ModbusCodecError.crcMismatch))
        }
    }

    private func runTask(named name: String, operation: @escaping () async throws -> Void) {
        guard pendingExchange == nil else {
            statusMessage = "仍有请求进行中：\(pendingExchange?.name ?? "unknown")"
            return
        }

        busyCommandName = name
        Task {
            do {
                try await operation()
            } catch {
                appendLog(.error, title: name, payloadHex: "", note: error.localizedDescription)
                statusMessage = "\(name)失败: \(error.localizedDescription)"
            }
            busyCommandName = nil
        }
    }

    private func readRegisters(title: String, start: UInt16, quantity: UInt16) async throws -> RegisterBlock {
        let request = ModbusCodec.readHolding(start: start, quantity: quantity)
        try ensureSafeBLELength(request)
        let response = try await transact(name: title, request: request)
        let parsed = try ModbusCodec.parse(response)
        guard case .readHolding(let words) = parsed else {
            throw ModbusCodecError.unexpectedResponse("收到的不是 0x03 读寄存器响应")
        }
        return RegisterBlock(
            title: title,
            startAddress: start,
            words: words,
            updatedAt: .now,
            responseHex: response.spacedHexString
        )
    }

    private func transact(name: String, request: Data, expectedLengthHint: Int? = nil) async throws -> Data {
        guard connectionStatus == .ready else {
            throw ModbusCodecError.transportNotReady
        }
        guard pendingExchange == nil else {
            throw ModbusCodecError.transportBusy(pendingExchange?.name ?? "unknown")
        }

        accumulator.reset(expectedLengthHint: expectedLengthHint)
        appendLog(.tx, title: name, payloadHex: request.spacedHexString, note: "\(request.count) byte")

        return try await withCheckedThrowingContinuation { continuation in
            let requestID = UUID()
            let pending = PendingExchange(id: requestID, name: name, continuation: continuation)
            pending.timeoutTask = Task { [weak self] in
                try? await Task.sleep(for: .seconds(3))
                await MainActor.run {
                    self?.handleTimeout(id: requestID)
                }
            }

            pendingExchange = pending

            do {
                try transport.send(request)
            } catch {
                pending.timeoutTask?.cancel()
                pendingExchange = nil
                continuation.resume(throwing: error)
            }
        }
    }

    private func handleTimeout(id: UUID) {
        guard let pendingExchange, pendingExchange.id == id else { return }
        appendLog(.error, title: pendingExchange.name, payloadHex: "", note: "等待响应超时")
        resolvePending(with: .failure(ModbusCodecError.invalidFrame("等待响应超时")))
    }

    private func resolvePending(with result: Result<Data, Error>) {
        guard let pendingExchange else { return }
        pendingExchange.timeoutTask?.cancel()
        self.pendingExchange = nil
        pendingExchange.continuation.resume(with: result)
    }

    private func ensureSafeBLELength(_ request: Data) throws {
        if request.count > 20 {
            throw ModbusCodecError.requestTooLong(request.count)
        }
    }

    private func appendLog(_ direction: ExchangeDirection, title: String, payloadHex: String, note: String) {
        logs.insert(
            ExchangeLogEntry(
                timestamp: .now,
                direction: direction,
                title: title,
                payloadHex: payloadHex,
                note: note
            ),
            at: 0
        )
    }
}

private final class PendingExchange {
    let id: UUID
    let name: String
    let continuation: CheckedContinuation<Data, Error>
    var timeoutTask: Task<Void, Never>?

    init(id: UUID, name: String, continuation: CheckedContinuation<Data, Error>) {
        self.id = id
        self.name = name
        self.continuation = continuation
    }
}
