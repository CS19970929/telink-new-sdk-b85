import Foundation
import CoreBluetooth

struct DiscoverySnapshot {
    var localName: String
    var peripheralName: String
    var advertisedServices: [String]
    var isConnectable: Bool?

    var preferredName: String {
        if !localName.isEmpty {
            return localName
        }
        return peripheralName
    }

    var alternateName: String {
        if !localName.isEmpty && !peripheralName.isEmpty && localName != peripheralName {
            return peripheralName
        }
        return ""
    }
}

enum ScanMode: String, CaseIterable, Identifiable {
    case targetFirmware = "当前固件"
    case allDevices = "全部设备"

    var id: String { rawValue }

    var serviceFilter: [CBUUID]? {
        switch self {
        case .targetFirmware:
            [
                CBUUID(string: "180F"),
                CBUUID(string: "1812"),
            ]
        case .allDevices:
            nil
        }
    }

    var note: String {
        switch self {
        case .targetFirmware:
            "仅扫描当前固件广播里的 16-bit UUID: 180F / 1812"
        case .allDevices:
            "扫描周围全部 BLE 设备"
        }
    }
}

enum DetailPage: String, CaseIterable, Identifiable {
    case batteryStatus = "电池状态"
    case debugWorkbench = "调试工作台"

    var id: String { rawValue }
}

enum ConnectionStatus: String {
    case idle = "空闲"
    case scanning = "扫描中"
    case connecting = "连接中"
    case connected = "已连接"
    case ready = "可收发"
    case disconnected = "已断开"
    case failed = "异常"
}

enum ExchangeDirection {
    case tx
    case rx
    case info
    case error

    var label: String {
        switch self {
        case .tx:
            "TX"
        case .rx:
            "RX"
        case .info:
            "INFO"
        case .error:
            "ERR"
        }
    }
}

struct DiscoveredDevice: Identifiable, Equatable {
    let id: UUID
    var name: String
    var alternateName: String
    var rssi: Int
    var lastSeen: Date
    var isConnected: Bool
    var advertisedServices: [String]
    var isConnectable: Bool?

    var displayName: String {
        if name.isEmpty {
            "Unnamed \(id.uuidString.prefix(6))"
        } else {
            name
        }
    }

    var rssiSummary: String {
        "\(rssi) dBm"
    }

    var advertisedServicesSummary: String {
        if advertisedServices.isEmpty {
            "No advertised UUID"
        } else {
            advertisedServices.joined(separator: ", ")
        }
    }

    var isLikelyBMS: Bool {
        let uppercaseName = displayName.uppercased()
        if uppercaseName.hasPrefix("BT") {
            return true
        }

        let advertisedSet = Set(advertisedServices.map { $0.uppercased() })
        let containsBattery = advertisedSet.contains("180F") || advertisedSet.contains("0000180F-0000-1000-8000-00805F9B34FB")
        let containsHID = advertisedSet.contains("1812") || advertisedSet.contains("00001812-0000-1000-8000-00805F9B34FB")
        return containsBattery || containsHID
    }
}

struct ExchangeLogEntry: Identifiable {
    let id = UUID()
    let timestamp: Date
    let direction: ExchangeDirection
    let title: String
    let payloadHex: String
    let note: String
}

struct DeviceIdentitySnapshot {
    var displayName: String = "—"
    var macAddress: String = "—"
    var serialNumber: String = "—"
    var hardwareVersion: String = "—"
    var softwareVersion: String = "—"
}

enum BatteryDataSource {
    case unavailable
    case legacyRegisters
    case realtimeWindow

    var title: String {
        switch self {
        case .unavailable:
            "未读取"
        case .legacyRegisters:
            "旧寄存器兼容模式"
        case .realtimeWindow:
            "实时窗口模式"
        }
    }

    var note: String {
        switch self {
        case .unavailable:
            "尚未建立数据快照。"
        case .legacyRegisters:
            "当前板子未返回 `0xD120~0xD12A`，上位机退回到当前工程 `stCell_Info` 的旧寄存器布局 `0xD000~0xD03E` 与 `0xD115~0xD116`。此模式可以显示单串电压、总压、电流方向、SOC、温度和状态字，但它依赖当前项目的内存平铺事实。"
        case .realtimeWindow:
            "当前板子已返回 `0xD120~0xD12A`，电压、电流、温度、SOC 采用实时窗口；单串电压和状态字仍来自旧寄存器区。"
        }
    }
}

