#if os(iOS)
import CoreBluetooth
import Foundation
import Observation

@Observable
final class OTAViewModel: NSObject {
    struct Device: Identifiable, Equatable {
        let id: UUID
        var name: String
        var rssi: Int
    }

    var devices: [Device] = []
    var selectedDeviceID: UUID?
    var bluetoothState = "初始化"
    var status = "等待操作"
    var firmwareName = "未选择 firmware.bin"
    var firmwareDetail = ""
    var progress = 0
    var isScanning = false
    var isReady = false
    var isRunning = false
    var needsConfirmation = false

    private lazy var central = CBCentralManager(delegate: self, queue: nil)
    private var peripherals: [UUID: CBPeripheral] = [:]
    private var connected: CBPeripheral?
    private var otaCharacteristic: CBCharacteristic?
    private var image: TelinkOTA.FirmwareImage?
    private var phase: Phase = .idle
    private var dataIndex = 0
    private var awaitingReboot = false

    private enum Phase { case idle, start, data, end, waitingResult, finished }

    func activate() {
        _ = central
    }

    func toggleScan() {
        isScanning ? stopScan() : startScan()
    }

    func startScan() {
        guard central.state == .poweredOn else {
            status = "蓝牙未开启"
            return
        }
        devices.removeAll(keepingCapacity: true)
        peripherals.removeAll(keepingCapacity: true)
        selectedDeviceID = nil
        isScanning = true
        status = "正在扫描 BLE 设备"
        central.scanForPeripherals(withServices: nil, options: [CBCentralManagerScanOptionAllowDuplicatesKey: true])
    }

    func stopScan() {
        central.stopScan()
        isScanning = false
        if status.contains("扫描") { status = "已停止扫描" }
    }

    func connectSelected() {
        guard let selectedDeviceID, let peripheral = peripherals[selectedDeviceID] else {
            status = "请先选择设备"
            return
        }
        stopScan()
        disconnect()
        status = "正在连接 \(devices.first(where: { $0.id == selectedDeviceID })?.name ?? selectedDeviceID.uuidString)"
        central.connect(peripheral)
    }

    func disconnect() {
        if let connected { central.cancelPeripheralConnection(connected) }
        connected = nil
        otaCharacteristic = nil
        isReady = false
    }

    func loadFirmware(url: URL) {
        do {
            let scoped = url.startAccessingSecurityScopedResource()
            defer { if scoped { url.stopAccessingSecurityScopedResource() } }
            let data = try Data(contentsOf: url)
            let parsed = try TelinkOTA.FirmwareImage(data: data)
            image = parsed
            firmwareName = url.lastPathComponent
            firmwareDetail = "\(parsed.declaredSize) bytes · \(parsed.packetCount) packets"
            status = "固件校验通过"
        } catch {
            image = nil
            firmwareName = "固件无效"
            firmwareDetail = ""
            status = "固件校验失败：\(error.localizedDescription)"
        }
    }

    func requestStart() {
        guard isReady, image != nil, !isRunning else { return }
        needsConfirmation = true
    }

    func confirmStart() {
        needsConfirmation = false
        guard let image else { return }
        isRunning = true
        progress = 0
        phase = .start
        dataIndex = 0
        awaitingReboot = false
        status = "发送 OTA START"
        write(TelinkOTA.startPacket())
        _ = image
    }

    func cancelConfirmation() {
        needsConfirmation = false
    }

    private func write(_ data: Data) {
        guard let connected, let otaCharacteristic else {
            fail("OTA characteristic 未就绪")
            return
        }
        connected.writeValue(data, for: otaCharacteristic, type: .withResponse)
    }

    private func handleWriteCompleted() {
        guard isRunning, let image else { return }
        switch phase {
        case .start:
            phase = .data
            sendCurrentData(image)
        case .data:
            dataIndex += 1
            progress = min(100, dataIndex * 100 / max(1, image.packetCount))
            status = "OTA data \(dataIndex)/\(image.packetCount)"
            if dataIndex < image.packetCount {
                sendCurrentData(image)
            } else {
                phase = .end
                do { write(try image.endPacket()) }
                catch { fail(error.localizedDescription) }
            }
        case .end:
            phase = .waitingResult
            status = "OTA END 已发送，等待 OTA_RESULT"
        default:
            break
        }
    }

