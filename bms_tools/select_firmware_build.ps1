# Windows PowerShell 5.1：单项目窗口；编译与发送复用同一份配置。
[CmdletBinding()]
param(
    [string]$Target,
    [ValidateSet('production', 'development')][string]$Mode = 'production',
    [ValidateSet('lfp', 'nmc')][string]$Chemistry,
    [ValidateRange(0, 6553)][int]$Capacity0p1Ah = 0,
    [ValidateRange(0, 65535)][int]$ParametersRevision = 0,
    [ValidateRange(0, 65535)][int]$SocStateRevision = 0,
    [switch]$Rebuild,
    [switch]$PlanOnly,
    [switch]$BuildOnly
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$workflow = Join-Path $PSScriptRoot 'android_ota.py'
$targets = @(
    @{ Id = 'd008-16s-lfp'; Name = 'D008 16S 磷酸铁锂' },
    @{ Id = 'd008-20s-nmc'; Name = 'D008 20S 三元锂' },
    @{ Id = 'd008-24s-lfp'; Name = 'D008 24S 磷酸铁锂' },
    @{ Id = 'd011'; Name = 'D011' },
    @{ Id = 'd013'; Name = 'D013' },
    @{ Id = 'd014'; Name = 'D014' }
)
if ($Target -and $Target -notin $targets.Id) { throw "Unknown build target: $Target" }
if ($PlanOnly -and -not $Target) { throw 'PlanOnly requires an explicit Target.' }

function Get-TargetDefaults([string]$id) {
    $product = $id.Split('-')[0]
    $header = Get-Content (Join-Path $repositoryRoot "bms/products/$product/bms_product.h") -Raw
    if ($header -notmatch '(?m)^\s*#define\s+BMS_PRODUCT_DEFAULT_CAPACITY_0P1AH\s+(\d+)') {
        throw "无法读取 $product 默认容量。"
    }
    $capacity = [int]$Matches[1]
    if ($product -eq 'd008') {
        if ($id -notmatch '^d008-(16|20|24)s-(lfp|nmc)$') { throw 'D008 装配无效。' }
        $cells = [int]$Matches[1]
        $chemical = $Matches[2]
    } else {
        if ($header -notmatch '(?m)^\s*#define\s+SH3673510_BOARD_CELL_COUNT\s+(\d+)') {
            throw "无法读取 $product 串数。"
        }
        $cells = [int]$Matches[1]
        if ($header -notmatch '(?m)^\s*#define\s+BMS_PRODUCT_CHEMISTRY\s+BMS_SOC_CHEMISTRY_(LFP|NMC)') {
            throw "无法读取 $product 电池类型。"
        }
        $chemical = $Matches[1].ToLowerInvariant()
    }
    return @{ Capacity0p1Ah = $capacity; Chemistry = $chemical; Cells = $cells }
}

if (-not $Target) {
    # 设置保存在用户目录；编号延续，避免下次普通编译意外回退参数版本。
    $hash = [System.Security.Cryptography.SHA256]::Create()
    try {
        $repoKey = ([BitConverter]::ToString($hash.ComputeHash(
            [Text.Encoding]::UTF8.GetBytes($repositoryRoot)))).Replace('-', '').Substring(0, 12)
    } finally { $hash.Dispose() }
    $settingsPath = Join-Path $env:LOCALAPPDATA "BmsBuildMenu/$repoKey.json"
    $settings = @{ Target = $targets[0].Id; Mode = $Mode; Profiles = @{} }
    if (Test-Path -LiteralPath $settingsPath) {
        try {
            $saved = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
            if ($saved.Target -in $targets.Id) { $settings.Target = $saved.Target }
            if ($saved.Mode -in @('production', 'development')) { $settings.Mode = $saved.Mode }
            foreach ($entry in $targets) {
                $item = $saved.Profiles.($entry.Id)
                if ($null -ne $item -and $item.Chemistry -in @('lfp', 'nmc') -and
                    $item.Capacity0p1Ah -ge 1 -and $item.Capacity0p1Ah -le 6553 -and
                    $item.ParametersRevision -ge 0 -and $item.ParametersRevision -le 65535 -and
                    $item.SocStateRevision -ge 0 -and $item.SocStateRevision -le 65535) {
                    $settings.Profiles[$entry.Id] = $item
                }
            }
        } catch { Write-Warning '上次窗口设置无法读取，本次使用源码默认。' }
    }
    $policy = Get-Content (Join-Path $repositoryRoot 'bms/products/bms_parameter_policy.h') -Raw
    $sourceRevisions = @([regex]::Matches($policy, '#define\s+BMS_UPDATE_\w+_REVISION\s+(\d+)u') |
        ForEach-Object { [int]$_.Groups[1].Value })
    if ($sourceRevisions.Count -eq 0) { throw '无法读取源码参数更新编号。' }

    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [System.Windows.Forms.Application]::EnableVisualStyles()
    $form = New-Object System.Windows.Forms.Form
    $form.Text = 'BMS 单项目配置与编译'
    $form.ClientSize = New-Object System.Drawing.Size(590, 430)
    $form.StartPosition = 'CenterScreen'
    $form.FormBorderStyle = 'FixedDialog'
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false
    $form.Font = New-Object System.Drawing.Font('Microsoft YaHei UI', 10)
    function Add-Label([string]$text, [int]$x, [int]$y, [int]$width) {
        $label = New-Object System.Windows.Forms.Label
        $label.Text = $text
        $label.SetBounds($x, $y, $width, 28)
        $form.Controls.Add($label)
    }
    Add-Label '产品 / 装配' 16 20 116
    $productList = New-Object System.Windows.Forms.ComboBox
    $productList.DropDownStyle = 'DropDownList'
    $productList.SetBounds(136, 16, 430, 30)
    foreach ($entry in $targets) { [void]$productList.Items.Add($entry.Name) }
    $form.Controls.Add($productList)
    Add-Label '构建模式' 16 60 116
    $modeList = New-Object System.Windows.Forms.ComboBox
    $modeList.DropDownStyle = 'DropDownList'
    $modeList.SetBounds(136, 56, 190, 30)
    [void]$modeList.Items.Add('开发')
    [void]$modeList.Items.Add('生产')
    $modeList.SelectedIndex = if ($settings.Mode -eq 'production') { 1 } else { 0 }
    $form.Controls.Add($modeList)
    $series = New-Object System.Windows.Forms.Label
    $series.SetBounds(350, 60, 216, 28)
    $form.Controls.Add($series)
    Add-Label '电池类型' 16 100 116
    $chemistryList = New-Object System.Windows.Forms.ComboBox
    $chemistryList.DropDownStyle = 'DropDownList'
    $chemistryList.SetBounds(136, 96, 190, 30)
    [void]$chemistryList.Items.Add('磷酸铁锂')
    [void]$chemistryList.Items.Add('三元锂（4.20V）')
    $form.Controls.Add($chemistryList)
    Add-Label '默认容量 (Ah)' 16 140 116
    $capacityInput = New-Object System.Windows.Forms.NumericUpDown
    $capacityInput.SetBounds(136, 136, 190, 30)
    $capacityInput.DecimalPlaces = 1
    $capacityInput.Increment = [decimal]0.1
    $capacityInput.Minimum = [decimal]0.1
    $capacityInput.Maximum = [decimal]655.3
    $form.Controls.Add($capacityInput)
    $applyDefaults = New-Object System.Windows.Forms.CheckBox
    $applyDefaults.Text = '本次 OTA 恢复配置默认值（保护 / 容量 / 加热 / 均衡 / SOC）'
    $applyDefaults.SetBounds(16, 180, 558, 28)
    $form.Controls.Add($applyDefaults)
    $resetState = New-Object System.Windows.Forms.CheckBox
    $resetState.Text = '更换电池：重置 SOC / 循环'
    $resetState.SetBounds(16, 212, 280, 28)
    $form.Controls.Add($resetState)
    Add-Label '更新编号' 322 216 90
    $revisionInput = New-Object System.Windows.Forms.NumericUpDown
    $revisionInput.SetBounds(414, 212, 152, 30)
    $revisionInput.Minimum = 1
    $revisionInput.Maximum = 65535
    $revisionInput.Enabled = $false
    $form.Controls.Add($revisionInput)
    $tip = New-Object System.Windows.Forms.ToolTip
    $tip.SetToolTip($revisionInput, '默认递增本机记录；须与设备旧编号不同。换电脑或使用其他固件时请核对。')
    $note = New-Object System.Windows.Forms.Label
    $note.Text = "电压保护、总压、均衡和 SOC 曲线自动匹配类型及串数。`r`n串数变化会恢复整套配置（含校准 / SN），升级前先备份。"
    $note.SetBounds(16, 250, 558, 52)
    $form.Controls.Add($note)
    $clean = New-Object System.Windows.Forms.CheckBox
    $clean.Text = '清理后全量重编译（默认增量编译）'
    $clean.SetBounds(16, 306, 558, 28)
    $clean.Checked = $Rebuild.IsPresent
    $form.Controls.Add($clean)
    $autoOta = New-Object System.Windows.Forms.CheckBox
    $autoOta.Text = '生成后发送安卓并请求 OTA（取消勾选则仅生成 BIN）'
    $autoOta.SetBounds(16, 338, 558, 28)
    $autoOta.Checked = -not $BuildOnly.IsPresent
    $form.Controls.Add($autoOta)
    $script:loadingInputs = $false
    $script:targetInputsValid = $false
    $script:previousParametersRevision = 0
    $script:previousSocStateRevision = 0
    function Update-TargetInputs {
        $script:loadingInputs = $true
        $script:targetInputsValid = $false
        try {
            $id = $targets[$productList.SelectedIndex].Id
            $defaults = Get-TargetDefaults $id
            $item = $settings.Profiles[$id]
            $chemical = $defaults.Chemistry
            $capacity = $defaults.Capacity0p1Ah
            $script:previousParametersRevision = 0
            $script:previousSocStateRevision = 0
            if ($null -ne $item) {
                if (-not $id.StartsWith('d008-')) { $chemical = $item.Chemistry }
                $capacity = [int]$item.Capacity0p1Ah
                $script:previousParametersRevision = [int]$item.ParametersRevision
                $script:previousSocStateRevision = [int]$item.SocStateRevision
            }
            $chemistryList.SelectedIndex = if ($chemical -eq 'nmc') { 1 } else { 0 }
            $chemistryList.Enabled = -not $id.StartsWith('d008-')
            $series.Text = "$($defaults.Cells) 串（随装配配置）"
            $capacityInput.Value = [decimal]$capacity / 10
            $applyDefaults.Checked = $false
            $resetState.Checked = $false
            $maximumRevision = ($sourceRevisions + @($script:previousParametersRevision,
                $script:previousSocStateRevision) | Measure-Object -Maximum).Maximum
            $revisionInput.Value = [Math]::Min(65535, $maximumRevision + 1)
            $script:targetInputsValid = $true
        } catch {
            [void][System.Windows.Forms.MessageBox]::Show("配置读取失败：$_", 'BMS')
        } finally { $script:loadingInputs = $false }
    }
    $productList.Add_SelectedIndexChanged({ Update-TargetInputs })
    $chemistryList.Add_SelectedIndexChanged({ if (-not $script:loadingInputs) { $applyDefaults.Checked = $true } })
    $capacityInput.Add_ValueChanged({ if (-not $script:loadingInputs) { $applyDefaults.Checked = $true } })
    $applyDefaults.Add_CheckedChanged({ $revisionInput.Enabled = $applyDefaults.Checked -or $resetState.Checked })
    $resetState.Add_CheckedChanged({ $revisionInput.Enabled = $applyDefaults.Checked -or $resetState.Checked })
    $productList.SelectedIndex = [array]::IndexOf(@($targets.Id), $settings.Target)
    $start = New-Object System.Windows.Forms.Button
    $start.Text = '开始编译'
    $start.SetBounds(322, 384, 116, 32)
    $start.Add_Click({
        if (-not $script:targetInputsValid) { return }
        $number = [int]$revisionInput.Value
        if (($applyDefaults.Checked -and ($number -in $sourceRevisions -or $number -eq $script:previousParametersRevision)) -or
            ($resetState.Checked -and ($number -in $sourceRevisions -or $number -eq $script:previousSocStateRevision))) {
            [void][System.Windows.Forms.MessageBox]::Show('请选择与旧值不同的更新编号，不能重复使用。', 'BMS')
            return
        }
        $form.DialogResult = [System.Windows.Forms.DialogResult]::OK
        $form.Close()
    })
    $form.Controls.Add($start)
    $cancel = New-Object System.Windows.Forms.Button
    $cancel.Text = '取消'
    $cancel.SetBounds(450, 384, 116, 32)
    $cancel.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
    $form.Controls.Add($cancel)
    $form.AcceptButton = $start
    $form.CancelButton = $cancel
    try {
        if ($form.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
            Write-Host '已取消，没有编译或发送固件。'
            exit 0
        }
        $Target = $targets[$productList.SelectedIndex].Id
        $Mode = if ($modeList.SelectedIndex -eq 1) { 'production' } else { 'development' }
        $Chemistry = if ($chemistryList.SelectedIndex -eq 1) { 'nmc' } else { 'lfp' }
        $Capacity0p1Ah = [int]($capacityInput.Value * 10)
        $ParametersRevision = $script:previousParametersRevision
        $SocStateRevision = $script:previousSocStateRevision
        if ($applyDefaults.Checked) { $ParametersRevision = [int]$revisionInput.Value }
        if ($resetState.Checked) { $SocStateRevision = [int]$revisionInput.Value }
        $Rebuild = [bool]$clean.Checked
        $BuildOnly = -not $autoOta.Checked
        $settings.Target = $Target
        $settings.Mode = $Mode
        $settings.Profiles[$Target] = @{ Chemistry = $Chemistry; Capacity0p1Ah = $Capacity0p1Ah;
            ParametersRevision = $ParametersRevision; SocStateRevision = $SocStateRevision }
        # 写入失败则停止，避免已经烧入的新编号无法延续到下次编译。
        [void][System.IO.Directory]::CreateDirectory((Split-Path -Parent $settingsPath))
        $settings | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $settingsPath -Encoding UTF8
    } finally { $tip.Dispose(); $form.Dispose() }
}

$commonArguments = @($workflow, '--target', $Target, '--mode', $Mode)
if ($Chemistry) { $commonArguments += @('--chemistry', $Chemistry) }
if ($Capacity0p1Ah) { $commonArguments += @('--capacity-0p1ah', "$Capacity0p1Ah") }
if ($ParametersRevision) { $commonArguments += @('--parameters-revision', "$ParametersRevision") }
if ($SocStateRevision) { $commonArguments += @('--soc-state-revision', "$SocStateRevision") }
$buildArguments = $commonArguments + @('--build-only')
if ($Rebuild) { $buildArguments += '--rebuild' }
if ($PlanOnly) {
    @{ Configuration = "$($Mode):$Target"; Arguments = $buildArguments } | ConvertTo-Json -Depth 4
    exit 0
}
$env:PYTHONUTF8 = '1'
$env:PYTHONDONTWRITEBYTECODE = '1'
Push-Location $repositoryRoot
try {
    & python @buildArguments
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if (-not $BuildOnly) {
        $sendArguments = $commonArguments + @('--send-only')
        & python @sendArguments
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
} finally { Pop-Location }
exit 0
