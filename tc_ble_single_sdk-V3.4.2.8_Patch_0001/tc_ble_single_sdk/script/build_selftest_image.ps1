param(
    [Parameter(Mandatory = $true)][string]$Elf,
    [Parameter(Mandatory = $true)][string]$Bin,
    [Parameter(Mandatory = $true)][string]$Report,
    [switch]$TestBuild,
    [string]$ToolchainDir = ""
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SdkRoot = Split-Path -Parent $ScriptDir
$SelfTestScripts = Join-Path $ScriptDir "selftest"

if (-not $ToolchainDir) {
    if ($env:TELINK_TC32_TOOLCHAIN) { $ToolchainDir = $env:TELINK_TC32_TOOLCHAIN }
    else { $ToolchainDir = "C:\TelinkSDK\opt\tc32\bin" }
}
$Objcopy = Join-Path $ToolchainDir "tc32-elf-objcopy.exe"
$Nm = Join-Path $ToolchainDir "tc32-elf-nm.exe"
$TelinkCheck = Join-Path $ScriptDir "tl_check_fw\tl_check_fw2.exe"
foreach ($Required in @($Objcopy, $Nm, $TelinkCheck)) {
    if (-not (Test-Path -LiteralPath $Required)) { throw "Required build tool not found: $Required" }
}

& $Objcopy -v -O binary $Elf $Bin
if ($LASTEXITCODE -ne 0) { throw "tc32-elf-objcopy failed: $LASTEXITCODE" }

$PatchArgs = @((Join-Path $SelfTestScripts "patch_image_crc.py"), "--elf", $Elf, "--image", $Bin, "--nm", $Nm)
if ($TestBuild) { $PatchArgs += "--test-build" }
& python @PatchArgs
if ($LASTEXITCODE -ne 0) { throw "BMS manifest patch failed: $LASTEXITCODE" }

& $TelinkCheck $Bin
if ($LASTEXITCODE -ne 0) { throw "tl_check_fw2.exe failed: $LASTEXITCODE" }

& python (Join-Path $SelfTestScripts "verify_image_crc.py") --elf $Elf --image $Bin --nm $Nm --json-out $Report
if ($LASTEXITCODE -ne 0) { throw "BMS/Telink image verification failed: $LASTEXITCODE" }
