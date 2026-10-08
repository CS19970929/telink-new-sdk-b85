# Windows PowerShell 5.1：用勾选窗口收集配置，复用已验证的 Python 镜像流程。
[CmdletBinding()]
param(
    [string[]]$Configurations,
    [switch]$Rebuild,
    [switch]$PlanOnly
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$workflow = Join-Path $PSScriptRoot 'android_ota.py'
$targets = @(
    @{ Id = 'd008-16s-lfp'; Name = 'D008 16S 磷酸铁锂' },
    @{ Id = 'd008-20s-nmc'; Name = 'D008 20S 三元锂' },
    @{ Id = 'd008-24s-lfp'; Name = 'D008 24S 磷酸铁锂' },
    @{ Id = 'd011'; Name = 'D011 10S' },
    @{ Id = 'd013'; Name = 'D013 4S' },
    @{ Id = 'd014'; Name = 'D014 8S' }
)
$choices = @()
foreach ($mode in @('production', 'development')) {
    foreach ($target in $targets) {
        $modeName = if ($mode -eq 'production') { '生产' } else { '开发' }
        $choices += @{ Id = "$($mode):$($target.Id)"; Mode = $mode; Target = $target.Id;
                       Name = "[$modeName] $($target.Name)" }
    }
}
if ($PlanOnly -and -not $Configurations) {
    throw 'PlanOnly requires explicit Configurations.'
}
if (-not $Configurations) {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [System.Windows.Forms.Application]::EnableVisualStyles()
    $form = New-Object System.Windows.Forms.Form
    $form.Text = 'BMS 单个 / 批量 BIN 编译'
    $form.ClientSize = New-Object System.Drawing.Size(560, 480)
    $form.StartPosition = 'CenterScreen'
    $form.FormBorderStyle = 'FixedDialog'
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false
    $form.TopMost = $true
    $form.Font = New-Object System.Drawing.Font('Microsoft YaHei UI', 10)

    $hint = New-Object System.Windows.Forms.Label
    $hint.Text = '勾选一个或多个配置，自动生成 BIN 和校验清单；此入口不发送 OTA。'
    $hint.SetBounds(16, 12, 528, 44)
    $form.Controls.Add($hint)
    $list = New-Object System.Windows.Forms.CheckedListBox
    $list.SetBounds(16, 60, 528, 274)
    $list.CheckOnClick = $true
    foreach ($choice in $choices) { [void]$list.Items.Add($choice.Name) }
    $list.SetItemChecked(0, $true)
    $form.Controls.Add($list)

    $allDev = New-Object System.Windows.Forms.Button
    $allDev.Text = '全部开发'
    $allDev.SetBounds(16, 344, 112, 32)
    $allDev.Add_Click({
        for ($i = 0; $i -lt $choices.Count; $i++) {
            $list.SetItemChecked($i, $choices[$i].Mode -eq 'development')
        }
    })
    $form.Controls.Add($allDev)
    $allProd = New-Object System.Windows.Forms.Button
    $allProd.Text = '全部生产'
    $allProd.SetBounds(138, 344, 112, 32)
    $allProd.Add_Click({
        for ($i = 0; $i -lt $choices.Count; $i++) {
            $list.SetItemChecked($i, $choices[$i].Mode -eq 'production')
        }
    })
    $form.Controls.Add($allProd)
    $clear = New-Object System.Windows.Forms.Button
    $clear.Text = '清空选择'
    $clear.SetBounds(260, 344, 112, 32)
    $clear.Add_Click({
        for ($i = 0; $i -lt $choices.Count; $i++) { $list.SetItemChecked($i, $false) }
    })
    $form.Controls.Add($clear)
    $clean = New-Object System.Windows.Forms.CheckBox
    $clean.Text = '清理后全量重编译（不勾选则增量编译）'
    $clean.SetBounds(16, 384, 528, 28)
    $clean.Checked = $Rebuild.IsPresent
    $form.Controls.Add($clean)
    $note = New-Object System.Windows.Forms.Label
    $note.Text = '生产模式须提交源码并通过批准门；失败配置单独报告，继续其他配置。'
    $note.SetBounds(16, 412, 528, 26)
    $form.Controls.Add($note)
    $start = New-Object System.Windows.Forms.Button
    $start.Text = '开始编译'
    $start.SetBounds(306, 442, 112, 30)
    $start.Add_Click({
        if ($list.CheckedIndices.Count -eq 0) {
            [void][System.Windows.Forms.MessageBox]::Show('请至少选择一个配置。', 'BMS')
            return
        }
        $form.DialogResult = [System.Windows.Forms.DialogResult]::OK
        $form.Close()
    })
    $form.Controls.Add($start)
    $cancel = New-Object System.Windows.Forms.Button
    $cancel.Text = '取消'
    $cancel.SetBounds(430, 442, 112, 30)
    $cancel.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
    $form.Controls.Add($cancel)
    $form.AcceptButton = $start
    $form.CancelButton = $cancel
    try {
        if ($form.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
            Write-Host '已取消，没有编译或发送固件。'
            exit 0
        }
        $Configurations = @($list.CheckedIndices | ForEach-Object { $choices[$_].Id })
        $Rebuild = [bool]$clean.Checked
    } finally { $form.Dispose() }
}

$commands = @()
foreach ($configuration in ($Configurations | Select-Object -Unique)) {
    $choice = @($choices | Where-Object { $_.Id -eq $configuration })
    if ($choice.Count -ne 1) { throw "Unknown build configuration: $configuration" }
    $arguments = @($workflow, '--target', $choice[0].Target, '--mode', $choice[0].Mode, '--build-only')
    if ($Rebuild) { $arguments += '--rebuild' }
    $commands += @{ Configuration = $configuration; Arguments = $arguments }
}
if ($PlanOnly) {
    ConvertTo-Json -InputObject $commands -Depth 4
    exit 0
}
$env:PYTHONUTF8 = '1'
$env:PYTHONDONTWRITEBYTECODE = '1'
$failed = @()
$succeeded = @()
Push-Location $repositoryRoot
try {
    foreach ($command in $commands) {
        Write-Host "===== 编译 $($command.Configuration) ====="
        $pythonArguments = $command.Arguments
        & python @pythonArguments
        if ($LASTEXITCODE -ne 0) {
            $failed += $command.Configuration
        } else {
            $succeeded += $command.Configuration
        }
    }
} finally { Pop-Location }
Write-Host "完成：成功 $($succeeded.Count)，失败 $($failed.Count)。"
foreach ($configuration in $succeeded) { Write-Host "BIN 已校验：$configuration" }
foreach ($configuration in $failed) { Write-Host "失败（不作为本次有效镜像）：$configuration" }
Write-Host "输出目录：$(Join-Path $repositoryRoot 'firmware')"
if ($failed.Count -gt 0) { exit 1 }
exit 0
