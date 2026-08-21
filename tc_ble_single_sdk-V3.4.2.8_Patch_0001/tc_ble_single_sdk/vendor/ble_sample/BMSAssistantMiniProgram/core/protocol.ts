import {
  BMSGeneratedBLEConstraints,
  BMSGeneratedRegisterCatalog,
  BMSSystemStatusBits,
} from '../generated/BMSGeneratedRegisterCatalog';

export class BmsProtocolError extends Error {}

export type ParsedResponse =
  | { kind: 'read_holding'; words: number[]; raw: Uint8Array }
  | { kind: 'write_single_ack'; register: number; value: number; raw: Uint8Array }
  | { kind: 'write_multiple_ack'; register: number; quantity: number; raw: Uint8Array }
  | { kind: 'echo'; raw: Uint8Array }
  | { kind: 'exception'; function: number; code: number; raw: Uint8Array };

export interface BatteryStatusSnapshot {
  source: 'realtime_window' | 'legacy_registers';
  supportsRealtimeWindow: boolean;
  protocolVersion: number;
  packVoltage: number;
  signedCurrent: number;
  soc: number;
  soh: number;
  maxTemp: number;
  minTemp: number;
  mosTemp: number;
  maxCellVoltage: number;
  minCellVoltage: number;
  cellDelta: number;
  maxCellPosition: number;
  minCellPosition: number;
  capacityNow: number;
  capacityFull: number;
  capacityFactory: number;
  cycleCount: number;
  cellVoltages: number[];
  systemStatusRaw: number;
  activeStatusFlags: string[];
}

export function crc16(data: Uint8Array): number {
  let crc = 0xffff;
  for (const byte of data) {
    crc ^= byte;
    for (let i = 0; i < 8; i += 1) {
      crc = (crc & 0x0001) !== 0 ? ((crc >> 1) ^ 0xa001) : (crc >> 1);
    }
  }
  return crc & 0xffff;
}

export function frame(body: Uint8Array): Uint8Array {
  const crc = crc16(body);
  return concatBytes(body, new Uint8Array([crc & 0xff, (crc >> 8) & 0xff]));
}

export function readHolding(start: number, quantity: number): Uint8Array {
  return frame(new Uint8Array([0x01, 0x03, ...u16be(start), ...u16be(quantity)]));
}

export function writeSingle(register: number, value: number): Uint8Array {
  return frame(new Uint8Array([0x01, 0x06, ...u16be(register), ...u16be(value)]));
}

export function writeMultiple(register: number, values: number[]): Uint8Array {
  const payload: number[] = [];
  values.forEach((value) => payload.push(...u16be(value)));
  return frame(new Uint8Array([0x01, 0x10, ...u16be(register), ...u16be(values.length), payload.length, ...payload]));
}

export function echo(payload: Uint8Array): Uint8Array {
  return frame(concatBytes(new Uint8Array([0x01, 0x7f]), payload));
}

export function ensureSafeBleLength(request: Uint8Array): void {
  const limit = BMSGeneratedBLEConstraints.SAFE_SINGLE_REQUEST_PAYLOAD_BYTES;
  if (request.length > limit) throw new BmsProtocolError(`请求长度 ${request.length} byte，超过当前固件 BLE 单包安全上限 ${limit} byte`);
}

export function validateCrc(data: Uint8Array): boolean {
  if (data.length < 4) return false;
  const body = data.slice(0, data.length - 2);
  const crc = crc16(body);
  return data[data.length - 2] === (crc & 0xff) && data[data.length - 1] === ((crc >> 8) & 0xff);
}

export function inferExpectedLength(buffer: Uint8Array, hint?: number): number | undefined {
  if (hint !== undefined) return hint;
  if (buffer.length < 2) return undefined;
  const func = buffer[1];
  if (func === 0x7f) return undefined;
  if ((func & 0x80) !== 0) return 5;
  if (func === 0x03) return buffer.length >= 3 ? buffer[2] + 5 : undefined;
  if (func === 0x06 || func === 0x10) return 8;
  return undefined;
}

export function parseResponse(data: Uint8Array): ParsedResponse {
  if (data.length < 4) throw new BmsProtocolError('Modbus 帧长度不足');
  if (!validateCrc(data)) throw new BmsProtocolError('Modbus CRC 校验失败');
  const func = data[1];
  if (func === 0x7f) return { kind: 'echo', raw: data };
  if ((func & 0x80) !== 0) {
    if (data.length !== 5) throw new BmsProtocolError('Modbus 异常响应长度错误');
    return { kind: 'exception', function: func & 0x7f, code: data[2], raw: data };
  }
  if (func === 0x03) {
    const byteCount = data[2];
    if (data.length !== byteCount + 5 || (byteCount % 2) !== 0) throw new BmsProtocolError('0x03 响应长度或 byteCount 错误');
    const words: number[] = [];
    for (let i = 3; i < 3 + byteCount; i += 2) words.push(u16frombe(data, i));
    return { kind: 'read_holding', words, raw: data };
  }
  if (func === 0x06) {
    if (data.length !== 8) throw new BmsProtocolError('0x06 响应长度应为 8');
    return { kind: 'write_single_ack', register: u16frombe(data, 2), value: u16frombe(data, 4), raw: data };
  }
  if (func === 0x10) {
    if (data.length !== 8) throw new BmsProtocolError('0x10 响应长度应为 8');
    return { kind: 'write_multiple_ack', register: u16frombe(data, 2), quantity: u16frombe(data, 4), raw: data };
  }
  throw new BmsProtocolError(`不支持的响应功能码 0x${func.toString(16).padStart(2, '0')}`);
}

