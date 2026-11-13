# Android C060 适配与通用升级实施记录

日期：2026-11-06。原分支分阶段实施，所有协议复用BmsTool.Core；本轮不改BMS固件、不生成固件镜像。

## 阶段1：连接与参数

已接入C060常规参数自动识别和旧项目手动核对，复用桌面常规字段定义、单位、范围、整组候选验证、ACK与回读。安卓不提供K/B维护，也不读取或导出校准数据；客户版保留服务设置门禁，工厂字段不可编辑。兼容字段须明确启用。保护与常规修改分别确认保存，不承诺跨组回滚。

JSON导出设备读回值，导入只载入候选；默认不导入身份、工厂校准或兼容字段，保护候选在保护页另行保存。连接变化会清除参数所有权，防止旧设备候选发送到新设备。首次读取先确认身份，连接失败清理GATT资源，可选能力缺失不关闭基础通信。

验证：共享候选模型572项检查通过；Android内部版C#编译0错误/0警告。客户分支编译同为0错误/0警告，Windows客户版、内部版及CLI构建均通过。当前未连接ADB手机，也无可用AVD；不把编译或host模型称为手机/BMS实测。

## 阶段2：通用YMODEM蓝牙升级

已接入两版独立“设备 → 固件升级”页、文件选择/分享/收件箱、包信息、进度与取消。文件内容经公共 `FirmwareImage.LoadForBluetoothUpgrade` 分流：Telink 继续严格校验 TLNK/size/CRC trailer，C060、D015和未来同合同项目使用 `SerialFirmwarePackage`、`UpgradeDetection`、`SerialYmodemClient`。不按扩展名或板型白名单决定是否升级。

升级前本地校验包头/载荷CRC；连接明确MAC后核对设备能力、板号、APP地址与容量。APP损坏时允许直接识别Boot，不要求普通业务probe通过。20字节/15ms分片复用公共YMODEM状态机；成功须回读已确认的版本、长度和CRC。业务视图暂未恢复不推翻已确认升级；额外SN核对失败明确提示。取消或最终提交结果不明不显示成功。

旧连接的GATT回调被过滤，重连重新协商MTU。连接MAC与升级目标不同则拒绝；不会扫描后自动选第一台。原VS Code自动Telink流程保留一次性导入授权与原连接约束；YMODEM导入后须在手机手动确认。

验证：固件分流/目标会话策略22项通过（含未登记项目板号与后缀、坏CRC、错误MAC/无原连接拒绝）；Telink OTA核心回归通过；C060/D015生产C接收器host模型分别接受179/185次BLE写入，最大20字节，最终元数据一致。两版Android C#编译0错误/0警告；Windows客户/内部/CLI构建通过（客户版保留既有 `_socRecording` 未赋值警告）。这些是host与编译证据，尚无手机/无线链路验收。

## 阶段3：MAC二维码连接

已按用户确认的蓝牙MAC二维码实现。入口为“设备 → 连接其他电池 → 扫码连接（蓝牙MAC）”。识别后自动填写标准MAC并复用现有连接入口；切换设备前清理原会话与参数候选，不会按蓝牙名称猜测设备。

支持 `EC:F6:4C:E9:F1:3D`、`EC-F6-4C-E9-F1-3D`、`ECF64CE9F13D`，大小写和首尾空白可接受。全零/全FF、混合分隔符、URL、JSON、设备名称、超长和附加命令均拒绝。手动连接也使用同一规范化。

扫码为独立、不可被外部导出的Activity，识别在本机执行，不依赖Google服务/网络。仅点击扫码才申请相机权限；相机不是安装必需硬件。权限拒绝或相机不可用时保留手动MAC与蓝牙扫描。暂停或预览销毁即释放相机，旧帧结果不能触发连接；只解码一个私有亮度帧，预览缓冲立即归还。

