[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$')]
    [string]$Mac,

    [string]$Bin,
    [string]$FirmwareRoot,

    [ValidatePattern('^[A-Za-z0-9._-]{0,32}$')]
    [string]$ExpectedSerial = '',

    [ValidatePattern('^[A-Za-z0-9._:-]*$')]
    [string]$DeviceId = '',

    [switch]$InstallApp,
    [switch]$NoWait,

    [ValidateRange(30, 1200)]
    [int]$TimeoutSeconds = 480,

    [string]$EvidenceDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$package = 'com.cs.bmstool.android'
$scriptRoot = $PSScriptRoot
$appProject = Join-Path $scriptRoot 'BmsTool.Android\BmsTool.Android.csproj'
$apk = Join-Path $scriptRoot 'BmsTool.Android\bin\Release\net10.0-android\com.cs.bmstool.android-Signed.apk'

function Invoke-Adb {
    param([Parameter(Mandatory = $true)][string[]]$AdbArguments)
    $selector = if ($script:SelectedDevice) { @('-s', $script:SelectedDevice) } else { @() }
    & adb @selector @AdbArguments
    if ($LASTEXITCODE -ne 0) { throw "adb failed: adb $($AdbArguments -join ' ')" }
}

if (-not (Get-Command adb -ErrorAction SilentlyContinue)) {
    throw 'adb was not found. Install Android platform-tools and add adb to PATH.'
}

if ([string]::IsNullOrWhiteSpace($Bin) -eq [string]::IsNullOrWhiteSpace($FirmwareRoot)) {
    throw 'Specify exactly one of -Bin or -FirmwareRoot.'
}

if ($FirmwareRoot) {
    $firmwareRootPath = (Resolve-Path -LiteralPath $FirmwareRoot).Path
    $buildTool = Join-Path $firmwareRootPath 'bms_tools\bms.py'
    if (-not (Test-Path -LiteralPath $buildTool)) {
        throw "FirmwareRoot does not contain bms_tools\bms.py: $firmwareRootPath"
    }
    $firmwareHead = (& git -C $firmwareRootPath rev-parse HEAD).Trim()
    Write-Host "Firmware HEAD: $firmwareHead"
    Push-Location $firmwareRootPath
    try {
        & python .\bms_tools\bms.py rebuild
        if ($LASTEXITCODE -ne 0) { throw 'Firmware rebuild failed.' }
        & python .\bms_tools\bms.py check-fw
        if ($LASTEXITCODE -ne 0) { throw 'Firmware check-fw failed.' }
    }
    finally { Pop-Location }
    $Bin = Join-Path $firmwareRootPath 'project\tlsr_tc32\B85\825x_ble_sample_cli\825x_ble_sample.bin'
}

$binPath = (Resolve-Path -LiteralPath $Bin).Path
if ($binPath.EndsWith('.raw.bin', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'raw.bin is not allowed. Use 825x_ble_sample.bin with its Telink OTA trailer.'
}
if (-not $binPath.EndsWith('.bin', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The OTA image must use the .bin extension.'
}

$devices = @(& adb devices | Select-Object -Skip 1 | ForEach-Object {
    if ($_ -match '^([^\s]+)\s+device(?:\s|$)') { $Matches[1] }
})
if ($DeviceId) {
    if ($devices -notcontains $DeviceId) { throw "The selected Android device is not connected: $DeviceId" }
    $script:SelectedDevice = $DeviceId
}
elseif ($devices.Count -eq 1) { $script:SelectedDevice = $devices[0] }
elseif ($devices.Count -eq 0) { throw 'No authorized Android ADB device was found.' }
else { throw "Multiple Android devices were found. Select one with -DeviceId: $($devices -join ', ')" }

$installed = (& adb -s $script:SelectedDevice shell pm path $package 2>$null) -match '^package:'
if ($InstallApp -or -not $installed) {
    Write-Host 'Building and installing the Android app. Omit -InstallApp on later firmware-only runs...'
    & dotnet build $appProject -c Release
    if ($LASTEXITCODE -ne 0) { throw 'Android app Release build failed.' }
    if (-not (Test-Path -LiteralPath $apk)) { throw "APK was not generated: $apk" }
    Invoke-Adb -AdbArguments @('install', '-r', $apk) | Out-Host
}

$file = Get-Item -LiteralPath $binPath
$sha256 = (Get-FileHash -LiteralPath $binPath -Algorithm SHA256).Hash
$remoteName = "ota-$([DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss'))-$($sha256.Substring(0, 8)).bin"
$remotePath = "FirmwareInbox/$remoteName"

Write-Host "BIN: $binPath"
Write-Host "Size: $($file.Length) bytes"
Write-Host "SHA-256: $sha256"
Invoke-Adb -AdbArguments @('logcat', '-c') | Out-Null
$activity = (Invoke-Adb -AdbArguments @('shell', 'cmd', 'package', 'resolve-activity', '--brief', $package) | Select-Object -Last 1).Trim()
if (-not $activity.Contains('/')) { throw "Unable to resolve Android MainActivity: $activity" }
# Some Android vendors block exported receivers while an app is force-stopped.
# Bring the app process to the foreground before sending the bounded import chunks.
Invoke-Adb -AdbArguments @('shell', 'am', 'start', '-n', $activity, '--es', 'mac', $Mac) | Out-Null
Start-Sleep -Seconds 1
$bytes = [IO.File]::ReadAllBytes($binPath)
$chunkBytes = 4096
$totalChunks = [Math]::Ceiling($bytes.Length / [double]$chunkBytes)
$uploadId = [Guid]::NewGuid().ToString('N').Substring(0, 16)
for ($index = 0; $index -lt $totalChunks; $index++) {
    $offset = $index * $chunkBytes
    $count = [Math]::Min($chunkBytes, $bytes.Length - $offset)
    $data = [Convert]::ToBase64String($bytes, $offset, $count)
    Invoke-Adb -AdbArguments @('shell', 'am', 'broadcast', '-a', 'com.cs.bmstool.android.IMPORT_FIRMWARE',
        '-p', $package, '--es', 'upload_id', $uploadId, '--es', 'file_name', $remoteName,
        '--es', 'sha256', $sha256, '--ei', 'index', $index.ToString(), '--ei', 'total', $totalChunks.ToString(),
        '--ei', 'offset', $offset.ToString(), '--es', 'data', $data) | Out-Null
}
Start-Sleep -Milliseconds 500
$importLog = (& adb -s $script:SelectedDevice logcat -d -s 'BmsTool.Android:I' '*:S' | Out-String)
if ($importLog -notmatch "FIRMWARE_IMPORT_OK upload=$uploadId") {
    throw "Android firmware import did not complete. Device log: $importLog"
}

if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
    $EvidenceDirectory = Join-Path $env:LOCALAPPDATA ("CodexTemp\bms-android-ota\" + [DateTimeOffset]::Now.ToString('yyyyMMdd-HHmmss'))
}
New-Item -ItemType Directory -Path $EvidenceDirectory -Force | Out-Null

Invoke-Adb -AdbArguments @('shell', 'am', 'force-stop', $package) | Out-Null
$start = @('shell', 'am', 'start', '-n', $activity, '--es', 'operation', 'ota', '--es', 'mac', $Mac,
    '--es', 'firmware_inbox_name', $remoteName)
if ($ExpectedSerial) { $start += @('--es', 'expected_serial', $ExpectedSerial) }
Invoke-Adb -AdbArguments $start | Out-Host

Write-Host ''
Write-Host 'The app is connecting and will open the OTA confirmation page. Check the target, image and power, then confirm.'
Write-Host "Firmware inbox: $remotePath"
if ($NoWait) { return }

$deadline = [DateTimeOffset]::Now.AddSeconds($TimeoutSeconds)
$result = $null
$logcat = ''
while ([DateTimeOffset]::Now -lt $deadline) {
    Start-Sleep -Seconds 2
    $logcat = (& adb -s $script:SelectedDevice logcat -d -s 'BmsTool.Android:I' 'AndroidRuntime:E' '*:S' | Out-String)
    if ($logcat -match 'TEST_RESULT OTA_OK') { $result = 'OTA_OK'; break }
    if ($logcat -match 'TEST_RESULT CANCELLED') { $result = 'CANCELLED'; break }
    if ($logcat -match 'TEST_RESULT FAIL') { $result = 'FAIL'; break }
}

$logcatPath = Join-Path $EvidenceDirectory 'android-logcat.txt'
$logcat | Set-Content -LiteralPath $logcatPath -Encoding utf8
if ($logcat -match 'LOG_FILE path=([^\r\n]+)') {
    $deviceLog = $Matches[1].Trim()
    try { Invoke-Adb -AdbArguments @('pull', $deviceLog, (Join-Path $EvidenceDirectory 'bms-tool-device.log')) | Out-Null }
    catch { Write-Warning "Unable to pull the device log: $($_.Exception.Message)" }
}

$summary = [ordered]@{
    result = $(if ($null -eq $result) { 'TIMEOUT' } else { $result })
    mac = $Mac
    expected_serial = $ExpectedSerial
    local_bin = $binPath
    remote_bin = $remotePath
    size = $file.Length
    sha256 = $sha256
    device_id = $script:SelectedDevice
    finished_at = [DateTimeOffset]::Now.ToString('O')
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'summary.json') -Encoding utf8
Write-Host "Evidence: $EvidenceDirectory"

switch ($result) {
    'OTA_OK' { Write-Host 'OTA succeeded: OTA_SUCCESS and post-upgrade identity verification passed.'; exit 0 }
    'CANCELLED' { throw 'OTA was cancelled on the phone.' }
    'FAIL' { throw 'OTA failed or success evidence was not confirmed. Inspect the evidence logs.' }
    default { throw "Timed out waiting for the OTA result after $TimeoutSeconds seconds. Inspect the phone and evidence logs." }
}
