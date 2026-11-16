param([Parameter(Mandatory=$true)][string]$CustomerAssembly,
      [Parameter(Mandatory=$true)][string]$InternalAssembly)
& (Join-Path $PSScriptRoot 'test-unified-monitor.ps1') -CustomerAssembly $CustomerAssembly -InternalAssembly $InternalAssembly
if ($LASTEXITCODE -ne 0) { throw '统一一线通主页面检查失败' }