本次使用缓存且固定版本 `ZXing.Net 0.16.9`（Apache-2.0）。原生 Camera API1 提供有界NV21预览，未引入第二套UI框架；该API已被Android标为deprecated，仍在当前SDK中保留，后续真机覆盖需包括新系统。调用与缓冲复用依据官方 [Camera文档](https://developer.android.com/reference/android/hardware/Camera)，解码依据 [ZXing.Net](https://github.com/micjahn/ZXing.Net)。

同时修正连接后首屏部分寄存器不支持/超时不撤销已通过的基础probe；可选MOS窗口异常不阻断已读实时数据。真实GATT/传输失败仍明确显示失败。

验证：MAC/二维码亮度解码52项通过，包含四个旋转方向及非法标签；两版Android C#编译0错误/0警告。无连接手机，权限弹窗、预览、对焦、后台恢复及实际扫码连接尚待真机验收。

## 阶段4：APK交付

已从干净源码 `b35e259cf5bd` 完整编译两版APK，均0警告/0错误，版本0.9.0（versionCode 15）。Android API23及以上，包内架构为arm64-v8a与x86_64；不包含32位ARM。两版包名不同，可共存。

正式交付目录为 `BmsTool.Android/publish/20261106-v0.9.0-b35e259c/`。APK不提交Git，源码、文档和校验清单单独留档。全部中间obj、日志与预览均位于用户TEMP/CodexTemp/bms-tool-windows/android-c060-20261106。

| 版别 | APK | 应用ID | 大小 |
| --- | --- | --- | --- |
| Customer | BMS-Battery-0.9.0-Customer.apk | com.cs.bmsbattery | 11.87 MiB |
| Internal | BMS-Tool-0.9.0-Internal.apk | com.cs.bmstool.android | 11.88 MiB |

使用本机已有的Android Debug证书签名，v1/v2/v3校验均通过；Release构建不等于已配置正式商店发布密钥。安装覆盖旧版本时需要同一证书。证书SHA256为 `de7e98795ad8d75a88b6b0e96eb68196d043720f58447af41b399632522704fd`。各APK SHA256与源码摘要见同目录 `release-manifest.json`、`SHA256SUMS.txt` 及 `ANDROID_C060_EVIDENCE.json`。

客户版：实时数据、扫码、通用固件升级可用；保护与常规参数保存须进入服务设置，工厂字段不可编辑。内部版保留诊断、测试和原始寄存器等工程入口；两版都不提供K/B。

## 手机验收步骤

1. 安装对应版别APK，打开蓝牙并允许附近设备权限；确认未被PC或另一手机占用。
2. 设备页展开“连接其他电池”，点击扫码；仅此次申请相机权限。扫描MAC标签，确认连接页显示对应MAC。拒绝权限、取消扫码、无相机和后台恢复均应能返回手动连接。
3. 核对C060身份、20串实时值和失效串位过滤；读取保护参数与全部常规参数，K/B入口与数据不应出现。
4. 客户版进入服务设置后，再选择已批准的小范围参数修改。核对确认列表、ACK、回读及断线后候选失效；整组写入不承诺跨组回滚。
5. 导出JSON，导入只改变候选；确认身份/工厂/兼容字段未自动套用。
6. 设备 → 固件升级导入 `C060_APP.c060`，核对MAC、板号、地址和CRC，再手动确认。成功应显示版本、长度、CRC；中断后可从Boot重新传输，结果未确认不能显示成功。
7. 换D015或未来同合同项目验证升级，换旧Telink项目验证原OTA_SUCCESS与身份回读。新升级合同无需每增加一个板号更新App；不同参数表仍须明确其读写合同。

## 通道与BMS固件边界

本轮没有修改BMS固件。当前C060已提供所需参数映射与YMODEM升级合同，App复用公共实现。后续D015/D016同方案新项目只要包头、能力窗口、ENTER命令与最终版本/长度/CRC回读合同一致，不需要逐板号更新App；新的参数布局仍应明确映射，不能自动猜测写地址。

普通Modbus参数保持整组原子帧，禁止借用YMODEM分片绕过固件组事务。MTU23支持4字/5字组；8字组要求MTU≥28，24字热冷组要求MTU≥60。App会协商MTU247，实际结果以手机连接页显示为准。指定蓝牙模块在之前Windows实板中协商247，不代表安卓已验收；不足时拒绝本次长组写入，但保留实时读取与连接。YMODEM独立使用20字节分片，可在MTU23通道传输。

不同项目不支持的AFE、诊断或其他可选窗口只影响对应页面，不可凭“未支持”推断BMS故障或自动发送其他项目参数。板号、地址、容量或包CRC不匹配时不得升级。

## 可复现构建与验证

在仓库根目录执行，选择 `Internal` 或 `Customer`；`$buildOut` 指向用户临时区，不把中间产物留在仓库。

```powershell
$buildOut=Join-Path $env:TEMP 'CodexTemp/bms-tool-windows/android-build'
dotnet build bms-tool-windows/BmsTool.Android/BmsTool.Android.csproj -c Release --artifacts-path $buildOut -p:BmsAppEdition=Customer -p:AndroidSdkDirectory=D:\telink\android_env\android-sdk -p:JavaSdkDirectory=D:\telink\android_env\jdk\jdk-17.0.19+10
.\bms-tool-windows\test-android-qr.ps1
.\bms-tool-windows\test-protection-batch.ps1
.\bms-tool-windows\test-c060-draft.ps1
.\bms-tool-windows\test-ota-protocol.ps1
```

本轮通过：572项共享候选校验、22项固件分流/目标策略、52项MAC/旋转二维码解析、保护13组/65字批处理、Telink OTA核心、C060/D015生产接收器MTU23字节流互通、两版APK完整编译与签名/manifest/源码身份核对。Windows客户/内部及CLI也构建通过；同源C060客户端契约回归再次通过，覆盖可选协议拒绝、通用设备保持通信、150字段、整组保存、保护13组与MTU预检。

当前ADB未连接设备、无可用模拟器；未执行安卓安装、相机扫码、实际BLE参数写入或安卓实板OTA。此前Windows对指定模块的升级证据只用于说明共享协议已有基线，不能代替本版App验收。手机验收按上节步骤执行，尤其覆盖相机权限、后台恢复、实际MTU、参数部分提交和中断恢复。

HTML已离线生成并在1440像素宽度渲染检查；包含目录、章节搜索、表格横向滚动和打印入口，无外部字体、脚本或网络请求。
