import {
  OTA_CHARACTERISTIC_UUID,
  OTA_SERVICE_UUID,
  TelinkFirmwareImage,
  endPacket,
  firmwareDataPacket,
  parseResult,
  resultText,
  startPacket,
} from './ota'

declare const wx: any

export interface OtaDiscoveredDevice {
  deviceId: string
  name: string
  localName: string
  rssi: number
}

export type OtaStatus =
  | 'idle'
  | 'scanning'
  | 'connecting'
  | 'ready'
  | 'transferring'
  | 'waiting_result'
  | 'success'
  | 'failed'
  | 'disconnected'

export class TelinkOtaTransport {
  onStatus?: (status: OtaStatus, note: string) => void
  onDevice?: (device: OtaDiscoveredDevice) => void
  onProgress?: (percent: number, sent: number, total: number) => void
  onLog?: (message: string) => void

  private initialized = false
  private deviceId = ''
  private serviceId = ''
  private characteristicId = ''
  private running = false
  private resultResolve?: (code: number) => void
  private resultReject?: (error: Error) => void
  private resultTimer?: ReturnType<typeof setTimeout>

  async initialize(): Promise<void> {
    if (this.initialized) return
    await callWx('openBluetoothAdapter')
    wx.onBluetoothDeviceFound((event: any) => this.handleDeviceFound(event))
    wx.onBLECharacteristicValueChange((event: any) => this.handleNotify(event))
    wx.onBLEConnectionStateChange((event: any) => {
      if (event.deviceId === this.deviceId && !event.connected) {
        const wasRunning = this.running
        this.clearConnection()
        if (wasRunning) this.rejectResult(new Error('OTA 过程中 BLE 连接断开'))
        this.onStatus?.('disconnected', wasRunning ? 'OTA 过程中连接断开' : 'BLE 连接已断开')
      }
    })
    this.initialized = true
    this.onStatus?.('idle', 'OTA 蓝牙适配器已初始化')
  }

  async startScan(): Promise<void> {
    await this.initialize()
    await callWx('startBluetoothDevicesDiscovery', { allowDuplicatesKey: true, interval: 600 })
    this.onStatus?.('scanning', '正在扫描 BMS')
  }

  async stopScan(): Promise<void> {
    try { await callWx('stopBluetoothDevicesDiscovery') } catch (_) { /* ignore */ }
  }

  async connect(deviceId: string): Promise<void> {
    if (this.running) throw new Error('OTA 进行中，禁止重新连接')
    await this.initialize()
    await this.stopScan()
    this.deviceId = deviceId
    this.serviceId = ''
    this.characteristicId = ''
    this.onStatus?.('connecting', '正在连接 BMS OTA')
    await callWx('createBLEConnection', { deviceId, timeout: 10000 })

    const servicesResult = await callWx('getBLEDeviceServices', { deviceId })
    const service = (servicesResult.services ?? []).find((item: any) => uuidEqual(item.uuid, OTA_SERVICE_UUID))
    if (!service) throw new Error(`未发现 OTA Service ${OTA_SERVICE_UUID}`)
    this.serviceId = service.uuid

    const charsResult = await callWx('getBLEDeviceCharacteristics', { deviceId, serviceId: this.serviceId })
    const characteristic = (charsResult.characteristics ?? []).find((item: any) => uuidEqual(item.uuid, OTA_CHARACTERISTIC_UUID))
    if (!characteristic) throw new Error(`未发现 OTA Characteristic ${OTA_CHARACTERISTIC_UUID}`)
    this.characteristicId = characteristic.uuid

    try {
      await callWx('notifyBLECharacteristicValueChange', {
        state: true,
        deviceId,
        serviceId: this.serviceId,
        characteristicId: this.characteristicId,
      })
    } catch (error) {
      this.onLog?.(`OTA Notify 订阅失败，将继续尝试升级：${asError(error).message}`)
    }
    this.onStatus?.('ready', 'OTA characteristic READY')
  }

  async disconnect(): Promise<void> {
    if (this.running) throw new Error('OTA 进行中，禁止主动断开')
    if (!this.deviceId) return
    const id = this.deviceId
    try { await callWx('closeBLEConnection', { deviceId: id }) }
    finally {
      this.clearConnection()
      this.onStatus?.('disconnected', '已断开连接')
    }
  }

