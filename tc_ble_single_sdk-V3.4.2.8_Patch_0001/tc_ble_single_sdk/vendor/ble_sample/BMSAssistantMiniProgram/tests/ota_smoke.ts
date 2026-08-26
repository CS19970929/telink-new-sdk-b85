import { dataPacket, endPacket, parseResult, startPacket } from '../core/ota'

function hex(text: string): Uint8Array {
  const compact = text.replace(/\s+/g, '')
  const out = new Uint8Array(compact.length / 2)
  for (let i = 0; i < compact.length; i += 2) out[i / 2] = parseInt(compact.slice(i, i + 2), 16)
  return out
}

function equal(actual: Uint8Array, expected: Uint8Array, title: string): void {
  if (actual.length !== expected.length || actual.some((value, i) => value !== expected[i])) {
    throw new Error(`${title} failed: ${Array.from(actual).map(v => v.toString(16).padStart(2, '0')).join(' ')}`)
  }
  console.log(`[PASS] ${title}`)
}

equal(startPacket(), hex('01 FF'), 'OTA start')
equal(
  dataPacket(0, hex('26 80 00 00 00 00 5D 02 4B 4E 4C 54 30 04 88 00')),
  hex('00 00 26 80 00 00 00 00 5D 02 4B 4E 4C 54 30 04 88 00 1C A3'),
  'OTA data packet 0',
)
equal(endPacket(0x14fa), hex('02 FF FA 14 05 EB'), 'OTA end')
if (parseResult(hex('06 FF 00')) !== 0) throw new Error('OTA success result parse failed')
if (parseResult(hex('06 FF 03')) !== 3) throw new Error('OTA CRC error result parse failed')
console.log('[PASS] OTA result parse')
