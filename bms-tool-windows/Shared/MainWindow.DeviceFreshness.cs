using System.Windows;
using System.Windows.Controls;

namespace BmsTool.Windows;

public partial class MainWindow
{
    // 1s轮询、单请求最长4s；容纳一次超时及485唤醒，约两次连续超时后清空。
    private static readonly TimeSpan DeviceDisplayTimeout = TimeSpan.FromSeconds(8);
    private bool _deviceDisplaysCleared;
    private bool _identityRefreshRequired = true;

    private bool CheckDeviceDisplayFreshness()
    {
        if (_integratedOwcTask is not null) return true;
        if (_bms?.LastSuccessfulResponseAge is not TimeSpan age || age >= DeviceDisplayTimeout)
        {
            ClearDeviceDisplays();
            return false;
        }
        _deviceDisplaysCleared = false;
        return true;
    }

    private void ClearDeviceDisplays()
    {
        if (_integratedOwcTask is null) PublishUnifiedReading(null);
        if (!_deviceDisplaysCleared)
        {
            _deviceClockClient = null;
            _deviceClockReadTick = null;
            CancelCurrentCalibrationForCommunicationTimeout();
            NoteMosGap("通信中断，设备显示已清空");
            AppendLog("设备显示清空：连续8秒无有效业务响应，或连接已断开/更换。", "COMM");
        }
        _deviceDisplaysCleared = true;
        _identityRefreshRequired = true;
        _customerLastDataUtc = DateTime.MinValue;
        _customerLastCellsRaw = string.Empty;
        foreach (string name in new[] {
            "PackVoltageText", "CurrentText", "SocText", "WorkStateText", "MosStateText", "SystemStateText",
            "ProtectionSummaryText", "ProtectionLevelsText", "TempsText", "CellExtremeText", "CapacityText", "CellsText",
            "DeviceClockText", "CustomerClockText", "IdentityText", "BtNameResultText", "CurrentSocParamText", "CurrentCycleParamText", "BmsParamResultText",
            "ProtectionStatusText", "DeviceSerialStatusText", "LastUpdateText", "SleepStatusText",
            "CustomerSohText", "CustomerRemainCapacityText", "CustomerFullCapacityText", "CustomerCycleText",
            "CustomerMaxTempText", "CustomerMinTempText", "CustomerMosTempText", "CustomerMaxCellText", "CustomerMinCellText",
            "CustomerAvgCellText", "CustomerDeltaCellText", "CustomerCellCountText", "CustomerSystemMonitorText",
            "CustomerAlarm1Text", "CustomerAlarm2Text", "CustomerAlarm3Text", "DebugRawStatusText",
            "FactoryPackText", "FactoryCurrentText", "FactorySocText", "FactorySohText", "FactoryCapacityText",
            "FactoryCycleText", "FactoryCellTempText", "FactoryMosTempText", "FactoryCellExtremeText", "FactoryUpdateText",
            "FactoryMosText", "FactoryEffectiveText", "FactoryReportText", "FactoryStateText",
            "FactoryConnectionText", "FactoryIdentityText", "FactoryParameterSummaryText", "FactorySystemText",
            "FactoryProtection1Text", "FactoryProtection2Text", "FactoryProtection3Text", "FactorySessionText",
            "FactoryInjectionText", "FactoryResultText", "FactoryRawText"
        }) if (FindName(name) is TextBlock text) text.Text = "—";
        CustomerSocText.Text = "SOC:—";
        CustomerSocFill.Height = 0;
        CustomerCellPanel.Children.Clear();
        if (FindName("FactoryCellPanel") is Panel cells) cells.Children.Clear();
        UpdateCustomerIdentityFooter();
        ResetMosControlUi();

        _protectionSnapshotClient = null;
        foreach (var row in _protectionRows) row.ClearReadback();
        ClearC060ParameterCapture();
        _hs3520Owner = null;
        _hs3520Words = null;
        _hs3520Grid.ItemsSource = null;
        foreach (var box in new[] { _hs3520Capacity, _hs3520Soc, _hs3520Cycles, _hs3520Serial }) box.Clear();
        _parameterCapture = null;
        _parameterProbedClient = null;
        _d015BasicGrid.ItemsSource = null;
        _d015OcvGrid.ItemsSource = null;
        foreach (var box in new[] { _parameterCapacity, _parameterSoc, _parameterCycle, DeviceSerialBox,
            NameSuffixBox, _parameterReadback, _parameterHeatStart, _parameterHeatStop,
            _parameterBalanceStart, _parameterBalanceDeltaStart, _parameterBalanceDeltaStop,
            _sleepLowMv, _sleepLowMinutes, _sleepNormalMv, _sleepNormalMinutes, _socOcvPoints }) box.Clear();
        _parameterHeatEnable.IsChecked = false;
        _parameterBusinessReadback.Text = _parameterStatus.Text = _parameterBalancePermission.Text = "—";
        _socProfile = null;
        _socCandidate = null;
        _socProfileStatus.Text = _batteryTypeStatus.Text = "—";
        _afeHardwareSource = null;
        _afeHardwareModel.ClearReadback();
        if (_afeHardwareStatus is not null) _afeHardwareStatus.Text = "—";
        if (_afeHardwareDeviceInfo is not null) _afeHardwareDeviceInfo.Text = "—";
        UpdateBusinessParameterControls();

        _sleepStatusClient = null;
        _diagCapture = null;
        _diagHealthReport = null;
        _diagProbedClient = null;
        foreach (var grid in new[] { _diagCurrent, _diagSoc, _diagPower, _diagProtection, _diagBoot,
            _diagStorage, _diagMos, _diagTrace, _diagHealth, _diagTests }) grid.ItemsSource = null;
        _diagStatus.Text = _currentCalibrationStatus.Text = "—";
        _socRecords.Clear(); _socChart.Children.Clear(); _socRecordStatus.Text = "—";
        _openWireRows.Clear(); _openWirePrevious = null; _openWireStatus.Text = "—";
        _mosRows.Clear(); _mosPrevious = null; _mosIdentity = null; _mosStatus.Text = "—";
        _runtimeLogs.Clear(); _runtimeLogCursor = null; _runtimeLogStatus.Text = "—";
        _deviceEventLogs.Clear();
        _longTermRecords.Clear();
        _longTermLastUpdateToken = string.Empty;
        if (_longTermStatus is not null) _longTermStatus.Text = "—";
        if (FindName("FactoryLogText") is TextBox factoryLog) factoryLog.Clear();
        if (_eventLogStatus is not null) _eventLogStatus.Text = "—";
#if BMS_FACTORY_APP
        _factorySteps.Clear();
#endif
        // 已落盘日志、连接设置及升级进度由各自流程保留；这里只清设备值与读回资格。
    }
}
