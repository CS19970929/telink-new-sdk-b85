param(
    [switch]$SkipClean,
    [switch]$NoPostBuild,
    [int]$Jobs = 4,
    [string]$ToolchainDir = "C:\TelinkSDK\opt\tc32\bin"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SdkRoot = Split-Path -Parent $ScriptDir
$ProjectDir = Join-Path $SdkRoot "project\tlsr_tc32\B85\825x_ble_sample"
$PostBuildTool = Join-Path $SdkRoot "script\tl_check_fw\tl_check_fw2.exe"

if (-not (Test-Path -LiteralPath $ProjectDir)) {
    throw "B85 project directory not found: $ProjectDir"
}

if (-not (Test-Path -LiteralPath (Join-Path $ToolchainDir "tc32-elf-gcc.exe"))) {
    throw "TC32 toolchain not found: $ToolchainDir"
}

$env:PATH = "$ToolchainDir;$env:PATH"

if (-not (Get-Command make -ErrorAction SilentlyContinue)) {
    throw "make command not found. Install Telink/qtools make and add it to PATH."
}

Push-Location $ProjectDir
try {
    Write-Host "Project: $ProjectDir"
    Write-Host "Toolchain: $ToolchainDir"

    if (-not $SkipClean) {
        Write-Host "Running: make clean"
        make clean
        if ($LASTEXITCODE -ne 0) {
            throw "make clean failed with exit code $LASTEXITCODE"
        }
    }

    Write-Host "Running: make all -j$Jobs"
    make all "-j$Jobs"
    if ($LASTEXITCODE -ne 0) {
        throw "make all failed with exit code $LASTEXITCODE"
    }

    if (-not (Test-Path -LiteralPath "tc_ble_single_sdk_B85.elf")) {
        throw "ELF output not found: tc_ble_single_sdk_B85.elf"
    }

    if (-not $NoPostBuild) {
        Write-Host "Running: tc32-elf-objcopy -v -O binary tc_ble_single_sdk_B85.elf 825x_ble_sample.bin"
        tc32-elf-objcopy -v -O binary tc_ble_single_sdk_B85.elf 825x_ble_sample.bin
        if ($LASTEXITCODE -ne 0) {
            throw "tc32-elf-objcopy failed with exit code $LASTEXITCODE"
        }

        if (Test-Path -LiteralPath $PostBuildTool) {
            Write-Host "Running: $PostBuildTool 825x_ble_sample.bin"
            & $PostBuildTool 825x_ble_sample.bin
            if ($LASTEXITCODE -ne 0) {
                throw "tl_check_fw2.exe failed with exit code $LASTEXITCODE"
            }
        }
        else {
            Write-Warning "Post-build tool not found: $PostBuildTool"
        }
    }

    Write-Host "Running: tc32-elf-size -t tc_ble_single_sdk_B85.elf"
    tc32-elf-size -t tc_ble_single_sdk_B85.elf
    if ($LASTEXITCODE -ne 0) {
        throw "tc32-elf-size failed with exit code $LASTEXITCODE"
    }

    Write-Host "Outputs:"
    Get-Item -LiteralPath "tc_ble_single_sdk_B85.elf", "825x_ble_sample.lst", "825x_ble_sample.bin" -ErrorAction SilentlyContinue |
        ForEach-Object {
            Write-Host ("  {0}  {1} bytes  {2}" -f $_.Name, $_.Length, $_.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss"))
        }
}
finally {
    Pop-Location
}