export class ResponseAccumulator {
  private buffer = new Uint8Array(0);
  private expectedLengthHint?: number;
  reset(expectedLengthHint?: number): void { this.buffer = new Uint8Array(0); this.expectedLengthHint = expectedLengthHint; }
  append(fragment: Uint8Array): Uint8Array | undefined {
    this.buffer = concatBytes(this.buffer, fragment);
    const expected = inferExpectedLength(this.buffer, this.expectedLengthHint);
    if (expected === undefined || this.buffer.length < expected) return undefined;
    if (this.buffer.length !== expected) throw new BmsProtocolError(`响应长度 ${this.buffer.length} 与期望 ${expected} 不一致`);
    const completed = this.buffer;
    if (!validateCrc(completed)) throw new BmsProtocolError('完整响应 CRC 校验失败');
    this.reset();
    return completed;
  }
}

export function asciiStringFromWords(words: number[]): string {
  const bytes: number[] = [];
  words.forEach((word) => bytes.push((word >> 8) & 0xff, word & 0xff));
  const end = bytes.indexOf(0);
  return String.fromCharCode(...(end >= 0 ? bytes.slice(0, end) : bytes));
}

export function macStringFromWords(words: number[]): string {
  const bytes: number[] = [];
  words.forEach((word) => bytes.push((word >> 8) & 0xff, word & 0xff));
  return bytes.slice(0, 6).map((item) => item.toString(16).toUpperCase().padStart(2, '0')).join(':');
}

export function encodeAsciiWords(text: string): number[] {
  const bytes = Array.from(text).map((ch) => ch.charCodeAt(0));
  if (bytes.some((item) => item > 0x7f)) throw new BmsProtocolError('当前蓝牙名只允许 ASCII 字符');
  if ((bytes.length % 2) !== 0) bytes.push(0);
  const words: number[] = [];
  for (let i = 0; i < bytes.length; i += 2) words.push((bytes[i] << 8) | bytes[i + 1]);
  return words;
}

export function decodeBatteryStatus(realtimeWords: number[], legacyWords: number[], systemStatusWords: number[]): BatteryStatusSnapshot {
  const statusLow = systemStatusWords[0] ?? 0;
  const statusHigh = systemStatusWords[1] ?? 0;
  const systemStatusRaw = ((statusHigh << 16) | statusLow) >>> 0;
  const activeStatusFlags = BMSSystemStatusBits.filter((item) => (systemStatusRaw & (1 << item.bit)) !== 0).map((item) => item.key);
  const cellVoltages = legacyWords.slice(0, 10);
  const realtimeValid = realtimeWords.length >= BMSGeneratedRegisterCatalog.REALTIME_STATUS_WORD_COUNT && realtimeWords[0] === BMSGeneratedRegisterCatalog.REALTIME_STATUS_MAGIC;
  const charge = legacyWords[50] ?? 0;
  const discharge = legacyWords[51] ?? 0;
  const legacyCurrent = discharge !== 0 ? -signed16(discharge) / 10.0 : (charge !== 0 ? signed16(charge) / 10.0 : 0);
  if (realtimeValid) {
    return { source:'realtime_window', supportsRealtimeWindow:true, protocolVersion:realtimeWords[1]??0, packVoltage:(realtimeWords[2]??0)/100.0, signedCurrent:signed16(realtimeWords[3]??0)/10.0, soc:realtimeWords[4]??0, maxTemp:decodeTemp(realtimeWords[5]??0), minTemp:decodeTemp(realtimeWords[6]??0), mosTemp:decodeTemp(realtimeWords[7]??0), maxCellVoltage:realtimeWords[8]??0, minCellVoltage:realtimeWords[9]??0, cellDelta:realtimeWords[10]??0, maxCellPosition:legacyWords[34]??0, minCellPosition:legacyWords[35]??0, soh:legacyWords[53]??0, capacityNow:(legacyWords[54]??0)/100.0, capacityFull:(legacyWords[55]??0)/100.0, capacityFactory:(legacyWords[56]??0)/100.0, cycleCount:legacyWords[57]??0, cellVoltages, systemStatusRaw, activeStatusFlags };
  }
  return { source:'legacy_registers', supportsRealtimeWindow:false, protocolVersion:0, packVoltage:(legacyWords[37]??0)/100.0, signedCurrent:legacyCurrent, soc:legacyWords[52]??0, soh:legacyWords[53]??0, maxTemp:decodeTemp(legacyWords[48]??0), minTemp:decodeTemp(legacyWords[49]??0), mosTemp:decodeTemp(legacyWords[47]??0), maxCellVoltage:legacyWords[32]??0, minCellVoltage:legacyWords[33]??0, cellDelta:legacyWords[36]??0, maxCellPosition:legacyWords[34]??0, minCellPosition:legacyWords[35]??0, capacityNow:(legacyWords[54]??0)/100.0, capacityFull:(legacyWords[55]??0)/100.0, capacityFactory:(legacyWords[56]??0)/100.0, cycleCount:legacyWords[57]??0, cellVoltages, systemStatusRaw, activeStatusFlags };
}

export function spacedHex(data: Uint8Array): string { return Array.from(data).map((item) => item.toString(16).toUpperCase().padStart(2, '0')).join(' '); }
function signed16(value: number): number { const raw=value&0xffff; return (raw&0x8000)!==0 ? raw-0x10000 : raw; }
function decodeTemp(raw: number): number { return raw/10.0-40.0; }
function u16be(value: number): number[] { return [(value>>8)&0xff,value&0xff]; }
function u16frombe(data: Uint8Array, offset: number): number { return ((data[offset]<<8)|data[offset+1])&0xffff; }
export function concatBytes(a: Uint8Array,b: Uint8Array): Uint8Array { const out=new Uint8Array(a.length+b.length); out.set(a,0); out.set(b,a.length); return out; }
