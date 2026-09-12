import CoreBluetooth
import Foundation

struct DiscoveryRecord: Codable {
    let id: String
    let localName: String
    let peripheralName: String
    let rssi: Int
    let advertisedServices: [String]
    let overflowServices: [String]
    let isConnectable: Bool?
    let manufacturerDataHex: String
    let seenAt: String
}

struct ProbeReport: Codable {
    let startedAt: String
    let finishedAt: String
    let durationSeconds: Int
    let bluetoothState: String
    let discoveryCount: Int
    let btLikeCount: Int
    let targetLikeCount: Int
    let discoveries: [DiscoveryRecord]
    let note: String
}

final class BLEProbe: NSObject, CBCentralManagerDelegate {
    private let outputPath: String
    private let durationSeconds: Int
    private var bluetoothState = "unknown"
    private var discoveries = [UUID: DiscoveryRecord]()
    private let startedAt = ISO8601DateFormatter().string(from: .now)
    private lazy var central = CBCentralManager(delegate: self, queue: nil)
    private var timeoutWorkItem: DispatchWorkItem?

    init(outputPath: String, durationSeconds: Int) {
        self.outputPath = outputPath
        self.durationSeconds = durationSeconds
        super.init()
    }

    func start() {
        _ = central
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        bluetoothState = readableName(for: central.state)

        switch central.state {
        case .poweredOn:
            central.scanForPeripherals(
                withServices: nil,
                options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
            )

            let workItem = DispatchWorkItem { [weak self] in
                self?.finish()
            }
            timeoutWorkItem = workItem
            DispatchQueue.main.asyncAfter(deadline: .now() + .seconds(durationSeconds), execute: workItem)

        case .unsupported, .unauthorized, .poweredOff:
            finish()

        case .unknown, .resetting:
            break

        @unknown default:
            finish()
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let localName = advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? ""
        let peripheralName = peripheral.name ?? ""
        let serviceUUIDs = ((advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID]) ?? []).map(normalized)
        let overflowUUIDs = ((advertisementData[CBAdvertisementDataOverflowServiceUUIDsKey] as? [CBUUID]) ?? []).map(normalized)
        let manufacturerData = (advertisementData[CBAdvertisementDataManufacturerDataKey] as? Data)?.map { String(format: "%02X", $0) }.joined() ?? ""
        let record = DiscoveryRecord(
            id: peripheral.identifier.uuidString,
            localName: localName,
            peripheralName: peripheralName,
            rssi: RSSI.intValue,
            advertisedServices: serviceUUIDs,
            overflowServices: overflowUUIDs,
            isConnectable: advertisementData[CBAdvertisementDataIsConnectable] as? Bool,
            manufacturerDataHex: manufacturerData,
            seenAt: ISO8601DateFormatter().string(from: .now)
        )
        discoveries[peripheral.identifier] = record
    }

    private func normalized(_ uuid: CBUUID) -> String {
        let raw = uuid.uuidString.uppercased()
        if raw.hasPrefix("0000"), raw.hasSuffix("-0000-1000-8000-00805F9B34FB") {
            return String(raw.dropFirst(4).prefix(4))
        }
        return raw
    }

    private func readableName(for state: CBManagerState) -> String {
        switch state {
        case .unknown:
            return "unknown"
        case .resetting:
            return "resetting"
        case .unsupported:
            return "unsupported"
        case .unauthorized:
            return "unauthorized"
        case .poweredOff:
            return "poweredOff"
        case .poweredOn:
            return "poweredOn"
        @unknown default:
            return "future"
        }
    }

    private func finish() {
        timeoutWorkItem?.cancel()
        central.stopScan()

        let allDiscoveries = discoveries.values.sorted { lhs, rhs in
            if lhs.rssi != rhs.rssi {
                return lhs.rssi > rhs.rssi
            }
            let lhsName = lhs.localName.isEmpty ? lhs.peripheralName : lhs.localName
            let rhsName = rhs.localName.isEmpty ? rhs.peripheralName : rhs.localName
            return lhsName < rhsName
        }

        let btLikeCount = allDiscoveries.filter {
            let name = ($0.localName.isEmpty ? $0.peripheralName : $0.localName).uppercased()
            return name.hasPrefix("BT")
        }.count

        let targetLikeCount = allDiscoveries.filter {
            let serviceSet = Set(($0.advertisedServices + $0.overflowServices).map { $0.uppercased() })
            return serviceSet.contains("180F") || serviceSet.contains("1812")
        }.count

        let note = "目标固件名字在 scan response，主广播常见 UUID 为 180F/1812。若 iPhone 已连接，板子通常不会继续对外广播。"
        let report = ProbeReport(
            startedAt: startedAt,
            finishedAt: ISO8601DateFormatter().string(from: .now),
            durationSeconds: durationSeconds,
            bluetoothState: bluetoothState,
            discoveryCount: allDiscoveries.count,
            btLikeCount: btLikeCount,
            targetLikeCount: targetLikeCount,
            discoveries: allDiscoveries,
            note: note
        )

        do {
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            let data = try encoder.encode(report)
            try data.write(to: URL(fileURLWithPath: outputPath), options: .atomic)
        } catch {
            let fallback = """
            {
              "error": "\(error.localizedDescription.replacingOccurrences(of: "\"", with: "'"))",
              "bluetoothState": "\(bluetoothState)"
            }
            """
            try? fallback.data(using: .utf8)?.write(to: URL(fileURLWithPath: outputPath), options: .atomic)
        }

        CFRunLoopStop(CFRunLoopGetMain())
    }
}

func parseArgument(_ flag: String) -> String? {
    guard let index = CommandLine.arguments.firstIndex(of: flag), CommandLine.arguments.indices.contains(index + 1) else {
        return nil
    }
    return CommandLine.arguments[index + 1]
}

let outputPath = parseArgument("--output") ?? "/tmp/ble-probe-report.json"
let durationSeconds = Int(parseArgument("--duration") ?? "12") ?? 12

let probe = BLEProbe(outputPath: outputPath, durationSeconds: durationSeconds)
probe.start()
RunLoop.main.run()
