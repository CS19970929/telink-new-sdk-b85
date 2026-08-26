import { BmsBleTransport,BmsDiscoveredDevice } from '../../core/bleTransport';
import { BmsService } from '../../core/bmsService';
import { BatteryStatusSnapshot } from '../../core/protocol';
declare const wx:any;declare const Page:any;
const transport=new BmsBleTransport();const service=new BmsService(transport);const deviceMap=new Map<string,BmsDiscoveredDevice>();
Page({
 data:{status:'idle',note:'未初始化',devices:[] as BmsDiscoveredDevice[],connectedName:'',busy:false,identity:null as any,battery:null as BatteryStatusSnapshot|null,socInput:'60',nameSuffixInput:'DEMO',logs:[] as string[]},
 async onLoad(){transport.onStatus=(status,note)=>this.setData({status,note});transport.onDevice=(device)=>{deviceMap.set(device.deviceId,device);this.setData({devices:Array.from(deviceMap.values()).sort((a,b)=>b.rssi-a.rssi)});};transport.onLog=(direction,message,payloadHex)=>{const line=`${new Date().toLocaleTimeString()} ${direction} ${message}${payloadHex?` | ${payloadHex}`:''}`;this.setData({logs:[line,...this.data.logs].slice(0,80)});};try{await transport.initialize();}catch(error){this.showError(error);}},
 async startScan(){deviceMap.clear();this.setData({devices:[]});try{await transport.startScan();}catch(error){this.showError(error);}},
 async connectDevice(event:any){const deviceId=event.currentTarget.dataset.deviceId;const device=deviceMap.get(deviceId);await this.runBusy(async()=>{await transport.connect(deviceId);this.setData({connectedName:device?.localName||device?.name||deviceId});const identity=await service.refreshIdentity();const battery=await service.refreshBatteryStatus();this.setData({identity,battery});});},
 async disconnect(){try{await transport.disconnect();this.setData({connectedName:'',identity:null,battery:null});}catch(error){this.showError(error);}},
 async refreshStatus(){await this.runBusy(async()=>this.setData({battery:await service.refreshBatteryStatus()}));},
 async refreshIdentity(){await this.runBusy(async()=>this.setData({identity:await service.refreshIdentity()}));},
 async echoTest(){await this.runBusy(async()=>{await service.echoTest();wx.showToast({title:'Echo OK',icon:'success'});});},
 onSocInput(event:any){this.setData({socInput:event.detail.value});},
 async writeSoc(){await this.runBusy(async()=>{await service.writeSoc(Number(this.data.socInput));wx.showToast({title:'SOC 已写入',icon:'success'});this.setData({battery:await service.refreshBatteryStatus()});});},
 onNameSuffixInput(event:any){this.setData({nameSuffixInput:event.detail.value});},
 async writeNameSuffix(){await this.runBusy(async()=>{await service.writeBluetoothNameSuffix(this.data.nameSuffixInput);wx.showToast({title:'名称已写入',icon:'success'});});},
 async openOta(){try{if(this.data.status==='ready')await transport.disconnect();wx.navigateTo({url:'/pages/ota/ota'});}catch(error){this.showError(error);}},
 async runBusy(task:()=>Promise<void>){if(this.data.busy)return;this.setData({busy:true});try{await task();}catch(error){this.showError(error);}finally{this.setData({busy:false});}},
 showError(error:unknown){const message=error instanceof Error?error.message:String(error);transport.onLog?.('ERR',message);wx.showModal({title:'BMS 通信错误',content:message,showCancel:false});}
});
