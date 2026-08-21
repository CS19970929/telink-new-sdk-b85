import {
  ResponseAccumulator,
  echo,
  parseResponse,
  readHolding,
  spacedHex,
  writeSingle,
} from '../core/protocol';

function expectEqual(actual: string, expected: string, title: string): void {
  if (actual !== expected) {
    throw new Error(`${title}: expected=${expected}, actual=${actual}`);
  }
  console.log(`[PASS] ${title}`);
}

expectEqual(
  spacedHex(readHolding(0xD120, 11)),
  '01 03 D1 20 00 0B 3C FB',
  'read realtime request',
);
expectEqual(
  spacedHex(readHolding(0xD115, 2)),
  '01 03 D1 15 00 02 EC F3',
  'read system status request',
);
expectEqual(
  spacedHex(writeSingle(0x1005, 60)),
  '01 06 10 05 00 3C 9D 1A',
  'write SOC request',
);
expectEqual(
  spacedHex(echo(new Uint8Array([0x12, 0x34, 0x56, 0x78]))),
  '01 7F 12 34 56 78 6F 34',
  'echo request',
);

const accumulator = new ResponseAccumulator();
const first = accumulator.append(new Uint8Array([0x01, 0x03, 0x04, 0x00]));
if (first !== undefined) throw new Error('fragment 1 should still be waiting');
const second = accumulator.append(new Uint8Array([0x03, 0x00, 0x01, 0xCB, 0xF3]));
if (second === undefined) throw new Error('fragment 2 should complete the frame');
expectEqual(spacedHex(second), '01 03 04 00 03 00 01 CB F3', 'notify accumulator');
const parsed = parseResponse(second);
if (parsed.kind !== 'read_holding' || parsed.words[0] !== 0x0003 || parsed.words[1] !== 0x0001) {
  throw new Error('read response parse mismatch');
}
console.log('[PASS] read response parse');
