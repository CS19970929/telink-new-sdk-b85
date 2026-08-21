import { OtaDiscoveredDevice, TelinkOtaTransport } from '../../core/otaTransport'
import { TelinkFirmwareImage, parseFirmware } from '../../core/ota'

declare const wx: any
declare const Page: any

const transport = new TelinkOtaTransport()
const deviceMap = new Map<string, OtaDiscoveredDevice>()

Page({
  data: {
    status: 'idle',
    note: '等待操作',
    devices: [] as OtaDiscoveredDevice[],
    selectedDeviceId: '',
    connectedName: '',
    firmwareName: '未选择 firmware.bin',
    firmwareDetail: '',
    firmware: null as TelinkFirmwareImage | null,
    progress: 0,
    progressText: '0%',
    busy: false,
    ready: false,
    logs: [] as string[],
  },

  async onLoad() {
    transport.onStatus = (status, note) => {
      this.setData({ status, note, ready: status === 'ready' || status === 'success' })
      this.addLog(`${status}: ${note}`)
    }
    transport.onDevice = (device) => {
      deviceMap.set(device.deviceId, device)
      this.setData({ devices: Array.from(deviceMap.values()).sort((a, b) => b.rssi - a.rssi) })
    }
    transport.onProgress = (percent, sent, total) => {
      this.setData({ progress: percent, progressText: `${percent}% (${sent}/${total})` })
    }
    transport.onLog = (message) => this.addLog(message)
    try {
      await transport.initialize()
    } catch (error) {
      this.showError(error)
    }
  },

  async onUnload() {
    if (!this.data.busy) {
      try { await transport.disconnect() } catch (_) { /* ignore */ }
    }
  },

  async startScan() {
    if (this.data.busy) return
    deviceMap.clear()
    this.setData({ devices: [], selectedDeviceId: '', connectedName: '', ready: false })
    try { await transport.startScan() } catch (error) { this.showError(error) }
  },

  async stopScan() {
    try { await transport.stopScan() } catch (error) { this.showError(error) }
  },

  selectDevice(event: any) {
    if (this.data.busy) return
    this.setData({ selectedDeviceId: event.currentTarget.dataset.deviceId })
  },

  async connectSelected() {
    if (this.data.busy || !this.data.selectedDeviceId) return
    const device = deviceMap.get(this.data.selectedDeviceId)
    try {
      await transport.connect(this.data.selectedDeviceId)
      this.setData({ connectedName: device?.localName || device?.name || this.data.selectedDeviceId, ready: true })
    } catch (error) {
      this.setData({ ready: false })
      this.showError(error)
    }
  },

  chooseFirmware() {
    if (this.data.busy) return
    wx.chooseMessageFile({
      count: 1,
      type: 'file',
      extension: ['bin'],
      success: (result: any) => {
        try {
          const file = result.tempFiles?.[0]
          if (!file?.path) throw new Error('未获得固件临时文件路径')
          const raw = wx.getFileSystemManager().readFileSync(file.path) as ArrayBuffer
          const image = parseFirmware(new Uint8Array(raw))
          this.setData({
            firmware: image,
            firmwareName: file.name || 'firmware.bin',
            firmwareDetail: `${image.declaredSize} bytes · ${image.packetCount} packets`,
            note: '固件校验通过',
          })
          this.addLog(`firmware loaded: ${file.name || file.path}`)
        } catch (error) {
          this.setData({ firmware: null, firmwareName: '固件无效', firmwareDetail: '' })
          this.showError(error)
        }
      },
      fail: (error: any) => {
        const message = error?.errMsg || String(error)
        if (!message.includes('cancel')) this.showError(new Error(message))
      },
    })
  },

  confirmStart() {
    if (!this.data.ready || !this.data.firmware || this.data.busy) return
    wx.showModal({
      title: '确认 OTA',
      content: '升级期间禁止断电。当前 BMS BLE SMP 未启用，请确认连接的是目标 BMS。',
      confirmText: '开始升级',
      confirmColor: '#d92d20',
      success: (result: any) => { if (result.confirm) void this.startOta() },
    })
  },

  async startOta() {
    const firmware = this.data.firmware as TelinkFirmwareImage | null
    if (!firmware || !this.data.ready || this.data.busy) return
    this.setData({ busy: true, progress: 0, progressText: `0% (0/${firmware.packetCount})` })
    try {
      await delay(120)
      await transport.run(firmware)
      wx.showModal({
        title: 'OTA 成功',
        content: 'BMS 已通过 OTA_RESULT 校验。设备应自动重启，请重新扫描并确认软件版本。',
        showCancel: false,
      })
    } catch (error) {
      this.showError(error)
    } finally {
      this.setData({ busy: false })
    }
  },

  addLog(message: string) {
    const line = `${new Date().toLocaleTimeString()} ${message}`
    this.setData({ logs: [line, ...this.data.logs].slice(0, 120) })
  },

  showError(error: unknown) {
    const message = error instanceof Error ? error.message : String(error)
    this.addLog(`ERROR: ${message}`)
    wx.showModal({ title: 'BMS OTA 错误', content: message, showCancel: false })
  },
})

function delay(ms: number): Promise<void> {
  return new Promise(resolve => setTimeout(resolve, ms))
}
