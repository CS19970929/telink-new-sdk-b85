export const OTA_SERVICE_UUID = '00010203-0405-0607-0809-0A0B0C0D1912'
export const OTA_CHARACTERISTIC_UUID = '00010203-0405-0607-0809-0A0B0C0D2B12'
export const OTA_PDU_BYTES = 16
export const OTA_PACKET_BYTES = 20

export interface TelinkFirmwareImage {
  source: Uint8Array
  firmware: Uint8Array
  declaredSize: number
  packetCount: number
  maxIndex: number
}

export function crc16(data: Uint8Array): number {
  let crc = 0xffff
  for (const byte of data) {
    crc ^= byte
    for (let i = 0; i < 8; i += 1) {
      crc = (crc & 1) !== 0 ? (crc >>> 1) ^ 0xa001 : crc >>> 1
    }
  }
  return crc & 0xffff
}

export function startPacket(): Uint8Array {
  return new Uint8Array([0x01, 0xff])
}

export function dataPacket(index: number, payload: Uint8Array): Uint8Array {
  if (index < 0 || index > 0xffff) throw new Error(`OTA index out of range: ${index}`)
  if (payload.length > OTA_PDU_BYTES) throw new Error(`OTA payload must be <= ${OTA_PDU_BYTES} bytes`)
  const body = new Uint8Array(18)
  body.fill(0xff)
  body[0] = index & 0xff
  body[1] = (index >>> 8) & 0xff
  body.set(payload, 2)
  const crc = crc16(body)
  const packet = new Uint8Array(20)
  packet.set(body, 0)
  packet[18] = crc & 0xff
  packet[19] = (crc >>> 8) & 0xff
  return packet
}

export function endPacket(maxIndex: number): Uint8Array {
  if (maxIndex < 0 || maxIndex > 0xffff) throw new Error(`OTA max index out of range: ${maxIndex}`)
  const inverted = maxIndex ^ 0xffff
  return new Uint8Array([
    0x02, 0xff,
    maxIndex & 0xff, (maxIndex >>> 8) & 0xff,
    inverted & 0xff, (inverted >>> 8) & 0xff,
  ])
}

export function parseResult(packet: Uint8Array): number | undefined {
  if (packet.length < 3 || packet[0] !== 0x06 || packet[1] !== 0xff) return undefined
  return packet[2]
}

export function parseFirmware(source: Uint8Array): TelinkFirmwareImage {
  if (source.length < 0x1c) throw new Error('firmware is too small to contain Telink header')
  const mark = String.fromCharCode(...source.slice(0x08, 0x0c))
  if (mark !== 'KNLT') throw new Error('firmware mark at 0x08 is not KNLT')
  const declaredSize = readU32LE(source, 0x18)
  if (declaredSize <= 0 || declaredSize > source.length) {
    throw new Error(`invalid firmware size field: declared=${declaredSize}, file=${source.length}`)
  }
  const packetCount = Math.ceil(declaredSize / OTA_PDU_BYTES)
  if (packetCount <= 0 || packetCount > 0x10000) throw new Error(`unsupported OTA packet count: ${packetCount}`)
  return {
    source,
    firmware: source.slice(0, declaredSize),
    declaredSize,
    packetCount,
    maxIndex: packetCount - 1,
  }
}

export function firmwareDataPacket(image: TelinkFirmwareImage, index: number): Uint8Array {
  if (index < 0 || index >= image.packetCount) throw new Error(`OTA packet index out of image: ${index}`)
  const start = index * OTA_PDU_BYTES
  return dataPacket(index, image.firmware.slice(start, start + OTA_PDU_BYTES))
}

export function resultText(code: number): string {
  const map: Record<number, string> = {
    0x00: 'OTA_SUCCESS',
    0x01: 'OTA_DATA_PACKET_SEQ_ERR',
    0x02: 'OTA_PACKET_INVALID',
    0x03: 'OTA_DATA_CRC_ERR',
    0x04: 'OTA_WRITE_FLASH_ERR',
    0x05: 'OTA_DATA_INCOMPLETE',
    0x06: 'OTA_FLOW_ERR',
    0x07: 'OTA_FW_CHECK_ERR',
    0x08: 'OTA_VERSION_COMPARE_ERR',
    0x09: 'OTA_PDU_LEN_ERR',
    0x0a: 'OTA_FIRMWARE_MARK_ERR',
    0x0b: 'OTA_FW_SIZE_ERR',
    0x0c: 'OTA_DATA_PACKET_TIMEOUT',
    0x0d: 'OTA_TIMEOUT',
    0x0e: 'OTA_FAIL_DUE_TO_CONNECTION_TERMINATE',
    0x0f: 'OTA_MCU_NOT_SUPPORTED',
    0x10: 'OTA_LOGIC_ERR',
  }
  return map[code] ?? `OTA_UNKNOWN_RESULT_0x${code.toString(16).toUpperCase().padStart(2, '0')}`
}

function readU32LE(data: Uint8Array, offset: number): number {
  return (
    data[offset] |
    (data[offset + 1] << 8) |
    (data[offset + 2] << 16) |
    (data[offset + 3] << 24)
  ) >>> 0
}
