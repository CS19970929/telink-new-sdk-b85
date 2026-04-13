param(
    [string]$SourceRoot = (Get-Location).Path,
    [string]$InstallRoot = (Join-Path $env:USERPROFILE '.pending-tools')
)

$ErrorActionPreference = 'Stop'

$source = (Resolve-Path $SourceRoot).Path
$install = [System.IO.Path]::GetFullPath($InstallRoot)

New-Item -ItemType Directory -Force -Path $install | Out-Null

$items = @(
    'tools\pending-init.ps1',
    'tools\pending-init.cmd',
    'tools\install-global-pending-tools.ps1'
)

foreach ($item in $items) {
    $src = Join-Path $source $item
    $dst = Join-Path $install ([System.IO.Path]::GetFileName($item))
    Copy-Item -Force -LiteralPath $src -Destination $dst
}

$currentPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if ([string]::IsNullOrWhiteSpace($currentPath)) {
    $newPath = $install
} elseif ($currentPath -notlike "*$install*") {
    $newPath = "$currentPath;$install"
} else {
    $newPath = $currentPath
}

[Environment]::SetEnvironmentVariable('Path', $newPath, 'User')

if ($PROFILE) {
    $profileDir = Split-Path -Parent $PROFILE
    if (-not [string]::IsNullOrWhiteSpace($profileDir)) {
        New-Item -ItemType Directory -Force -Path $profileDir | Out-Null
    }

    $profileMarkerStart = '# BEGIN pending-init'
    $profileMarkerEnd = '# END pending-init'
    $profileBlock = @"
$profileMarkerStart
function pending-init {
    & "$install\pending-init.ps1" @args
}
$profileMarkerEnd
"@

    $profileText = ''
    if (Test-Path $PROFILE) {
        $profileText = Get-Content -Raw -LiteralPath $PROFILE
    }

    if ($profileText -notmatch [regex]::Escape($profileMarkerStart)) {
        Add-Content -LiteralPath $PROFILE -Value "`r`n$profileBlock"
    } else {
        $updated = [regex]::Replace(
            $profileText,
            '(?ms)# BEGIN pending-init.*?# END pending-init',
            $profileBlock
        )
        Set-Content -LiteralPath $PROFILE -Value $updated -Encoding UTF8
    }
}

Write-Host "Global tools installed:"
Write-Host "  $install"
Write-Host ""
Write-Host "Open a new terminal, then run:"
Write-Host "  pending-init"
