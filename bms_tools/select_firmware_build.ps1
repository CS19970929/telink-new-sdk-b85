# Windows PowerShell 5.1：单项目窗口；编译与发送复用同一份配置。
[CmdletBinding()]
param(
    [string]$Target,
    [ValidateSet('production', 'development')][string]$Mode = 'production',
    [ValidateSet('lfp', 'nmc')][string]$Chemistry,
    [ValidateSet('dvc1124', 'sh3673510', 'sh3673520')][string]$AfeModel,
    [ValidateRange(0, 24)][int]$CellCount = 0,
    [ValidateRange(0, 6553)][int]$Capacity0p1Ah = 0,
    [ValidateSet('sw', 'afe', 'business', 'soc', 'soc_state', 'factory_reset')][string[]]$UpdateGroups = @(),
    [string]$UpdateId,
    [switch]$Rebuild,
    [switch]$PlanOnly,
    [switch]$BuildOnly,
    [switch]$SaveOnly,
    [switch]$NewPackage
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$workflow = Join-Path $PSScriptRoot 'android_ota.py'
$configurationTool = Join-Path $PSScriptRoot 'build_options.py'
$env:PYTHONUTF8 = '1'
$env:PYTHONDONTWRITEBYTECODE = '1'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$targets = @(
    @{ Id = 'd008'; Name = 'D008' },
    @{ Id = 'd011'; Name = 'D011' },
    @{ Id = 'd013'; Name = 'D013' },
    @{ Id = 'd014'; Name = 'D014' }
)
if ($Target -and $Target -notin $targets.Id) { throw "Unknown build target: $Target" }
if ($PlanOnly -and -not $Target) { throw 'PlanOnly requires an explicit Target.' }

function Invoke-SourceConfiguration([string[]]$toolArguments) {
    $output = & python $configurationTool @toolArguments
    if ($LASTEXITCODE -ne 0) { throw '源码配置操作失败，请查看终端错误。' }
    return ($output | ConvertFrom-Json)
}

function Get-TargetDefaults([string]$id) {
    $source = Invoke-SourceConfiguration @('show', '--root', $repositoryRoot, '--product', $id)
    return @{ Capacity0p1Ah = $source.capacity_0p1ah; Chemistry = $source.chemistry;
        Cells = $source.cell_count; AfeModel = $source.afe_model;
        UpdateGroups = @($source.update_groups | Where-Object { $_ }); UpdateId = $source.update_id }
}

if ($SaveOnly) {
    if (-not $Target) { throw 'SaveOnly requires an explicit Target.' }
    $saveArguments = @('save', '--root', $repositoryRoot, '--product', $Target)
    if ($AfeModel) { $saveArguments += @('--afe-model', $AfeModel) }
    if ($Chemistry) { $saveArguments += @('--chemistry', $Chemistry) }
    if ($CellCount) { $saveArguments += @('--cell-count', "$CellCount") }
    if ($Capacity0p1Ah) { $saveArguments += @('--capacity-0p1ah', "$Capacity0p1Ah") }
    if ($PSBoundParameters.ContainsKey('UpdateGroups')) { $saveArguments += @('--update-groups') + $UpdateGroups }
    if ($NewPackage) { $saveArguments += '--new-package' }
    if ($UpdateId) { throw '更新标识由保存新参数包生成，不能通过命令行覆盖。' }
    Invoke-SourceConfiguration $saveArguments | ConvertTo-Json -Depth 5
    exit 0
}
if ($Chemistry -or $AfeModel -or $CellCount -or $Capacity0p1Ah -or $UpdateGroups.Count -or $UpdateId -or $NewPackage) {
    throw '构建阶段不接受参数覆盖；请先用 SaveOnly 或窗口保存配置。'
}

if (-not $Target) {
    # 本机仅记住项目和构建模式；参数和 OTA 策略始终读取源码。
    $hash = [System.Security.Cryptography.SHA256]::Create()
    try {
        $repoKey = ([BitConverter]::ToString($hash.ComputeHash(
            [Text.Encoding]::UTF8.GetBytes($repositoryRoot)))).Replace('-', '').Substring(0, 12)
    } finally { $hash.Dispose() }
    $settingsPath = Join-Path $env:LOCALAPPDATA "BmsBuildMenu/$repoKey.json"
    $settings = @{ Target = $targets[0].Id; Mode = $Mode }
    if (Test-Path -LiteralPath $settingsPath) {
        try {
            $saved = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
            if ($saved.Target -in $targets.Id) { $settings.Target = $saved.Target }
            if ($saved.Mode -in @('production', 'development')) { $settings.Mode = $saved.Mode }
        } catch { Write-Warning '上次窗口设置无法读取，本次使用源码默认。' }
    }
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    [System.Windows.Forms.Application]::EnableVisualStyles()
    $form = New-Object System.Windows.Forms.Form
    $form.Text = 'BMS 源码配置与编译'
    $form.ClientSize = New-Object System.Drawing.Size(590, 654)
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
    $updateBox.Text = '保存到源码的 OTA 更新策略（全不选则保留参数）'
    $updateBox.SetBounds(16, 180, 558, 124)
    $form.Controls.Add($updateBox)
    $groupChoices = @(
        @{ Id = 'sw'; Text = '软件保护'; X = 14; Y = 28 },
        @{ Id = 'afe'; Text = 'AFE 硬件保护'; X = 286; Y = 28 },
        @{ Id = 'business'; Text = '容量 / 加热 / 均衡'; X = 14; Y = 58 },
        @{ Id = 'soc'; Text = 'SOC 配置 / 电池类型'; X = 286; Y = 58 },
        @{ Id = 'soc_state'; Text = '更换电池：重置 SOC / 循环'; X = 14; Y = 88 },
        @{ Id = 'factory_reset'; Text = '恢复出厂：Config / State / Event'; X = 286; Y = 88 }
    )
    $updateChecks = @{}
    foreach ($choice in $groupChoices) {
        $check = New-Object System.Windows.Forms.CheckBox
        $check.Text = $choice.Text
        $check.SetBounds($choice.X, $choice.Y, 264, 28)
        $updateBox.Controls.Add($check)
        $updateChecks[$choice.Id] = $check
    }
    # 恢复出厂必须独占 OTA 更新选择，防止混合更新造成语义不明。
    $updateChecks['factory_reset'].Add_CheckedChanged({
        if (-not $script:loadingInputs -and $updateChecks['factory_reset'].Checked) {
            foreach ($key in @('sw', 'afe', 'business', 'soc', 'soc_state')) {
                $updateChecks[$key].Checked = $false
            }
        }
    })
    foreach ($key in @('sw', 'afe', 'business', 'soc', 'soc_state')) {
        $updateChecks[$key].Add_CheckedChanged({
            if (-not $script:loadingInputs -and $this.Checked) {
                $updateChecks['factory_reset'].Checked = $false
            }
        })
    }
    $note = New-Object System.Windows.Forms.Label
    $note.Text = "BIN 自带更新选择；无需读取客户板子编号，只执行一次。`r`n换串数或电池类型须更新四组；接线须匹配 AFE 手册。`r`n普通更新保留校准、SN、事件；恢复出厂将全部清空并重置为新固件默认。"
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
    $newPackageCheck = New-Object System.Windows.Forms.CheckBox
    $newPackageCheck.Text = '保存时制作新参数更新包（重新固定更新标识）'
    $newPackageCheck.SetBounds(16, 464, 558, 28)
    $form.Controls.Add($newPackageCheck)
    $fixedId = New-Object System.Windows.Forms.Label
    $fixedId.SetBounds(16, 504, 558, 28)
    $form.Controls.Add($fixedId)
    $saveStatus = New-Object System.Windows.Forms.Label
    $saveStatus.Text = '参数修改后先保存到源码，再点击编译；Git 提交不是门禁。'
    $saveStatus.SetBounds(16, 608, 558, 40)
    $form.Controls.Add($saveStatus)
    $parameterEditorButton = New-Object System.Windows.Forms.Button
    $parameterEditorButton.Text = '保护参数 / AFE 位配置'
    $parameterEditorButton.SetBounds(350, 98, 216, 30)
    $parameterEditorButton.Add_Click({
        try {
            $inputConfiguration = Get-WindowConfiguration
            $source = Invoke-SourceConfiguration @('show', '--root', $repositoryRoot,
                '--product', $inputConfiguration.Product)
            Assert-WindowConfigurationSaved $inputConfiguration $source
            & python -X utf8 -B (Join-Path $PSScriptRoot 'configure_parameters.py') --product $inputConfiguration.Product
            if ($LASTEXITCODE -ne 0) { throw '参数编辑器未正常结束；请检查终端。' }
            Update-TargetInputs
        } catch { $saveStatus.Text = "不能打开参数编辑器：$($_.Exception.Message)" }
    })
    $form.Controls.Add($parameterEditorButton)
    $script:loadingInputs = $false
    $script:targetInputsValid = $false
    function Select-LinkedGroups {
        # 出厂包已覆盖整套默认值，参数联动不能把用户选中的出厂策略改成普通更新。
        if (-not $script:loadingInputs -and -not $updateChecks['factory_reset'].Checked) {
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
            $chemical = $defaults.Chemistry
            $capacity = $defaults.Capacity0p1Ah
            $cells = $defaults.Cells
            $model = $defaults.AfeModel
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
            foreach ($choice in $groupChoices) { $updateChecks[$choice.Id].Checked = $choice.Id -in $defaults.UpdateGroups }
            $fixedId.Text = if ($defaults.UpdateId) { "固定更新标识：$($defaults.UpdateId)" }
                elseif ($defaults.UpdateGroups.Count) { '已选择更新组，请保存以固定新标识。' }
                else { '源码策略：保留参数；无更新标识' }
            $script:targetInputsValid = $true
        } catch {
            [void][System.Windows.Forms.MessageBox]::Show("配置读取失败：$_", 'BMS')
        } finally { $script:loadingInputs = $false }
    }
    $afeList.Add_SelectedIndexChanged({ Update-CellRange; Select-LinkedGroups })
    $productList.Add_SelectedIndexChanged({ Update-TargetInputs })
    $chemistryList.Add_SelectedIndexChanged({ Update-CellRange; Select-LinkedGroups })
    $cellInput.Add_ValueChanged({ Select-LinkedGroups })
    $capacityInput.Add_ValueChanged({
        if (-not $script:loadingInputs -and -not $updateChecks['factory_reset'].Checked) {
            $updateChecks['business'].Checked = $true
        }
    })
    function Get-WindowConfiguration {
        $id = $targets[$productList.SelectedIndex].Id
        return @{ Product = $id; Chemistry = $(if ($chemistryList.SelectedIndex -eq 1) { 'nmc' } else { 'lfp' });
            Cells = [int]$cellInput.Value; Capacity = [int]($capacityInput.Value * 10);
            Afe = $(if ($id -eq 'd008') { 'dvc1124' } elseif ($afeList.SelectedIndex -eq 1) { 'sh3673520' } else { 'sh3673510' });
            Groups = @($groupChoices | Where-Object { $updateChecks[$_.Id].Checked } | ForEach-Object { $_.Id }) }
    }
    function Assert-WindowConfigurationSaved($inputConfiguration, $source) {
        if ($source.afe_model -ne $inputConfiguration.Afe -or
            $source.chemistry -ne $inputConfiguration.Chemistry -or
            $source.cell_count -ne $inputConfiguration.Cells -or
            $source.capacity_0p1ah -ne $inputConfiguration.Capacity -or
            (@($source.update_groups) -join ',') -ne ($inputConfiguration.Groups -join ',')) {
            throw '窗口配置尚未保存，请先保存到源码。'
        }
    }
    $save = New-Object System.Windows.Forms.Button
    $save.Text = '保存到源码'
    $save.SetBounds(16, 560, 132, 32)
    $save.Add_Click({
        if (-not $script:targetInputsValid) { return }
        try {
            $inputConfiguration = Get-WindowConfiguration
            $saveArguments = @('save', '--root', $repositoryRoot, '--product', $inputConfiguration.Product,
                '--afe-model', $inputConfiguration.Afe, '--chemistry', $inputConfiguration.Chemistry,
                '--cell-count', "$($inputConfiguration.Cells)", '--capacity-0p1ah', "$($inputConfiguration.Capacity)",
                '--update-groups') + $inputConfiguration.Groups
            if ($newPackageCheck.Checked) { $saveArguments += '--new-package' }
            $savedConfiguration = Invoke-SourceConfiguration $saveArguments
            Update-TargetInputs
            $newPackageCheck.Checked = $false
            $saveStatus.Text = if ($savedConfiguration.changed_files.Count) {
                '已写回源码，可以直接编译；建议保留 Git 差异用于追溯。'
            } else { '配置与源码一致，可以直接编译。' }
        } catch { $saveStatus.Text = "保存失败：$($_.Exception.Message)" }
    })
    $form.Controls.Add($save)
    $productList.SelectedIndex = [array]::IndexOf(@($targets.Id), $settings.Target)
    $start = New-Object System.Windows.Forms.Button
    $start.Text = '按源码编译'
    $start.SetBounds(282, 560, 156, 32)
    $start.Add_Click({
        if (-not $script:targetInputsValid) { return }
        try {
            $inputConfiguration = Get-WindowConfiguration
            if ($newPackageCheck.Checked) { throw '新参数更新包尚未保存，请先保存到源码。' }
            $source = Invoke-SourceConfiguration @('verify', '--root', $repositoryRoot,
                '--product', $inputConfiguration.Product)
            Assert-WindowConfigurationSaved $inputConfiguration $source
            $form.DialogResult = [System.Windows.Forms.DialogResult]::OK
            $form.Close()
        } catch { $saveStatus.Text = "不能编译：$($_.Exception.Message)" }
    })
    $form.Controls.Add($start)
    $cancel = New-Object System.Windows.Forms.Button
    $cancel.Text = '取消'
    $cancel.SetBounds(450, 560, 116, 32)
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
        [void][System.IO.Directory]::CreateDirectory((Split-Path -Parent $settingsPath))
        $settings | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $settingsPath -Encoding UTF8
    } finally { $form.Dispose() }
}

$commonArguments = @($workflow, '--target', $Target, '--mode', $Mode)
$buildArguments = $commonArguments + @('--build-only')
if ($Rebuild) { $buildArguments += '--rebuild' }
if ($PlanOnly) {
    @{ Configuration = "$($Mode):$Target"; Arguments = $buildArguments;
        SourceConfiguration = (Invoke-SourceConfiguration @('show', '--root', $repositoryRoot, '--product', $Target)) } | ConvertTo-Json -Depth 4
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
