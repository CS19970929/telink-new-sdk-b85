param(
    [string]$RepositoryRoot,
    [string]$SettingsRoot,
    [ValidateSet('defaults', 'saved')][string]$Scenario
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$repositoryRoot = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$env:LOCALAPPDATA = $SettingsRoot
$global:menuTestChecks = 0
$global:menuTestProfiles = @{}
if ($Scenario -eq 'saved') {
    $global:menuTestProfiles = @{
        d008 = @{ AfeModel = 'dvc1124'; CellCount = 23; Chemistry = 'nmc'; Capacity0p1Ah = 1234 }
        d011 = @{ AfeModel = 'sh3673520'; CellCount = 20; Chemistry = 'nmc'; Capacity0p1Ah = 6553 }
        d013 = @{ AfeModel = 'sh3673510'; CellCount = 4; Chemistry = 'lfp'; Capacity0p1Ah = 1 }
        d014 = @{ AfeModel = 'sh3673520'; CellCount = 19; Chemistry = 'nmc'; Capacity0p1Ah = 400 }
    }
    $hash = [System.Security.Cryptography.SHA256]::Create()
    try {
        $repoKey = ([BitConverter]::ToString($hash.ComputeHash(
            [Text.Encoding]::UTF8.GetBytes($repositoryRoot)))).Replace('-', '').Substring(0, 12)
    } finally { $hash.Dispose() }
    $settingsPath = Join-Path $SettingsRoot "BmsBuildMenu/$repoKey.json"
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $settingsPath))
    @{ Target = 'd008'; Mode = 'development'; Profiles = $global:menuTestProfiles } |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $settingsPath -Encoding UTF8
}

function Assert-Menu([bool]$condition, [string]$message) {
    $global:menuTestChecks++
    if (-not $condition) { throw $message }
}

