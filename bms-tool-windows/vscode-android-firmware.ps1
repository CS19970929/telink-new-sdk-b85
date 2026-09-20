[CmdletBinding()]
param(
    [switch]$BuildFirmware,
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

$firmware = if ([string]::IsNullOrWhiteSpace($FirmwarePath)) { $defaultFirmware } else { $FirmwarePath }
if (-not (Test-Path -LiteralPath $firmware -PathType Leaf)) {
    throw "Firmware was not found. Run the build task first: $firmware"
}
$firmware = (Resolve-Path -LiteralPath $firmware).Path

& dotnet build $senderProject -c Release --nologo
if ($LASTEXITCODE -ne 0) { throw 'Android direct sender build failed.' }
if (-not (Test-Path -LiteralPath $senderDll)) { throw "Sender was not generated: $senderDll" }

& dotnet $senderDll --firmware $firmware
if ($LASTEXITCODE -ne 0) { throw "Android direct sender failed (exit $LASTEXITCODE)." }
