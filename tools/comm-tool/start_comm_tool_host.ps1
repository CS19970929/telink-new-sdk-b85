param(
    [ValidateSet("list-ports", "info", "set-can", "fw-dry-run", "fw-download", "fw-info", "bms-read", "bms-write", "enter-iap", "upgrade", "upgrade-status", "upgrade-abort")]
    [string]$Mode = "info",
    [string]$Port = "COM4",
    [int]$Baud = 115200,
    [string]$Bin = "",
    [string]$AppAddress = "",
    [string]$Address = "",
    [string]$Count = "",
    [string[]]$Values = @(),
    [string]$ConfirmAppAddress = "",
    [int]$CanBitrate = 250000,
    [int]$NodeId = 1,
    [int]$AppCanAddr = 0,
    [string]$PythonVersion = "3.9",
    [switch]$ConfirmEnterIap,
    [switch]$ConfirmUpgrade
)

$ErrorActionPreference = "Stop"
$RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$Py = (Get-Command py.exe -ErrorAction Stop).Source

Push-Location $RepoRoot
try {
    $ArgsList = @(
        "-$PythonVersion",
        "tools\comm-tool\comm_tool_host.py",
        $Mode
    )

    if ($Mode -ne "list-ports" -and $Mode -ne "fw-dry-run") {
        $ArgsList += @("--port", $Port, "--baud", [string]$Baud)
    }

    if ($Mode -eq "fw-dry-run" -or $Mode -eq "fw-download") {
        if ([string]::IsNullOrWhiteSpace($Bin)) {
            throw "Mode=$Mode 需要提供 -Bin"
        }
        $ArgsList += @("--bin", $Bin)
        if (-not [string]::IsNullOrWhiteSpace($AppAddress)) {
            $ArgsList += @("--app-address", $AppAddress)
        }
    }

    if ($Mode -eq "fw-download") {
        $ArgsList += @("--confirm-app-address", $ConfirmAppAddress)
    }

    if ($Mode -eq "set-can") {
        $ArgsList += @("--can-bitrate", [string]$CanBitrate, "--node-id", [string]$NodeId, "--app-can-addr", [string]$AppCanAddr)
    }

    if ($Mode -eq "bms-read") {
        if ([string]::IsNullOrWhiteSpace($Address) -or [string]::IsNullOrWhiteSpace($Count)) {
            throw "Mode=bms-read 需要提供 -Address 和 -Count"
        }
        $ArgsList += @("--address", $Address, "--count", $Count)
    }

    if ($Mode -eq "bms-write") {
        if ([string]::IsNullOrWhiteSpace($Address) -or $Values.Count -eq 0) {
            throw "Mode=bms-write 需要提供 -Address 和 -Values"
        }
        $ArgsList += @("--address", $Address)
        $ArgsList += $Values
    }

    if ($Mode -eq "enter-iap" -and $ConfirmEnterIap) {
        $ArgsList += "--confirm-enter-iap"
    }

    if ($Mode -eq "upgrade" -and $ConfirmUpgrade) {
        $ArgsList += "--confirm-upgrade"
    }

    & $Py @ArgsList
    exit $LASTEXITCODE
}
catch {
    Write-Error @"
comm tool 上位机启动失败。

依赖安装：
  py -$PythonVersion -m pip install pyserial

错误信息：
$($_.Exception.Message)
"@
    exit 1
}
finally {
    Pop-Location
}
