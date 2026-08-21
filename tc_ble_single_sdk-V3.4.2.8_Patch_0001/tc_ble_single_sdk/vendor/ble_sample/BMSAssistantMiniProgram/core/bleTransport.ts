import { BMSGeneratedUUIDs } from '../generated/BMSGeneratedRegisterCatalog';
import { BmsProtocolError, ensureSafeBleLength, ResponseAccumulator, spacedHex } from './protocol';

declare const wx: any;

export interface BmsDiscoveredDevice { deviceId:string; name:string; localName:string; rssi:number; advertisServiceUUIDs:string[]; }
export type TransportStatus = 'idle'|'scanning'|'connecting'|'connected'|'ready'|'disconnected'|'failed';
interface PendingRequest { resolve:(data:Uint8Array)=>void; reject:(error:Error)=>void; timer:ReturnType<typeof setTimeout>; }

export class BmsBleTransport {
  onStatus?: (status:TransportStatus,note:string)=>void;
  onDevice?: (device:BmsDiscoveredDevice)=>void;
  onLog?: (direction:'TX'|'RX'|'INFO'|'ERR',message:string,payloadHex?:string)=>void;
  private initialized=false;
  private deviceId=''; private serviceId=''; private requestCharacteristicId=''; private responseCharacteristicId='';
  private pending?:PendingRequest;
  private accumulator=new ResponseAccumulator();

