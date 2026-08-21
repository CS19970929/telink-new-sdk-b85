import Foundation

enum TelinkOTA {
    static let serviceUUID = "00010203-0405-0607-0809-0A0B0C0D1912"
    static let characteristicUUID = "00010203-0405-0607-0809-0A0B0C0D2B12"
    static let pduBytes = 16
    static let packetBytes = 20

    struct FirmwareImage {
        let source: Data
        let firmware: Data
        let declaredSize: Int
        let packetCount: Int
        let maxIndex: Int

        init(data: Data) throws {
            guard data.count >= 0x1C else {
                throw OTAError.invalidFirmware("firmware is too small to contain Telink header")
            }
            guard Data(data[0x08..<0x0C]) == Data("KNLT".utf8) else {
                throw OTAError.invalidFirmware("firmware mark at 0x08 is not KNLT")
            }
            let size = Int(readUInt32LE(data, at: 0x18))
            guard size > 0, size <= data.count else {
                throw OTAError.invalidFirmware("invalid firmware size field: declared=\(size), file=\(data.count)")
            }
            let count = (size + pduBytes - 1) / pduBytes
            guard count > 0, count <= 0x10000 else {
                throw OTAError.invalidFirmware("unsupported OTA packet count: \(count)")
            }
            source = data
            firmware = Data(data.prefix(size))
            declaredSize = size
            packetCount = count
            maxIndex = count - 1
        }

        func dataPacket(index: Int) throws -> Data {
            guard index >= 0, index < packetCount else {
                throw OTAError.invalidIndex(index)
            }
            let start = index * pduBytes
            let end = min(start + pduBytes, firmware.count)
            return try TelinkOTA.dataPacket(index: index, payload: Data(firmware[start..<end]))
        }

        func endPacket() throws -> Data {
            try TelinkOTA.endPacket(maxIndex: maxIndex)
        }
    }

    enum OTAError: LocalizedError {
        case invalidFirmware(String)
        case invalidIndex(Int)
        case payloadTooLong(Int)

        var errorDescription: String? {
            switch self {
            case .invalidFirmware(let message): message
            case .invalidIndex(let index): "OTA index out of range: \(index)"
            case .payloadTooLong(let count): "OTA payload must be <= 16 bytes, got \(count)"
            }
        }
    }

    static func startPacket() -> Data {
        Data([0x01, 0xFF])
    }

    static func dataPacket(index: Int, payload: Data) throws -> Data {
        guard index >= 0, index <= 0xFFFF else { throw OTAError.invalidIndex(index) }
        guard payload.count <= pduBytes else { throw OTAError.payloadTooLong(payload.count) }
        var body = Data(littleEndian16(index))
        body.append(payload)
        if payload.count < pduBytes {
            body.append(Data(repeating: 0xFF, count: pduBytes - payload.count))
        }
        let crc = crc16(body)
        body.append(contentsOf: littleEndian16(Int(crc)))
        return body
    }

    static func endPacket(maxIndex: Int) throws -> Data {
        guard maxIndex >= 0, maxIndex <= 0xFFFF else { throw OTAError.invalidIndex(maxIndex) }
        var data = Data([0x02, 0xFF])
        data.append(contentsOf: littleEndian16(maxIndex))
        data.append(contentsOf: littleEndian16(maxIndex ^ 0xFFFF))
        return data
    }

    static func parseResult(_ data: Data) -> UInt8? {
        guard data.count >= 3, data[0] == 0x06, data[1] == 0xFF else { return nil }
        return data[2]
    }

    static func resultText(_ code: UInt8) -> String {
        switch code {
        case 0x00: "OTA_SUCCESS"
        case 0x01: "OTA_DATA_PACKET_SEQ_ERR"
        case 0x02: "OTA_PACKET_INVALID"
        case 0x03: "OTA_DATA_CRC_ERR"
        case 0x04: "OTA_WRITE_FLASH_ERR"
        case 0x05: "OTA_DATA_INCOMPLETE"
        case 0x06: "OTA_FLOW_ERR"
        case 0x07: "OTA_FW_CHECK_ERR"
        case 0x08: "OTA_VERSION_COMPARE_ERR"
        case 0x09: "OTA_PDU_LEN_ERR"
        case 0x0A: "OTA_FIRMWARE_MARK_ERR"
        case 0x0B: "OTA_FW_SIZE_ERR"
        case 0x0C: "OTA_DATA_PACKET_TIMEOUT"
        case 0x0D: "OTA_TIMEOUT"
        case 0x0E: "OTA_FAIL_DUE_TO_CONNECTION_TERMINATE"
        case 0x0F: "OTA_MCU_NOT_SUPPORTED"
        case 0x10: "OTA_LOGIC_ERR"
        default: String(format: "OTA_UNKNOWN_RESULT_0x%02X", code)
        }
    }

    static func crc16(_ data: Data) -> UInt16 {
        var crc: UInt16 = 0xFFFF
        for byte in data {
            crc ^= UInt16(byte)
            for _ in 0..<8 {
                crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xA001 : crc >> 1
            }
        }
        return crc
    }

    private static func littleEndian16(_ value: Int) -> [UInt8] {
        [UInt8(value & 0xFF), UInt8((value >> 8) & 0xFF)]
    }

    private static func readUInt32LE(_ data: Data, at offset: Int) -> UInt32 {
        UInt32(data[offset]) |
            (UInt32(data[offset + 1]) << 8) |
            (UInt32(data[offset + 2]) << 16) |
            (UInt32(data[offset + 3]) << 24)
    }
}