struct CellVoltageSample: Identifiable {
    let index: Int
    let millivolts: UInt16

    var id: Int { index }

    var title: String {
        "Cell \(index)"
    }

    var voltageText: String {
        String(format: "%.3f V", Double(millivolts) / 1000.0)
    }

    var detailText: String {
        "\(millivolts) mV"
    }
}

struct StatusFlagSample: Identifiable {
    let key: String
    let title: String
    let isActive: Bool

    var id: String { key }
}

struct BatteryStatusSnapshot {
    var isSupported = false
    var source: BatteryDataSource = .unavailable
    var supportsRealtimeWindow = false
    var protocolVersion: UInt16 = 0
    var packVoltageRaw: UInt16 = 0
    var signedCurrentRaw: Int16 = 0
    var socRaw: UInt16 = 0
    var maxTempRaw: UInt16 = 0
    var minTempRaw: UInt16 = 0
    var mosTempRaw: UInt16 = 0
    var maxCellVoltageRaw: UInt16 = 0
    var minCellVoltageRaw: UInt16 = 0
    var cellDeltaRaw: UInt16 = 0
    var maxCellPosition: Int = 0
    var minCellPosition: Int = 0
    var sohRaw: UInt16 = 0
    var capacityNowRaw: UInt16 = 0
    var capacityFullRaw: UInt16 = 0
    var capacityFactoryRaw: UInt16 = 0
    var cycleCountRaw: UInt16 = 0
    var legacyPackVoltageRawMV: UInt16 = 0
    var legacyBatteryTempADCmV: UInt16 = 0
    var legacyMosTempADCmV: UInt16 = 0
    var cellVoltages: [CellVoltageSample] = []
    var statusFlags: [StatusFlagSample] = []
    var systemStatusRaw: UInt32 = 0
    var updatedAt: Date?

    static let empty = BatteryStatusSnapshot()

