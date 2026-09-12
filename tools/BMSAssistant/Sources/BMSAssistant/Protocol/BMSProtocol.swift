import CoreBluetooth
import Foundation

enum BMSUUIDs {
    static let sppService = CBUUID(string: "6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
    static let requestCharacteristic = CBUUID(string: "6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
    static let responseCharacteristic = CBUUID(string: "6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
}

enum RegisterCatalog {
    static let currentProjectSeriesCount = 10

    static let realtimeStatusMagic: UInt16 = 0x4253
    static let realtimeStatusStart: UInt16 = 0xD120
    static let realtimeStatusCount: UInt16 = 11

    static let legacyCellArrayStart: UInt16 = 0xD000
    static let legacyCellArrayCount: UInt16 = 63
    static let legacyBatteryTempADCIndex = 29
    static let legacyMosTempADCIndex = 30
    static let legacyPackVoltageADCIndex = 31
    static let legacyMaxCellVoltageIndex = 32
    static let legacyMinCellVoltageIndex = 33
    static let legacyMaxCellPositionIndex = 34
    static let legacyMinCellPositionIndex = 35
    static let legacyCellDeltaIndex = 36
    static let legacyPackVoltageEngineeringIndex = 37
    static let legacyTemperatureBaseIndex = 38
    static let legacyTemperatureCount = 10
    static let legacyMosTemperatureIndex = 47
    static let legacyMaxTempIndex = 48
    static let legacyMinTempIndex = 49
    static let legacyChargeCurrentIndex = 50
    static let legacyDischargeCurrentIndex = 51
    static let legacySocIndex = 52
    static let legacySohIndex = 53
    static let legacyCapacityNowIndex = 54
    static let legacyCapacityFullIndex = 55
    static let legacyCapacityFactoryIndex = 56
    static let legacyCycleCountIndex = 57

    static let macAddressStart: UInt16 = 0x0000
    static let macAddressCount: UInt16 = 3

    static let btNameStart: UInt16 = 0x0100
    static let btNameReadCount: UInt16 = 12

    static let productSerialStart: UInt16 = 0xC002
    static let productTextCount: UInt16 = 16
    static let productHardwareStart: UInt16 = 0xC012
    static let productSoftwareStart: UInt16 = 0xC022

    static let eventLogStart: UInt16 = 0xC008
    static let eventLogPreviewCount: UInt16 = 20

    static let systemStatusStart: UInt16 = 0xD115
    static let systemStatusCount: UInt16 = 2

    static let protectStart: UInt16 = 0x2100
    static let protectPreviewCount: UInt16 = 15

    static let socWriteRegister: UInt16 = 0x1005
    static let debugRegister1102: UInt16 = 0x1102
    static let debugRegister1103: UInt16 = 0x1103

    static let btNameMaxWriteBytes = 10
}

enum ModbusCodecError: LocalizedError {
    case invalidHex(String)
    case invalidFrame(String)
    case crcMismatch
    case unexpectedResponse(String)
    case requestTooLong(Int)
    case transportBusy(String)
    case transportNotReady

    var errorDescription: String? {
        switch self {
        case .invalidHex(let value):
            "无法解析十六进制输入: \(value)"
        case .invalidFrame(let reason):
            "Modbus 帧无效: \(reason)"
        case .crcMismatch:
            "收到的响应 CRC 校验失败"
        case .unexpectedResponse(let reason):
            "响应内容不符合预期: \(reason)"
        case .requestTooLong(let length):
            "请求长度 \(length) byte，超过当前固件 BLE 单包安全上限 20 byte"
        case .transportBusy(let name):
            "当前仍有未完成请求: \(name)"
        case .transportNotReady:
            "BLE 通道尚未就绪，请先连接并完成特征发现"
        }
    }
}

enum ModbusResponse {
    case readHolding(words: [UInt16])
    case writeSingleAck(register: UInt16, value: UInt16)
    case writeMultipleAck(register: UInt16, quantity: UInt16)
    case echo(Data)
    case exception(function: UInt8, code: UInt8)
}

enum HexCodec {
    static func parseAddress(_ text: String) throws -> UInt16 {
        let cleaned = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if cleaned.lowercased().hasPrefix("0x") {
            guard let value = UInt16(cleaned.dropFirst(2), radix: 16) else {
                throw ModbusCodecError.invalidHex(text)
            }
            return value
        }

        guard let value = UInt16(cleaned) else {
            throw ModbusCodecError.invalidHex(text)
        }
        return value
    }

    static func parseWords(_ text: String) throws -> [UInt16] {
        let separators = CharacterSet(charactersIn: ", \n\t")
        let parts = text
            .components(separatedBy: separators)
            .filter { !$0.isEmpty }

        guard !parts.isEmpty else {
            throw ModbusCodecError.invalidHex(text)
        }

        return try parts.map(parseAddress)
    }

    static func parseRawBytes(_ text: String) throws -> Data {
        let normalized = text
            .replacingOccurrences(of: "0x", with: "", options: .caseInsensitive)
            .replacingOccurrences(of: ",", with: " ")
        let hexCharacterSet = CharacterSet(charactersIn: "0123456789ABCDEFabcdef")
        let scalars = normalized.unicodeScalars.filter { hexCharacterSet.contains($0) }
        let cleaned = String(String.UnicodeScalarView(scalars))
        guard !cleaned.isEmpty, cleaned.count.isMultiple(of: 2) else {
            throw ModbusCodecError.invalidHex(text)
        }

        var bytes = [UInt8]()
        bytes.reserveCapacity(cleaned.count / 2)

        var index = cleaned.startIndex
        while index < cleaned.endIndex {
            let next = cleaned.index(index, offsetBy: 2)
            let pair = cleaned[index..<next]
            guard let value = UInt8(pair, radix: 16) else {
                throw ModbusCodecError.invalidHex(text)
            }
            bytes.append(value)
            index = next
        }

        return Data(bytes)
    }
}

enum ModbusCodec {
    static let deviceAddress: UInt8 = 0x01
    private static let echoFunction: UInt8 = 0x7F

    static func readHolding(start: UInt16, quantity: UInt16) -> Data {
        var bytes: [UInt8] = [deviceAddress, 0x03]
        bytes.append(contentsOf: start.bytesBE)
        bytes.append(contentsOf: quantity.bytesBE)
        return frame(for: bytes)
    }

    static func writeSingle(register: UInt16, value: UInt16) -> Data {
        var bytes: [UInt8] = [deviceAddress, 0x06]
        bytes.append(contentsOf: register.bytesBE)
        bytes.append(contentsOf: value.bytesBE)
        return frame(for: bytes)
    }

    static func writeMultiple(register: UInt16, values: [UInt16]) -> Data {
        var bytes: [UInt8] = [deviceAddress, 0x10]
        bytes.append(contentsOf: register.bytesBE)
        bytes.append(contentsOf: UInt16(values.count).bytesBE)
        bytes.append(UInt8(values.count * 2))
        for value in values {
            bytes.append(contentsOf: value.bytesBE)
        }
        return frame(for: bytes)
    }

    static func echo(payload: Data) -> Data {
        var bytes = [deviceAddress, echoFunction]
        bytes.append(contentsOf: payload)
        return frame(for: bytes)
    }

    static func parse(_ data: Data) throws -> ModbusResponse {
        guard data.count >= 4 else {
            throw ModbusCodecError.invalidFrame("长度小于最小 Modbus RTU 帧")
        }

        guard validateCRC(data) else {
            throw ModbusCodecError.crcMismatch
        }

        let function = data[1]
        if function == echoFunction {
            return .echo(data)
        }

        if function & 0x80 == 0x80 {
            guard data.count >= 5 else {
                throw ModbusCodecError.invalidFrame("异常响应长度不足")
            }
            return .exception(function: function & 0x7F, code: data[2])
        }

        switch function {
        case 0x03:
            guard data.count >= 5 else {
                throw ModbusCodecError.invalidFrame("读寄存器响应长度不足")
            }
            let byteCount = Int(data[2])
            guard data.count == byteCount + 5 else {
                throw ModbusCodecError.invalidFrame("读寄存器响应字节数不匹配")
            }
            let payload = data[3..<(3 + byteCount)]
            let bytes = Array(payload)
            var words = [UInt16]()
            words.reserveCapacity(byteCount / 2)
            for index in stride(from: 0, to: bytes.count, by: 2) {
                words.append(UInt16(bytes[index]) << 8 | UInt16(bytes[index + 1]))
            }
            return .readHolding(words: words)

        case 0x06:
            guard data.count == 8 else {
                throw ModbusCodecError.invalidFrame("写单寄存器响应长度应为 8")
            }
            return .writeSingleAck(register: readUInt16BE(data, at: 2), value: readUInt16BE(data, at: 4))

        case 0x10:
            guard data.count == 8 else {
                throw ModbusCodecError.invalidFrame("写多寄存器响应长度应为 8")
            }
            return .writeMultipleAck(register: readUInt16BE(data, at: 2), quantity: readUInt16BE(data, at: 4))

        default:
            throw ModbusCodecError.unexpectedResponse("未支持的功能码 0x\(String(function, radix: 16, uppercase: true))")
        }
    }

    static func inferExpectedLength(buffer: Data, hint: Int?) -> Int? {
        if let hint {
            return hint
        }

        guard buffer.count >= 2 else {
            return nil
        }

        let function = buffer[1]
        if function == echoFunction {
            return nil
        }

        if function & 0x80 == 0x80 {
            return 5
        }

        switch function {
        case 0x03:
            guard buffer.count >= 3 else {
                return nil
            }
            return Int(buffer[2]) + 5
        case 0x06, 0x10:
            return 8
        default:
            return nil
        }
    }

    static func validateCRC(_ data: Data) -> Bool {
        guard data.count >= 4 else {
            return false
        }
        let payload = data.dropLast(2)
        let crc = crc16(payload)
        return data[data.count - 2] == UInt8(truncatingIfNeeded: crc & 0xFF) &&
            data[data.count - 1] == UInt8(truncatingIfNeeded: crc >> 8)
    }

    static func asciiString(from words: [UInt16]) -> String {
        let bytes = words.flatMap { $0.bytesBE }
        let terminated = bytes.prefix { $0 != 0x00 }
        return String(decoding: terminated, as: UTF8.self)
    }

    static func macString(from words: [UInt16]) -> String {
        let bytes = words.flatMap { $0.bytesBE }
        return bytes.prefix(6).map { String(format: "%02X", $0) }.joined(separator: ":")
    }

    static func encodeASCIIWords(_ string: String) -> [UInt16] {
        let bytes = Array(string.utf8)
        let padded = bytes.count.isMultiple(of: 2) ? bytes : bytes + [0x00]
        var words = [UInt16]()
        words.reserveCapacity((padded.count + 1) / 2)

        for index in stride(from: 0, to: padded.count, by: 2) {
            let hi = UInt16(padded[index]) << 8
            let lo = UInt16(padded[index + 1])
            words.append(hi | lo)
        }

        return words
    }

    static func frame(for body: [UInt8]) -> Data {
        let crc = crc16(body)
        var frame = body
        frame.append(UInt8(truncatingIfNeeded: crc & 0xFF))
        frame.append(UInt8(truncatingIfNeeded: crc >> 8))
        return Data(frame)
    }

    private static func crc16<S: Sequence>(_ bytes: S) -> UInt16 where S.Element == UInt8 {
        var crc: UInt16 = 0xFFFF
        for byte in bytes {
            crc ^= UInt16(byte)
            for _ in 0..<8 {
                if crc & 0x0001 != 0 {
                    crc = (crc >> 1) ^ 0xA001
                } else {
                    crc >>= 1
                }
            }
        }
        return crc
    }

    private static func readUInt16BE(_ data: Data, at index: Int) -> UInt16 {
        UInt16(data[index]) << 8 | UInt16(data[index + 1])
    }
}

struct ResponseAccumulator {
    enum Event {
        case waiting(expectedLength: Int?, fragments: Int)
        case completed(frame: Data, fragments: Int)
        case invalidCRC(frame: Data, fragments: Int)
    }

    private(set) var buffer = Data()
    private(set) var fragmentCount = 0
    private var expectedLengthHint: Int?

    mutating func reset(expectedLengthHint: Int? = nil) {
        buffer.removeAll(keepingCapacity: true)
        fragmentCount = 0
        self.expectedLengthHint = expectedLengthHint
    }

    mutating func append(_ fragment: Data) -> Event {
        fragmentCount += 1
        buffer.append(fragment)

        let expectedLength = ModbusCodec.inferExpectedLength(buffer: buffer, hint: expectedLengthHint)
        guard let expectedLength else {
            return .waiting(expectedLength: nil, fragments: fragmentCount)
        }

        guard buffer.count >= expectedLength else {
            return .waiting(expectedLength: expectedLength, fragments: fragmentCount)
        }

        let frame = Data(buffer.prefix(expectedLength))
        let fragments = fragmentCount
        let remainder = buffer.dropFirst(expectedLength)
        buffer = Data(remainder)
        fragmentCount = 0

        if ModbusCodec.validateCRC(frame) {
            return .completed(frame: frame, fragments: fragments)
        } else {
            return .invalidCRC(frame: frame, fragments: fragments)
        }
    }
}

private extension UInt16 {
    var bytesBE: [UInt8] {
        [UInt8((self >> 8) & 0xFF), UInt8(self & 0xFF)]
    }
}

extension Data {
    var spacedHexString: String {
        map { String(format: "%02X", $0) }.joined(separator: " ")
    }
}
