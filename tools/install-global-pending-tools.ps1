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

Write-Host "Global tools installed:"
Write-Host "  $install"
Write-Host ""
Write-Host "Open a new terminal, then run:"
Write-Host "  pending-init"