    private func sendCurrentData(_ image: TelinkOTA.FirmwareImage) {
        do { write(try image.dataPacket(index: dataIndex)) }
        catch { fail(error.localizedDescription) }
    }

    private func handleNotification(_ data: Data) {
        guard let code = TelinkOTA.parseResult(data) else { return }
        isRunning = false
        phase = .finished
        if code == 0 {
            progress = 100
            awaitingReboot = true
            status = "OTA_SUCCESS，等待 BMS 重启"
        } else {
            status = "OTA 失败：\(TelinkOTA.resultText(code)) (0x\(String(format: "%02X", code)))"
        }
    }

    private func fail(_ message: String) {
        isRunning = false
        phase = .finished
        status = message
    }
}

extension OTAViewModel: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn: bluetoothState = "已开启"
        case .poweredOff: bluetoothState = "已关闭"
        case .unauthorized: bluetoothState = "无权限"
        case .unsupported: bluetoothState = "不支持"
        case .resetting: bluetoothState = "重置中"
        case .unknown: bluetoothState = "未知"
        @unknown default: bluetoothState = "未知"
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let localName = advertisementData[CBAdvertisementDataLocalNameKey] as? String
        let name = localName ?? peripheral.name ?? "BLE \(peripheral.identifier.uuidString.suffix(6))"
        peripherals[peripheral.identifier] = peripheral
        let item = Device(id: peripheral.identifier, name: name, rssi: RSSI.intValue)
        if let index = devices.firstIndex(where: { $0.id == item.id }) { devices[index] = item }
        else { devices.append(item) }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        connected = peripheral
        peripheral.delegate = self
        status = "已连接，发现 OTA Service"
        peripheral.discoverServices([CBUUID(string: TelinkOTA.serviceUUID)])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        fail("连接失败：\(error?.localizedDescription ?? "unknown")")
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        connected = nil
        otaCharacteristic = nil
        isReady = false
        if awaitingReboot {
            status = "BMS 已断开，符合 OTA 后重启预期；请重新扫描并读取软件版本"
            awaitingReboot = false
        } else if isRunning {
            fail("OTA 中连接断开：\(error?.localizedDescription ?? "unknown")")
        } else {
            status = "连接已断开"
        }
    }
}

extension OTAViewModel: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error { fail("发现 OTA Service 失败：\(error.localizedDescription)"); return }
        guard let service = peripheral.services?.first(where: { $0.uuid == CBUUID(string: TelinkOTA.serviceUUID) }) else {
            fail("未发现 OTA Service \(TelinkOTA.serviceUUID)")
            return
        }
        peripheral.discoverCharacteristics([CBUUID(string: TelinkOTA.characteristicUUID)], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error { fail("发现 OTA Characteristic 失败：\(error.localizedDescription)"); return }
        guard let characteristic = service.characteristics?.first(where: { $0.uuid == CBUUID(string: TelinkOTA.characteristicUUID) }) else {
            fail("未发现 OTA Characteristic \(TelinkOTA.characteristicUUID)")
            return
        }
        otaCharacteristic = characteristic
        peripheral.setNotifyValue(true, for: characteristic)
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        if let error { fail("订阅 OTA Notify 失败：\(error.localizedDescription)"); return }
        if characteristic.isNotifying {
            isReady = true
            status = "OTA characteristic ready"
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error { fail("OTA GATT 写失败：\(error.localizedDescription)"); return }
        if characteristic.uuid == CBUUID(string: TelinkOTA.characteristicUUID) { handleWriteCompleted() }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error { fail("OTA Notify 读取失败：\(error.localizedDescription)"); return }
        guard characteristic.uuid == CBUUID(string: TelinkOTA.characteristicUUID), let value = characteristic.value else { return }
        handleNotification(value)
    }
}
#endif
