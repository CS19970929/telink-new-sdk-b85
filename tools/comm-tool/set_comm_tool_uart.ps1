param(
    [ValidateSet('USART1', 'USART3')]
    [string]$Port = 'USART1'
)

$ErrorActionPreference = 'Stop'

$RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$ConfigPath = Join-Path $RepoRoot 'firmware\comm_tool_f103ret6\source\app\ct_config.h'
$Target = "CT_COMM_UART_PORT_$Port"

if (-not (Test-Path -LiteralPath $ConfigPath)) {
    throw "Config file not found: $ConfigPath"
}

$Content = [System.IO.File]::ReadAllText($ConfigPath)
$Pattern = '(?m)^#define\s+CT_COMM_UART_PORT\s+CT_COMM_UART_PORT_USART[13]\s*$'
$Replacement = "#define CT_COMM_UART_PORT              $Target"

if ($Content -notmatch $Pattern) {
    throw 'CT_COMM_UART_PORT define was not found or is in an unexpected format.'
}

$Updated = [System.Text.RegularExpressions.Regex]::Replace($Content, $Pattern, $Replacement, 1)
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($ConfigPath, $Updated, $Utf8NoBom)

Write-Host "Comm tool UART set to $Port in $ConfigPath"
