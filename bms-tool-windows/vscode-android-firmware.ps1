[CmdletBinding()]
param(
    [switch]$BuildFirmware,
    [switch]$SelectFirmware,
    [switch]$ConnectOnly,
    [string]$FirmwarePath
)

$ErrorActionPreference = 'Stop'
$toolRoot = $PSScriptRoot
$repositoryRoot = Split-Path -Parent $toolRoot
$defaultFirmware = Join-Path $repositoryRoot 'tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk\project\tlsr_tc32\B85\825x_ble_sample_cli\825x_ble_sample.bin'
$senderProject = Join-Path $toolRoot 'BmsTool.Android.Sender\BmsTool.Android.Sender.csproj'
$senderDll = Join-Path $toolRoot 'BmsTool.Android.Sender\bin\Release\net8.0-windows10.0.19041.0\BmsTool.Android.Sender.dll'

if ($BuildFirmware) {
    Push-Location $repositoryRoot
    try {
        & python .\bms_tools\bms.py rebuild
        if ($LASTEXITCODE -ne 0) { throw 'Firmware rebuild failed.' }
        & python .\bms_tools\bms.py check-fw
        if ($LASTEXITCODE -ne 0) { throw 'Firmware check-fw failed.' }
    }
    finally { Pop-Location }
}

& dotnet build $senderProject -c Release --nologo
if ($LASTEXITCODE -ne 0) { throw 'Android direct sender build failed.' }
if (-not (Test-Path -LiteralPath $senderDll)) { throw "Sender was not generated: $senderDll" }

if ($ConnectOnly) {
    & dotnet $senderDll --connect-only
    if ($LASTEXITCODE -ne 0) { throw "Android wireless connection failed (exit $LASTEXITCODE)." }
    return
}

if ($SelectFirmware) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = 'Select a Telink OTA firmware BIN to send to Android'
    $dialog.Filter = 'Firmware BIN (*.bin)|*.bin|All files (*.*)|*.*'
    $dialog.CheckFileExists = $true
    $defaultDirectory = Split-Path -Parent $defaultFirmware
    $dialog.InitialDirectory = if (Test-Path -LiteralPath $defaultDirectory) { $defaultDirectory } else { $repositoryRoot }
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
        Write-Host 'Selection cancelled. No firmware was sent.'
        return
    }
    $firmware = $dialog.FileName
}
else {
    $firmware = if ([string]::IsNullOrWhiteSpace($FirmwarePath)) { $defaultFirmware } else { $FirmwarePath }
}
if (-not (Test-Path -LiteralPath $firmware -PathType Leaf)) {
    throw "Firmware was not found. Run the build task first: $firmware"
}
$firmware = (Resolve-Path -LiteralPath $firmware).Path

& dotnet $senderDll --firmware $firmware
if ($LASTEXITCODE -ne 0) { throw "Android direct sender failed (exit $LASTEXITCODE)." }
