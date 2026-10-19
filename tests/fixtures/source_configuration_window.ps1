param([Parameter(Mandatory = $true)][string]$RepositoryRoot)
$ErrorActionPreference = 'Stop'
$sourceScriptRoot = Join-Path $RepositoryRoot 'bms_tools'
$source = [IO.File]::ReadAllText((Join-Path $sourceScriptRoot 'select_firmware_build.ps1'))
$show = $source.IndexOf('$form.ShowDialog()')
if ($show -lt 0) { throw '找不到真实窗口入口' }
$boundary = $source.LastIndexOf('    try {', $show)
if ($boundary -lt 0) { throw '找不到窗口显示前的构造边界' }
# 执行完整控件构造和真实事件；不显示窗口、不进入保存/构建/发送部分。
$prefix = $source.Substring(0, $boundary).Replace('$PSScriptRoot', '$sourceScriptRoot')
$assertions = @'
    try {
        $productList.SelectedIndex = 3
        $updateChecks['factory_reset'].Checked = $true
        $capacityInput.Value = if ($capacityInput.Value -lt $capacityInput.Maximum) {
            $capacityInput.Value + [decimal]0.1
        } else { $capacityInput.Value - [decimal]0.1 }
        if (-not $updateChecks['factory_reset'].Checked) {
            throw 'REGRESSION: 修改容量取消了已选中的 factory_reset'
        }
        $cellInput.Value = if ($cellInput.Value -lt $cellInput.Maximum) {
            $cellInput.Value + 1
        } else { $cellInput.Value - 1 }
        if (-not $updateChecks['factory_reset'].Checked) {
            throw 'REGRESSION: 修改串数取消了已选中的 factory_reset'
        }
        $chemistryList.SelectedIndex = 1 - $chemistryList.SelectedIndex
        $afeList.SelectedIndex = 1 - $afeList.SelectedIndex
        if (-not $updateChecks['factory_reset'].Checked) {
            throw 'REGRESSION: 修改类型或 AFE 取消了已选中的 factory_reset'
        }
        $selected = @((Get-WindowConfiguration).Groups)
        if ($selected.Count -ne 1 -or $selected[0] -ne 'factory_reset') {
            throw '恢复出厂必须继续独占更新选择'
        }
        # 未保存的父窗口不得被子编辑器返回时的重新加载静默覆盖。
        $script:childOpened = $false
        $pythonExecutable = (Get-Command python).Source
        function python {
            if (@($args | Where-Object { "$_" -like '*configure_parameters.py' }).Count) {
                $script:childOpened = $true
                return
            }
            & $pythonExecutable @args
        }
        $onClick = [Windows.Forms.Button].GetMethod('OnClick',
            [Reflection.BindingFlags]'Instance,NonPublic')
        [void]$onClick.Invoke($parameterEditorButton, @([EventArgs]::Empty))
        if ($script:childOpened) { throw 'REGRESSION: 未保存的父窗口仍打开了独立参数编辑器' }
        if (-not $updateChecks['factory_reset'].Checked) { throw '未保存的恢复出厂选择被重新加载覆盖' }
        if ($saveStatus.Text -notmatch '先保存') { throw '没有提示先保存未提交的窗口配置' }
        # 用户明确勾选普通更新组时，仍可取消恢复出厂。
        $updateChecks['business'].Checked = $true
        if ($updateChecks['factory_reset'].Checked) { throw '显式改选普通组没有取消恢复出厂' }
        foreach ($group in @('sw','afe','business','soc','soc_state')) { $updateChecks[$group].Checked = $false }
        $capacityInput.Value = if ($capacityInput.Value -lt $capacityInput.Maximum) {
            $capacityInput.Value + [decimal]0.1
        } else { $capacityInput.Value - [decimal]0.1 }
        if (-not $updateChecks['business'].Checked) { throw '普通容量变更没有联动 BUSINESS' }
        $cellInput.Value = if ($cellInput.Value -lt $cellInput.Maximum) {
            $cellInput.Value + 1
        } else { $cellInput.Value - 1 }
        foreach ($group in @('sw','afe','business','soc')) {
            if (-not $updateChecks[$group].Checked) { throw ('普通串数变更遗漏 ' + $group) }
        }
        Write-Host 'PASS real WinForms events: factory_reset preserved, parent edits protected, explicit/ordinary updates retained'
    } finally { $form.Dispose() }
}
'@
& ([ScriptBlock]::Create($prefix + $assertions))
