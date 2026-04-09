param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$content = @(
    '#pragma once'
    '/* Auto-generated during build. Do not edit manually. */'
    ('#define BMS_SOFTWARE_BUILD_TIMESTAMP "{0}"' -f $timestamp)
    ''
) -join "`r`n"

$outputDir = Split-Path -Parent $OutputPath
if (-not (Test-Path -LiteralPath $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}

Set-Content -LiteralPath $OutputPath -Value $content -Encoding ASCII
