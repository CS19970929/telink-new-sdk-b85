import CoreBluetooth
import Foundation

final class BLETransport: NSObject {
    var onBluetoothStateChange: ((CBManagerState) -> Void)?
    var onDiscovery: ((CBPeripheral, NSNumber, DiscoverySnapshot) -> Void)?
    var onConnectionChange: ((CBPeripheral, ConnectionStatus, String) -> Void)?
    var onReady: ((CBPeripheral) -> Void)?
    var onData: ((Data) -> Void)?
    var onError: ((String) -> Void)?

    private lazy var centralManager = CBCentralManager(delegate: self, queue: nil)
    private var writeCharacteristic: CBCharacteristic?
    private var notifyCharacteristic: CBCharacteristic?
    private var connectedPeripheral: CBPeripheral?

    func activate() {
        _ = centralManager
    }

    func startScan(services: [CBUUID]?) {
        guard centralManager.state == .poweredOn else {
            onError?("蓝牙当前不可用，状态为 \(centralManager.state.readableName)")
            return
        }

        centralManager.scanForPeripherals(
            withServices: services,
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: true]
        )
    }

    func stopScan() {
        centralManager.stopScan()
    }

    func connect(_ peripheral: CBPeripheral) {
        stopScan()
        connectedPeripheral = nil
        writeCharacteristic = nil
        notifyCharacteristic = nil
        centralManager.connect(peripheral, options: nil)
        onConnectionChange?(peripheral, .connecting, "正在建立 BLE 连接")
    }

    func disconnectCurrent() {
        guard let connectedPeripheral else { return }
        centralManager.cancelPeripheralConnection(connectedPeripheral)
    }

    func send(_ data: Data) throws {
        guard let connectedPeripheral, let writeCharacteristic else {
            throw ModbusCodecError.transportNotReady
        }

        let writeType: CBCharacteristicWriteType = writeCharacteristic.properties.contains(.write) ? .withResponse : .withoutResponse
        connectedPeripheral.writeValue(data, for: writeCharacteristic, type: writeType)
    }
}

extension BLETransport: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        onBluetoothStateChange?(central.state)
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let advertisedName = advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? ""
        let peripheralName = peripheral.name ?? ""
        let serviceUUIDs = (advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID]) ?? []
        let advertisedServices = serviceUUIDs.map { uuid in
            let raw = uuid.uuidString.uppercased()
            if raw.hasPrefix("0000"), raw.hasSuffix("-0000-1000-8000-00805F9B34FB") {
                return String(raw.dropFirst(4).prefix(4))
            }
            return raw
        }
        let snapshot = DiscoverySnapshot(
            localName: advertisedName,
            peripheralName: peripheralName,
            advertisedServices: advertisedServices,
            isConnectable: advertisementData[CBAdvertisementDataIsConnectable] as? Bool
        )
        onDiscovery?(peripheral, RSSI, snapshot)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        connectedPeripheral = peripheral
        peripheral.delegate = self
        peripheral.discoverServices([BMSUUIDs.sppService])
        onConnectionChange?(peripheral, .connected, "连接完成，正在发现服务")
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        onConnectionChange?(peripheral, .failed, error?.localizedDescription ?? "连接失败")
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        writeCharacteristic = nil
        notifyCharacteristic = nil
        connectedPeripheral = nil
        onConnectionChange?(peripheral, .disconnected, error?.localizedDescription ?? "连接已断开")
    }
}

extension BLETransport: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error {
            onError?("发现服务失败: \(error.localizedDescription)")
            return
        }

        guard let service = peripheral.services?.first(where: { $0.uuid == BMSUUIDs.sppService }) else {
            onError?("未发现目标 SPP 服务 \(BMSUUIDs.sppService.uuidString)")
            return
        }

        peripheral.discoverCharacteristics([BMSUUIDs.requestCharacteristic, BMSUUIDs.responseCharacteristic], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error {
            onError?("发现特征失败: \(error.localizedDescription)")
            return
        }

        writeCharacteristic = service.characteristics?.first(where: { $0.uuid == BMSUUIDs.requestCharacteristic })
        notifyCharacteristic = service.characteristics?.first(where: { $0.uuid == BMSUUIDs.responseCharacteristic })

        guard let notifyCharacteristic else {
            onError?("未找到响应特征 \(BMSUUIDs.responseCharacteristic.uuidString)")
            return
        }

        guard writeCharacteristic != nil else {
            onError?("未找到请求特征 \(BMSUUIDs.requestCharacteristic.uuidString)")
            return
        }

        peripheral.setNotifyValue(true, for: notifyCharacteristic)
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        if let error {
            onError?("订阅通知失败: \(error.localizedDescription)")
            return
        }

        if characteristic.uuid == BMSUUIDs.responseCharacteristic, characteristic.isNotifying {
            onReady?(peripheral)
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error {
            onError?("读取通知失败: \(error.localizedDescription)")
            return
        }

        guard characteristic.uuid == BMSUUIDs.responseCharacteristic, let data = characteristic.value else {
            return
        }

        onData?(data)
    }
}

private extension CBManagerState {
    var readableName: String {
        switch self {
        case .unknown:
            "unknown"
        case .resetting:
            "resetting"
        case .unsupported:
            "unsupported"
        case .unauthorized:
            "unauthorized"
        case .poweredOff:
            "poweredOff"
        case .poweredOn:
            "poweredOn"
        @unknown default:
            "future"
        }
    }
}