# 使用真实窗口和事件；只替换最后的 Python 调用，避免生成 BIN 或发送 OTA。
function global:python {
    Write-Output ('CALL:' + (ConvertTo-Json -InputObject @($args) -Compress))
    $global:LASTEXITCODE = 0
}
Add-Type -AssemblyName System.Windows.Forms
if (-not [System.Environment]::UserInteractive) {
    [Console]::WriteLine('UI_SKIP:interactive desktop required')
    exit 0
}
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 100
$timer.Add_Tick({
    $form = [System.Windows.Forms.Application]::OpenForms |
        Where-Object { $_.Text -eq 'BMS 单项目配置与编译' } | Select-Object -First 1
    if ($null -eq $form) { return }
    $timer.Stop()
    try {
        $products = @('d008', 'd011', 'd013', 'd014')
        $productList = $form.Controls | Where-Object { $_.Location.X -eq 136 -and $_.Location.Y -eq 16 }
        $afeList = $form.Controls | Where-Object { $_.Location.X -eq 410 -and $_.Location.Y -eq 16 }
        $modeList = $form.Controls | Where-Object { $_.Location.X -eq 136 -and $_.Location.Y -eq 56 }
        $cellInput = $form.Controls | Where-Object { $_.Location.X -eq 410 -and $_.Location.Y -eq 56 }
        $chemistryList = $form.Controls | Where-Object { $_.Location.X -eq 136 -and $_.Location.Y -eq 96 }
        $capacityInput = $form.Controls | Where-Object { $_.Location.X -eq 136 -and $_.Location.Y -eq 136 }
        $updateBox = $form.Controls | Where-Object { $_ -is [System.Windows.Forms.GroupBox] }
        $checks = @($updateBox.Controls | Where-Object { $_ -is [System.Windows.Forms.CheckBox] })
        $clean = $form.Controls | Where-Object { $_ -is [System.Windows.Forms.CheckBox] -and $_.Location.Y -eq 400 }
        $autoOta = $form.Controls | Where-Object { $_ -is [System.Windows.Forms.CheckBox] -and $_.Location.Y -eq 432 }
        Assert-Menu ($productList.Items.Count -eq 4 -and $checks.Count -eq 5) 'product/update controls missing'
        Assert-Menu ($capacityInput.Minimum -eq 0.1 -and $capacityInput.Maximum -eq 655.3) 'capacity bounds'
        Assert-Menu ($modeList.SelectedIndex -eq $(if ($Scenario -eq 'saved') { 0 } else { 1 })) 'mode restoration'
        $configurations = 0
        foreach ($productIndex in 0..3) {
            $productList.SelectedIndex = $productIndex
            $id = $products[$productIndex]
            $expected = $global:menuTestProfiles[$id]
            if ($null -eq $expected) {
                $sourceDefaults = Get-TargetDefaults $id
                $expected = @{ CellCount = $sourceDefaults.Cells; Chemistry = $sourceDefaults.Chemistry;
                    AfeModel = $sourceDefaults.AfeModel; Capacity0p1Ah = $sourceDefaults.Capacity0p1Ah }
            }
            Assert-Menu $script:targetInputsValid "$id inputs invalid"
            Assert-Menu ($cellInput.Value -eq $expected.CellCount) "$id cells restoration"
            Assert-Menu ($capacityInput.Value -eq ([decimal]$expected.Capacity0p1Ah / 10)) "$id capacity restoration"
            Assert-Menu ($chemistryList.SelectedIndex -eq $(if ($expected.Chemistry -eq 'nmc') { 1 } else { 0 })) "$id chemistry restoration"
            Assert-Menu ($afeList.SelectedIndex -eq $(if ($expected.AfeModel -eq 'sh3673520') { 1 } else { 0 })) "$id AFE restoration"
            Assert-Menu (@($checks | Where-Object Checked).Count -eq 0) "$id unexpectedly selects update groups"
            Assert-Menu ($afeList.Enabled -eq ($id -ne 'd008')) "$id AFE selector capability"
            $modeList.SelectedIndex = 0
            $modeList.SelectedIndex = 1
            $models = if ($id -eq 'd008') { @(0) } else { @(0, 1) }
            foreach ($modelIndex in $models) {
                $afeList.SelectedIndex = $modelIndex
                foreach ($chemistryIndex in 0..1) {
                    $chemistryList.SelectedIndex = $chemistryIndex
                    $maximum = if ($id -eq 'd008') {
                        if ($chemistryIndex -eq 0) { 24 } else { 23 }
                    } else { if ($modelIndex -eq 0) { 10 } else { 20 } }
                    Assert-Menu ($cellInput.Minimum -eq 4 -and $cellInput.Maximum -eq $maximum) "$id cell range"
                    foreach ($cells in 4..$maximum) {
                        $cellInput.Value = $cells
                        Assert-Menu ($cellInput.Value -eq $cells) "$id cells $cells"
                        $configurations++
                    }
                    Assert-Menu (@($checks | Where-Object { $_.Checked -and $_.Location.Y -ne 88 }).Count -eq 4) "$id linked update groups"
                    Assert-Menu (-not ($checks | Where-Object { $_.Location.Y -eq 88 }).Checked) "$id SOC_STATE unexpectedly selected"
                }
            }
            foreach ($capacity in @([decimal]0.1, [decimal]11.6, [decimal]655.3)) {
                if ($capacityInput.Value -eq $capacity) { $capacityInput.Value = 1.0 }
                foreach ($check in $checks) { $check.Checked = $false }
                $capacityInput.Value = $capacity
                Assert-Menu (@($checks | Where-Object Checked).Count -eq 1) "$id capacity must select only BUSINESS"
                Assert-Menu ($checks | Where-Object { $_.Location.X -eq 14 -and $_.Location.Y -eq 58 }).Checked "$id BUSINESS missing"
            }
        }
        $productList.SelectedIndex = 0
        $chemistryList.SelectedIndex = 0
        $cellInput.Value = 24
        $chemistryList.SelectedIndex = 1
        Assert-Menu ($cellInput.Value -eq 23) 'D008 NMC must clamp 24 to 23 cells'
        $productList.SelectedIndex = 3
        $afeList.SelectedIndex = 1
        $cellInput.Value = 20
        $afeList.SelectedIndex = 0
        Assert-Menu ($cellInput.Value -eq 10) 'SH3510 must clamp 20 to 10 cells'
        [Console]::WriteLine('UI_RESULT:' + (@{ Scenario = $Scenario; Checks = $global:menuTestChecks;
            Configurations = $configurations } | ConvertTo-Json -Compress))
        if ($Scenario -eq 'defaults') {
            $form.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
            $form.Close()
        } else {
            $modeList.SelectedIndex = 0
            $afeList.SelectedIndex = 1
            $chemistryList.SelectedIndex = 1
            $cellInput.Value = 19
            $capacityInput.Value = 40
            foreach ($check in $checks) { $check.Checked = $true }
            $clean.Checked = $true
            $autoOta.Checked = $true
            $form.AcceptButton.PerformClick()
        }
    } catch {
        [Console]::WriteLine('UI_FAIL:' + $_.Exception.Message)
        $form.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
        $form.Close()
    }
})
$timer.Start()
& (Join-Path $repositoryRoot 'bms_tools/select_firmware_build.ps1')
