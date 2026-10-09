# Windows PowerShell 5.1：单项目窗口；编译与发送复用同一份配置。
[CmdletBinding()]
param(
    [string]$Target,
    [ValidateSet('production', 'development')][string]$Mode = 'production',
    [ValidateSet('lfp', 'nmc')][string]$Chemistry,
    [ValidateSet('dvc1124', 'sh3673510', 'sh3673520')][string]$AfeModel,
    [ValidateRange(0, 24)][int]$CellCount = 0,
    [ValidateRange(0, 6553)][int]$Capacity0p1Ah = 0,
    [ValidateSet('sw', 'afe', 'business', 'soc', 'soc_state')][string[]]$UpdateGroups = @(),
    [string]$UpdateId,
    [switch]$Rebuild,
    [switch]$PlanOnly,
    [switch]$BuildOnly
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$workflow = Join-Path $PSScriptRoot 'android_ota.py'
$targets = @(
    @{ Id = 'd008'; Name = 'D008' },
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
        $profile = Get-Content (Join-Path $repositoryRoot 'bms/products/d008/d008_product_profile.h') -Raw
        if ($profile -notmatch '(?m)^\s*#define\s+D008_PRODUCT_PROFILE\s+D008_PRODUCT_PROFILE_(\d+)S_(LFP|NMC)') {
            throw '无法读取 D008 默认串数与电池类型。'
        }
        $cells = [int]$Matches[1]
        $chemical = $Matches[2].ToLowerInvariant()
        $model = 'dvc1124'
    } else {
        if ($header -notmatch '(?m)^\s*#define\s+SH3673510_BOARD_CELL_COUNT\s+(\d+)') {
            throw "无法读取 $product 串数。"
        }
        $cells = [int]$Matches[1]
        if ($header -notmatch '(?m)^\s*#define\s+BMS_PRODUCT_CHEMISTRY\s+BMS_SOC_CHEMISTRY_(LFP|NMC)') {
            throw "无法读取 $product 电池类型。"
        }
        $chemical = $Matches[1].ToLowerInvariant()
        $shDefaults = Get-Content (Join-Path $repositoryRoot 'bms/products/sh3673510_defaults.h') -Raw
        if ($shDefaults -notmatch '(?m)^\s*#define\s+BMS_BUILD_AFE_MODEL\s+(3510|3520)') {
            throw '无法读取 SH 默认 AFE 型号。'
        }
        $model = 'sh367' + $Matches[1]
    }
    return @{ Capacity0p1Ah = $capacity; Chemistry = $chemical; Cells = $cells; AfeModel = $model }
}