    static func decode(
        realtimeWords: [UInt16],
        legacyCellWords: [UInt16],
        systemStatusWords: [UInt16],
        updatedAt: Date
    ) -> BatteryStatusSnapshot {
        let cellWords = Array(legacyCellWords.prefix(Int(RegisterCatalog.legacyCellArrayCount)))
        let seriesCount = min(Int(RegisterCatalog.currentProjectSeriesCount), cellWords.count)
        let cells = Array(cellWords.prefix(seriesCount)).enumerated().map { offset, value in
            CellVoltageSample(index: offset + 1, millivolts: value)
        }
        let legacyChargeCurrent = Int16(word(at: RegisterCatalog.legacyChargeCurrentIndex, in: cellWords))
        let legacyDischargeCurrent = Int16(word(at: RegisterCatalog.legacyDischargeCurrentIndex, in: cellWords))
        let legacySignedCurrent = legacyDischargeCurrent > 0 ? -legacyDischargeCurrent : legacyChargeCurrent

        let statusLow = systemStatusWords.indices.contains(0) ? UInt32(systemStatusWords[0]) : 0
        let statusHigh = systemStatusWords.indices.contains(1) ? UInt32(systemStatusWords[1]) : 0
        let statusRaw = statusLow | (statusHigh << 16)

        var snapshot = BatteryStatusSnapshot(
            isSupported: !cells.isEmpty || !systemStatusWords.isEmpty,
            source: .legacyRegisters,
            supportsRealtimeWindow: false,
            protocolVersion: 0,
            packVoltageRaw: word(at: RegisterCatalog.legacyPackVoltageEngineeringIndex, in: cellWords),
            signedCurrentRaw: legacySignedCurrent,
            socRaw: word(at: RegisterCatalog.legacySocIndex, in: cellWords),
            maxTempRaw: word(at: RegisterCatalog.legacyMaxTempIndex, in: cellWords),
            minTempRaw: word(at: RegisterCatalog.legacyMinTempIndex, in: cellWords),
            mosTempRaw: word(at: RegisterCatalog.legacyMosTemperatureIndex, in: cellWords),
            maxCellVoltageRaw: word(at: RegisterCatalog.legacyMaxCellVoltageIndex, in: cellWords),
            minCellVoltageRaw: word(at: RegisterCatalog.legacyMinCellVoltageIndex, in: cellWords),
            cellDeltaRaw: word(at: RegisterCatalog.legacyCellDeltaIndex, in: cellWords),
            maxCellPosition: Int(word(at: RegisterCatalog.legacyMaxCellPositionIndex, in: cellWords)),
            minCellPosition: Int(word(at: RegisterCatalog.legacyMinCellPositionIndex, in: cellWords)),
            sohRaw: word(at: RegisterCatalog.legacySohIndex, in: cellWords),
            capacityNowRaw: word(at: RegisterCatalog.legacyCapacityNowIndex, in: cellWords),
            capacityFullRaw: word(at: RegisterCatalog.legacyCapacityFullIndex, in: cellWords),
            capacityFactoryRaw: word(at: RegisterCatalog.legacyCapacityFactoryIndex, in: cellWords),
            cycleCountRaw: word(at: RegisterCatalog.legacyCycleCountIndex, in: cellWords),
            legacyPackVoltageRawMV: word(at: RegisterCatalog.legacyPackVoltageADCIndex, in: cellWords),
            legacyBatteryTempADCmV: word(at: RegisterCatalog.legacyBatteryTempADCIndex, in: cellWords),
            legacyMosTempADCmV: word(at: RegisterCatalog.legacyMosTempADCIndex, in: cellWords),
            cellVoltages: cells,
            statusFlags: statusFlags(from: statusRaw),
            systemStatusRaw: statusRaw,
            updatedAt: updatedAt
        )

        guard realtimeWords.count >= Int(RegisterCatalog.realtimeStatusCount),
              realtimeWords[0] == RegisterCatalog.realtimeStatusMagic else {
            return snapshot
        }

        snapshot.source = .realtimeWindow
        snapshot.supportsRealtimeWindow = true
        snapshot.protocolVersion = realtimeWords[1]
        snapshot.packVoltageRaw = realtimeWords[2]
        snapshot.signedCurrentRaw = Int16(bitPattern: realtimeWords[3])
        snapshot.socRaw = realtimeWords[4]
        snapshot.maxTempRaw = realtimeWords[5]
        snapshot.minTempRaw = realtimeWords[6]
        snapshot.mosTempRaw = realtimeWords[7]
        snapshot.maxCellVoltageRaw = realtimeWords[8]
        snapshot.minCellVoltageRaw = realtimeWords[9]
        snapshot.cellDeltaRaw = realtimeWords[10]
        return snapshot
    }

    var sourceTitle: String {
        source.title
    }

    var sourceNote: String {
        source.note
    }

    var packVoltageText: String {
        guard isSupported else { return "—" }
        return String(format: "%.2f V", Double(packVoltageRaw) / 100.0)
    }

    var packVoltageDetailText: String {
        guard isSupported else { return "—" }
        if supportsRealtimeWindow {
            return "实时窗口 raw \(packVoltageRaw)"
        }
        return "旧寄存器 raw \(packVoltageRaw) / 镜像 \(legacyPackVoltageRawMV) mV"
    }

    var currentText: String {
        guard isSupported else { return "—" }
        return String(format: "%.1f A", Double(signedCurrentRaw) / 10.0)
    }

    var currentDirectionText: String {
        guard isSupported else { return "未支持" }
        if signedCurrentRaw > 0 {
            return "充电"
        }
        if signedCurrentRaw < 0 {
            return "放电"
        }
        return "静置"
    }

    var socText: String {
        guard isSupported else { return "—" }
        return "\(socRaw) %"
    }

    var maxTempText: String {
        return temperatureText(from: maxTempRaw)
    }

    var minTempText: String {
        return temperatureText(from: minTempRaw)
    }

    var mosTempText: String {
        return temperatureText(from: mosTempRaw)
    }

    var maxCellVoltageText: String {
        guard isSupported else { return "—" }
        guard maxCellVoltageRaw > 0 else { return "—" }
        return "\(maxCellVoltageRaw) mV"
    }

    var minCellVoltageText: String {
        guard isSupported else { return "—" }
        guard minCellVoltageRaw > 0 else { return "—" }
        return "\(minCellVoltageRaw) mV"
    }

    var cellDeltaText: String {
        guard isSupported else { return "—" }
        guard cellDeltaRaw > 0 || maxCellVoltageRaw > 0 || minCellVoltageRaw > 0 else { return "—" }
        return "\(cellDeltaRaw) mV"
    }

