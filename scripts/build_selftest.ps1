param(
    [switch]$TestBuild,
    [uint32]$FaultMask = 0,
    [int]$Jobs = 4,
    [string]$ToolchainDir = "C:\TelinkSDK\opt\tc32\bin"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$SdkRoot = Join-Path $RepoRoot "tc_ble_single_sdk-V3.4.2.8_Patch_0001\tc_ble_single_sdk"
$Project = Join-Path $SdkRoot "project\tlsr_tc32\B85\825x_ble_sample"
$Nm = Join-Path $ToolchainDir "tc32-elf-nm.exe"
$env:PATH = "$ToolchainDir;$env:PATH"

if (-not (Test-Path -LiteralPath (Join-Path $ToolchainDir "tc32-elf-gcc.exe"))) {
    throw "TC32 toolchain not found: $ToolchainDir"
}
Push-Location $Project
try {
    & make clean
    if ($LASTEXITCODE -ne 0) { throw "make clean failed: $LASTEXITCODE" }
    $Arguments = @("all", "-j$Jobs")
    if ($TestBuild) {
        $Arguments += "BMS_TEST_BUILD=1"
        $Arguments += "BMS_FAULT_INJECT_ENABLE=1"
        $Arguments += ("BMS_FAULT_INJECT_MASK=0x{0:x}" -f $FaultMask)
    }
    & make @Arguments
    if ($LASTEXITCODE -ne 0) { throw "make all failed: $LASTEXITCODE" }

    & python (Join-Path $SdkRoot "script\selftest\check_memory_layout.py") `
        --elf "tc_ble_single_sdk_B85.elf" --bin "825x_ble_sample.bin" --nm $Nm `
        --json-out "selftest_layout_report.json"
    if ($LASTEXITCODE -ne 0) { throw "memory layout check failed: $LASTEXITCODE" }
}
finally {
    Pop-Location
}

& python -m unittest discover -s (Join-Path $RepoRoot "tests\selftest") -p "test_*.py" -v
if ($LASTEXITCODE -ne 0) { throw "host self-test suite failed: $LASTEXITCODE" }