if (-not $Target) {
    # 只记住板级配置；参数更新选择每次默认关闭，避免无意覆盖客户参数。
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
            if ($saved.Target -match '^d008-(\d+)s-(lfp|nmc)$') {
                $settings.Target = 'd008'
                $oldD008 = $saved.Profiles.($saved.Target)
                if ($null -ne $oldD008) {
                    $oldD008 | Add-Member -NotePropertyName CellCount -NotePropertyValue ([int]$Matches[1]) -Force
                    $saved.Profiles | Add-Member -NotePropertyName d008 -NotePropertyValue $oldD008 -Force
                }
            }
            if ($saved.Mode -in @('production', 'development')) { $settings.Mode = $saved.Mode }
            foreach ($entry in $targets) {
                $item = $saved.Profiles.($entry.Id)
                if ($null -ne $item -and $item.Chemistry -in @('lfp', 'nmc') -and
                    $item.Capacity0p1Ah -ge 1 -and $item.Capacity0p1Ah -le 6553) {
                    if ($null -eq $item.CellCount) {
                        $item | Add-Member -NotePropertyName CellCount -NotePropertyValue (Get-TargetDefaults $entry.Id).Cells
                    }
                    if ($null -eq $item.AfeModel) {
                        $item | Add-Member -NotePropertyName AfeModel -NotePropertyValue (Get-TargetDefaults $entry.Id).AfeModel
                    }
                    $settings.Profiles[$entry.Id] = $item
                }
            }
        } catch { Write-Warning '上次窗口设置无法读取，本次使用源码默认。' }
    }
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [System.Windows.Forms.Application]::EnableVisualStyles()
    $form = New-Object System.Windows.Forms.Form
    $form.Text = 'BMS 单项目配置与编译'
    $form.ClientSize = New-Object System.Drawing.Size(590, 526)
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
    Add-Label '板型' 16 20 116
    $productList = New-Object System.Windows.Forms.ComboBox
    $productList.DropDownStyle = 'DropDownList'
    $productList.SetBounds(136, 16, 190, 30)
    foreach ($entry in $targets) { [void]$productList.Items.Add($entry.Name) }
    $form.Controls.Add($productList)
    Add-Label 'AFE' 350 20 58
    $afeList = New-Object System.Windows.Forms.ComboBox
    $afeList.DropDownStyle = 'DropDownList'
    $afeList.SetBounds(410, 16, 156, 30)
    $form.Controls.Add($afeList)
    Add-Label '构建模式' 16 60 116
    $modeList = New-Object System.Windows.Forms.ComboBox
    $modeList.DropDownStyle = 'DropDownList'
    $modeList.SetBounds(136, 56, 190, 30)
    [void]$modeList.Items.Add('开发')
    [void]$modeList.Items.Add('生产')
    $modeList.SelectedIndex = if ($settings.Mode -eq 'production') { 1 } else { 0 }
    $form.Controls.Add($modeList)
    Add-Label '串数' 350 60 58
    $cellInput = New-Object System.Windows.Forms.NumericUpDown
    $cellInput.SetBounds(410, 56, 70, 30)
    $cellInput.Minimum = 4
    $cellInput.Maximum = 24
    $form.Controls.Add($cellInput)
    $cellRange = New-Object System.Windows.Forms.Label
    $cellRange.SetBounds(488, 60, 80, 28)
    $form.Controls.Add($cellRange)
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
    $updateBox = New-Object System.Windows.Forms.GroupBox
    $updateBox.Text = 'OTA 后更新所选组为本次默认值（全不选则保留参数）'
    $updateBox.SetBounds(16, 180, 558, 124)
    $form.Controls.Add($updateBox)
    $groupChoices = @(
        @{ Id = 'sw'; Text = '软件保护'; X = 14; Y = 28 },
        @{ Id = 'afe'; Text = 'AFE 硬件保护'; X = 286; Y = 28 },
        @{ Id = 'business'; Text = '容量 / 加热 / 均衡'; X = 14; Y = 58 },
        @{ Id = 'soc'; Text = 'SOC 配置 / 电池类型'; X = 286; Y = 58 },
        @{ Id = 'soc_state'; Text = '更换电池：重置 SOC / 循环'; X = 14; Y = 88 }
    )
    $updateChecks = @{}
    foreach ($choice in $groupChoices) {
        $check = New-Object System.Windows.Forms.CheckBox
        $check.Text = $choice.Text
        $check.SetBounds($choice.X, $choice.Y, 264, 28)
        $updateBox.Controls.Add($check)
        $updateChecks[$choice.Id] = $check
    }
    $note = New-Object System.Windows.Forms.Label
    $note.Text = "BIN 自带更新选择；无需读取客户板子编号，只执行一次。`r`n换串数或电池类型须更新四组；接线须匹配 AFE 手册。`r`n校准、SN 和事件保留；换电池可单独重置 SOC / 循环。"
    $note.SetBounds(16, 318, 558, 74)
    $form.Controls.Add($note)
    $clean = New-Object System.Windows.Forms.CheckBox
    $clean.Text = '清理后全量重编译（默认增量编译）'
    $clean.SetBounds(16, 400, 558, 28)
    $clean.Checked = $Rebuild.IsPresent
    $form.Controls.Add($clean)
    $autoOta = New-Object System.Windows.Forms.CheckBox
    $autoOta.Text = '生成后发送安卓并请求 OTA（取消勾选则仅生成 BIN）'
    $autoOta.SetBounds(16, 432, 558, 28)
    $autoOta.Checked = -not $BuildOnly.IsPresent
    $form.Controls.Add($autoOta)
    $script:loadingInputs = $false
    $script:targetInputsValid = $false
    function Select-LinkedGroups {
        if (-not $script:loadingInputs) {
            foreach ($group in @('sw', 'afe', 'business', 'soc')) { $updateChecks[$group].Checked = $true }
        }
    }
    function Update-CellRange {
        if ($productList.SelectedIndex -lt 0 -or $afeList.SelectedIndex -lt 0) { return }
        $maximum = if ($targets[$productList.SelectedIndex].Id -eq 'd008') {
            if ($chemistryList.SelectedIndex -eq 1) { 23 } else { 24 }
        } else { if ($afeList.SelectedIndex -eq 1) { 20 } else { 10 } }
        $cellInput.Maximum = $maximum
        $cellRange.Text = "4～$maximum"
    }
    function Update-TargetInputs {
        $script:loadingInputs = $true
        $script:targetInputsValid = $false
        try {
            $id = $targets[$productList.SelectedIndex].Id
            $defaults = Get-TargetDefaults $id
            $item = $settings.Profiles[$id]
            $chemical = $defaults.Chemistry
            $capacity = $defaults.Capacity0p1Ah
            $cells = $defaults.Cells
            $model = $defaults.AfeModel
            if ($null -ne $item) {
                $chemical = $item.Chemistry
                $cells = [int]$item.CellCount
                $model = $item.AfeModel
                $capacity = [int]$item.Capacity0p1Ah
            }
            $afeList.Items.Clear()
            if ($id -eq 'd008') {
                [void]$afeList.Items.Add('DVC1124-2')
                $afeList.Enabled = $false
                $afeList.SelectedIndex = 0
            } else {
                [void]$afeList.Items.Add('SH3673510')
                [void]$afeList.Items.Add('SH3673520')
                $afeList.Enabled = $true
                $afeList.SelectedIndex = if ($model -eq 'sh3673520') { 1 } else { 0 }
            }
            $chemistryList.SelectedIndex = if ($chemical -eq 'nmc') { 1 } else { 0 }
            Update-CellRange
            $cellInput.Value = $cells
            $capacityInput.Value = [decimal]$capacity / 10
            foreach ($check in $updateChecks.Values) { $check.Checked = $false }
            $script:targetInputsValid = $true
        } catch {
            [void][System.Windows.Forms.MessageBox]::Show("配置读取失败：$_", 'BMS')
        } finally { $script:loadingInputs = $false }
    }
    $afeList.Add_SelectedIndexChanged({ Update-CellRange; Select-LinkedGroups })
    $productList.Add_SelectedIndexChanged({ Update-TargetInputs })
    $chemistryList.Add_SelectedIndexChanged({ Update-CellRange; Select-LinkedGroups })
    $cellInput.Add_ValueChanged({ Select-LinkedGroups })
    $capacityInput.Add_ValueChanged({ if (-not $script:loadingInputs) { $updateChecks['business'].Checked = $true } })
    $productList.SelectedIndex = [array]::IndexOf(@($targets.Id), $settings.Target)
    $start = New-Object System.Windows.Forms.Button
    $start.Text = '开始编译'
    $start.SetBounds(322, 480, 116, 32)
    $start.Add_Click({
        if (-not $script:targetInputsValid) { return }
        $form.DialogResult = [System.Windows.Forms.DialogResult]::OK
        $form.Close()
    })
    $form.Controls.Add($start)
    $cancel = New-Object System.Windows.Forms.Button
    $cancel.Text = '取消'
    $cancel.SetBounds(450, 480, 116, 32)
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
        $CellCount = [int]$cellInput.Value
        $AfeModel = if ($Target -eq 'd008') { 'dvc1124' } else {
            if ($afeList.SelectedIndex -eq 1) { 'sh3673520' } else { 'sh3673510' }
        }
        $UpdateGroups = @($groupChoices | Where-Object { $updateChecks[$_.Id].Checked } | ForEach-Object { $_.Id })
        $Rebuild = [bool]$clean.Checked
        $BuildOnly = -not $autoOta.Checked
        $settings.Target = $Target
        $settings.Mode = $Mode
        $settings.Profiles[$Target] = @{ Chemistry = $Chemistry; CellCount = $CellCount; AfeModel = $AfeModel; Capacity0p1Ah = $Capacity0p1Ah }
        [void][System.IO.Directory]::CreateDirectory((Split-Path -Parent $settingsPath))
        $settings | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $settingsPath -Encoding UTF8
    } finally { $form.Dispose() }
}

if ($UpdateGroups.Count -gt 0 -and -not $UpdateId) {
    $random = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        $bytes = New-Object byte[] 10
        $random.GetBytes($bytes)
        $UpdateId = '0000' + ([BitConverter]::ToString($bytes)).Replace('-', '').ToLowerInvariant()
        if ($UpdateId -eq ('0' * 24)) { $UpdateId = '000000000000000000000001' }
    } finally { $random.Dispose() }
}
$commonArguments = @($workflow, '--target', $Target, '--mode', $Mode)
if ($AfeModel) { $commonArguments += @('--afe-model', $AfeModel) }
if ($Chemistry) { $commonArguments += @('--chemistry', $Chemistry) }
if ($CellCount) { $commonArguments += @('--cell-count', "$CellCount") }
if ($Capacity0p1Ah) { $commonArguments += @('--capacity-0p1ah', "$Capacity0p1Ah") }
if ($UpdateGroups.Count -gt 0) { $commonArguments += @('--update-groups') + $UpdateGroups }
if ($UpdateId) { $commonArguments += @('--update-id', $UpdateId) }
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