    var maxCellPositionText: String {
        guard maxCellPosition > 0 else { return "—" }
        return "Cell \(maxCellPosition)"
    }

    var minCellPositionText: String {
        guard minCellPosition > 0 else { return "—" }
        return "Cell \(minCellPosition)"
    }

    var legacyBatteryTempADCText: String {
        guard legacyBatteryTempADCmV > 0 else { return "—" }
        return "\(legacyBatteryTempADCmV) mV"
    }

    var legacyMosTempADCText: String {
        guard legacyMosTempADCmV > 0 else { return "—" }
        return "\(legacyMosTempADCmV) mV"
    }

    var sohText: String {
        guard isSupported else { return "—" }
        return "\(sohRaw) %"
    }

    var cycleCountText: String {
        guard isSupported else { return "—" }
        return "\(cycleCountRaw)"
    }

    var capacityNowText: String {
        capacityText(from: capacityNowRaw)
    }

    var capacityFullText: String {
        capacityText(from: capacityFullRaw)
    }

    var capacityFactoryText: String {
        capacityText(from: capacityFactoryRaw)
    }

    var activeStatusFlags: [StatusFlagSample] {
        statusFlags.filter(\.isActive)
    }

    var systemStatusHexText: String {
        String(format: "0x%08X", systemStatusRaw)
    }

    var updatedAtText: String {
        guard let updatedAt else { return "未刷新" }
        return updatedAt.formatted(date: .omitted, time: .standard)
    }

    private func temperatureText(from raw: UInt16) -> String {
        guard isSupported else { return "—" }
        return String(format: "%.1f °C", Double(raw) / 10.0 - 40.0)
    }

    private func capacityText(from raw: UInt16) -> String {
        guard isSupported else { return "—" }
        return String(format: "%.2f Ah", Double(raw) / 100.0)
    }

    private static func word(at index: Int, in words: [UInt16]) -> UInt16 {
        guard words.indices.contains(index) else { return 0 }
        return words[index]
    }

    private static func statusFlags(from raw: UInt32) -> [StatusFlagSample] {
        [
            StatusFlagSample(key: "startup", title: "启动完成", isActive: raw.bit(0)),
            StatusFlagSample(key: "mos_pre", title: "预充 MOS", isActive: raw.bit(1)),
            StatusFlagSample(key: "mos_chg", title: "充电 MOS", isActive: raw.bit(2)),
            StatusFlagSample(key: "mos_dsg", title: "放电 MOS", isActive: raw.bit(3)),
            StatusFlagSample(key: "relay_pre", title: "预充继电器", isActive: raw.bit(4)),
            StatusFlagSample(key: "relay_chg", title: "充电继电器", isActive: raw.bit(5)),
            StatusFlagSample(key: "relay_dsg", title: "放电继电器", isActive: raw.bit(6)),
            StatusFlagSample(key: "relay_main", title: "主继电器", isActive: raw.bit(7)),
            StatusFlagSample(key: "heat", title: "加热", isActive: raw.bit(8)),
            StatusFlagSample(key: "cool", title: "冷却", isActive: raw.bit(9)),
            StatusFlagSample(key: "afe1", title: "AFE1", isActive: raw.bit(10)),
            StatusFlagSample(key: "afe2", title: "AFE2", isActive: raw.bit(11)),
            StatusFlagSample(key: "balance", title: "均衡", isActive: raw.bit(12)),
            StatusFlagSample(key: "sleep", title: "待休眠", isActive: raw.bit(13)),
            StatusFlagSample(key: "bn_close", title: "BMS 关断输出", isActive: raw.bit(14)),
            StatusFlagSample(key: "heat_close", title: "加热关闭输出", isActive: raw.bit(15)),
            StatusFlagSample(key: "driver_ext", title: "外部驱动控制", isActive: raw.bit(18)),
        ]
    }
}

private extension UInt32 {
    func bit(_ index: Int) -> Bool {
        ((self >> UInt32(index)) & 0x1) == 0x1
    }
}

struct RegisterBlock: Identifiable {
    let id = UUID()
    let title: String
    let startAddress: UInt16
    let words: [UInt16]
    let updatedAt: Date
    let responseHex: String
}