  async initialize():Promise<void>{
    if(this.initialized)return;
    await callWx('openBluetoothAdapter');
    wx.onBluetoothDeviceFound((event:any)=>this.handleDeviceFound(event));
    wx.onBLECharacteristicValueChange((event:any)=>this.handleCharacteristicValue(event));
    wx.onBLEConnectionStateChange((event:any)=>{ if(event.deviceId===this.deviceId&&!event.connected)this.handleDisconnect('BLE 连接断开'); });
    this.initialized=true; this.onStatus?.('idle','蓝牙适配器已初始化');
  }
  async startScan():Promise<void>{ await this.initialize(); await callWx('startBluetoothDevicesDiscovery',{allowDuplicatesKey:true,interval:600}); this.onStatus?.('scanning','正在扫描 BMS'); }
  async stopScan():Promise<void>{ try{await callWx('stopBluetoothDevicesDiscovery');}catch(_){ } }
  async connect(deviceId:string):Promise<void>{
    await this.initialize(); await this.stopScan(); this.cleanupPending(new BmsProtocolError('开始新的 BLE 连接')); this.deviceId=deviceId; this.onStatus?.('connecting','正在连接设备');
    await callWx('createBLEConnection',{deviceId,timeout:8000}); this.onStatus?.('connected','连接完成，正在发现服务');
    const servicesResult=await callWx('getBLEDeviceServices',{deviceId});
    const service=(servicesResult.services??[]).find((item:any)=>uuidEqual(item.uuid,BMSGeneratedUUIDs.SERVICE_UUID));
    if(!service)throw new BmsProtocolError(`未发现 BMS SPP Service ${BMSGeneratedUUIDs.SERVICE_UUID}`); this.serviceId=service.uuid;
    const charsResult=await callWx('getBLEDeviceCharacteristics',{deviceId,serviceId:this.serviceId}); const characteristics=charsResult.characteristics??[];
    const request=characteristics.find((item:any)=>uuidEqual(item.uuid,BMSGeneratedUUIDs.REQUEST_CHARACTERISTIC_UUID));
    const response=characteristics.find((item:any)=>uuidEqual(item.uuid,BMSGeneratedUUIDs.RESPONSE_CHARACTERISTIC_UUID));
    if(!request)throw new BmsProtocolError('未发现 BMS Request Characteristic'); if(!response)throw new BmsProtocolError('未发现 BMS Response Characteristic');
    this.requestCharacteristicId=request.uuid; this.responseCharacteristicId=response.uuid;
    await callWx('notifyBLECharacteristicValueChange',{state:true,type:'notification',deviceId,serviceId:this.serviceId,characteristicId:this.responseCharacteristicId});
    this.onStatus?.('ready','BMS BLE 通道 READY'); this.onLog?.('INFO','Notify 已订阅，业务通道可用');
  }
  async disconnect():Promise<void>{ if(!this.deviceId)return; const deviceId=this.deviceId; this.cleanupPending(new BmsProtocolError('主动断开连接')); try{await callWx('closeBLEConnection',{deviceId});}finally{this.clearConnection();this.onStatus?.('disconnected','已断开连接');} }
  async request(frame:Uint8Array,expectedLengthHint?:number,timeoutMs=3500):Promise<Uint8Array>{
    ensureSafeBleLength(frame); if(!this.deviceId||!this.serviceId||!this.requestCharacteristicId||!this.responseCharacteristicId)throw new BmsProtocolError('BLE 通道尚未 READY'); if(this.pending)throw new BmsProtocolError('当前仍有未完成请求，V1 协议禁止并发命令');
    this.accumulator.reset(expectedLengthHint); this.onLog?.('TX','Modbus Request',spacedHex(frame));
    const responsePromise=new Promise<Uint8Array>((resolve,reject)=>{ const timer=setTimeout(()=>{this.pending=undefined;this.accumulator.reset();reject(new BmsProtocolError(`等待 BMS 响应超时 (${timeoutMs} ms)`));},timeoutMs); this.pending={resolve,reject,timer}; });
    try{ await callWx('writeBLECharacteristicValue',{deviceId:this.deviceId,serviceId:this.serviceId,characteristicId:this.requestCharacteristicId,value:toArrayBuffer(frame),writeType:'write'}); }catch(error){this.cleanupPending(asError(error));throw error;}
    return responsePromise;
  }
  private handleDeviceFound(event:any):void{ (event.devices??[]).forEach((device:any)=>{ const name=device.localName||device.name||''; const services=(device.advertisServiceUUIDs??[]).map((item:string)=>item.toUpperCase()); const likely=name.toUpperCase().startsWith('BT_')||services.some((item:string)=>item.includes('180F')||item.includes('1812')); if(!likely)return; this.onDevice?.({deviceId:device.deviceId,name:device.name||name||device.deviceId,localName:device.localName||'',rssi:Number(device.RSSI??-127),advertisServiceUUIDs:services}); }); }
  private handleCharacteristicValue(event:any):void{ if(!this.pending||event.deviceId!==this.deviceId||!uuidEqual(event.characteristicId,this.responseCharacteristicId))return; try{ const fragment=new Uint8Array(event.value as ArrayBuffer); this.onLog?.('RX','Notify Fragment',spacedHex(fragment)); const complete=this.accumulator.append(fragment); if(!complete)return; const pending=this.pending; this.pending=undefined;clearTimeout(pending.timer);this.onLog?.('RX','Modbus Response',spacedHex(complete));pending.resolve(complete);}catch(error){const err=asError(error);this.onLog?.('ERR',err.message);this.cleanupPending(err);} }
  private handleDisconnect(note:string):void{this.cleanupPending(new BmsProtocolError(note));this.clearConnection();this.onStatus?.('disconnected',note);}
  private cleanupPending(error:Error):void{if(!this.pending){this.accumulator.reset();return;}const pending=this.pending;this.pending=undefined;clearTimeout(pending.timer);this.accumulator.reset();pending.reject(error);}
  private clearConnection():void{this.deviceId='';this.serviceId='';this.requestCharacteristicId='';this.responseCharacteristicId='';}
}
function uuidEqual(a:string,b:string):boolean{return String(a).toUpperCase()===String(b).toUpperCase();}
function toArrayBuffer(data:Uint8Array):ArrayBuffer{return data.buffer.slice(data.byteOffset,data.byteOffset+data.byteLength) as ArrayBuffer;}
function callWx(method:string,args:Record<string,unknown>={}):Promise<any>{return new Promise((resolve,reject)=>{const fn=wx[method];if(typeof fn!=='function'){reject(new Error(`当前微信环境不支持 wx.${method}`));return;}fn({...args,success:resolve,fail:(error:any)=>reject(asError(error))});});}
function asError(value:unknown):Error{if(value instanceof Error)return value;const candidate=value as {errMsg?:string}|undefined;return new Error(candidate?.errMsg||String(value));}