  async run(image: TelinkFirmwareImage): Promise<void> {
    if (this.running) throw new Error('已有 OTA session 正在运行')
    if (!this.deviceId || !this.serviceId || !this.characteristicId) throw new Error('OTA 通道尚未 READY')

    this.running = true
    this.onStatus?.('transferring', `OTA START，firmware=${image.declaredSize} bytes`)
    this.onProgress?.(0, 0, image.packetCount)
    try {
      await this.write(startPacket())
      for (let index = 0; index < image.packetCount; index += 1) {
        await this.write(firmwareDataPacket(image, index))
        const sent = index + 1
        const percent = Math.min(100, Math.floor((sent * 100) / image.packetCount))
        this.onProgress?.(percent, sent, image.packetCount)
        if (sent % 128 === 0 || sent === image.packetCount) {
          this.onLog?.(`OTA data ${sent}/${image.packetCount} (${percent}%)`)
        }
      }
      await this.write(endPacket(image.maxIndex))
      this.onStatus?.('waiting_result', 'OTA END 已发送，等待 OTA_RESULT')
      const result = await this.waitResult(20000)
      if (result !== 0) throw new Error(`${resultText(result)} (0x${result.toString(16).toUpperCase().padStart(2, '0')})`)
      this.onStatus?.('success', 'OTA_SUCCESS，BMS 应自动重启')
      this.onProgress?.(100, image.packetCount, image.packetCount)
    } catch (error) {
      const err = asError(error)
      this.onStatus?.('failed', err.message)
      throw err
    } finally {
      this.running = false
      this.clearResultWaiter()
    }
  }

  private async write(packet: Uint8Array): Promise<void> {
    if (packet.length > 20) throw new Error(`OTA packet 超过 20 byte: ${packet.length}`)
    this.onLog?.(`TX ${hex(packet)}`)
    await callWx('writeBLECharacteristicValue', {
      deviceId: this.deviceId,
      serviceId: this.serviceId,
      characteristicId: this.characteristicId,
      value: toArrayBuffer(packet),
      writeType: 'write',
    })
  }

  private waitResult(timeoutMs: number): Promise<number> {
    if (this.resultResolve) return Promise.reject(new Error('已有 OTA_RESULT waiter'))
    return new Promise((resolve, reject) => {
      this.resultResolve = resolve
      this.resultReject = reject
      this.resultTimer = setTimeout(() => this.rejectResult(new Error(`等待 OTA_RESULT 超时 (${timeoutMs} ms)`)), timeoutMs)
    })
  }

  private handleNotify(event: any): void {
    if (!this.deviceId || event.deviceId !== this.deviceId || !uuidEqual(event.characteristicId, this.characteristicId)) return
    const data = new Uint8Array(event.value as ArrayBuffer)
    this.onLog?.(`RX ${hex(data)}`)
    const result = parseResult(data)
    if (result === undefined || !this.resultResolve) return
    const resolve = this.resultResolve
    this.clearResultWaiter()
    resolve(result)
  }

  private handleDeviceFound(event: any): void {
    for (const device of event.devices ?? []) {
      const localName = device.localName || ''
      const name = localName || device.name || device.deviceId
      const services = (device.advertisServiceUUIDs ?? []).map((item: string) => item.toUpperCase())
      const likely = name.toUpperCase().startsWith('BT_') || services.some((item: string) => item.includes('180F') || item.includes('1812'))
      if (!likely) continue
      this.onDevice?.({
        deviceId: device.deviceId,
        name,
        localName,
        rssi: Number(device.RSSI ?? -127),
      })
    }
  }

  private rejectResult(error: Error): void {
    const reject = this.resultReject
    this.clearResultWaiter()
    reject?.(error)
  }

  private clearResultWaiter(): void {
    if (this.resultTimer) clearTimeout(this.resultTimer)
    this.resultTimer = undefined
    this.resultResolve = undefined
    this.resultReject = undefined
  }

  private clearConnection(): void {
    this.deviceId = ''
    this.serviceId = ''
    this.characteristicId = ''
  }
}

function uuidEqual(a: string, b: string): boolean {
  return String(a).toUpperCase() === String(b).toUpperCase()
}

function toArrayBuffer(data: Uint8Array): ArrayBuffer {
  return data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength) as ArrayBuffer
}

function hex(data: Uint8Array): string {
  return Array.from(data).map(value => value.toString(16).toUpperCase().padStart(2, '0')).join(' ')
}

function callWx(method: string, args: Record<string, unknown> = {}): Promise<any> {
  return new Promise((resolve, reject) => {
    const fn = wx[method]
    if (typeof fn !== 'function') {
      reject(new Error(`当前微信环境不支持 wx.${method}`))
      return
    }
    fn({ ...args, success: resolve, fail: (error: any) => reject(asError(error)) })
  })
}

function asError(value: unknown): Error {
  if (value instanceof Error) return value
  const candidate = value as { errMsg?: string } | undefined
  return new Error(candidate?.errMsg || String(value))
}
