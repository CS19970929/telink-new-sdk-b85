using Android.Content;
using Android.Graphics;
using Android.Views;
using Android.Views.InputMethods;
using Android.Widget;
using BmsTool.Windows;

namespace BmsTool.Android;

public sealed partial class MainActivity
{
    private readonly List<Button> _navButtons = new();
    private readonly List<int> _navDestinations = new();
    private readonly List<View> _pages = new();
    private readonly Dictionary<ProtectionParameterRow, EditText> _protectionInputs = new();
    private readonly Dictionary<AfeParameterRow, EditText> _afeInputs = new();
    private readonly List<Button> _actionButtons = new();
    private readonly HashSet<Button> _busyAllowedButtons = new();

    private TextView? _statusView;
    private TextView? _connectionView;
    private TextView? _identityView;
    private TextView? _summaryView;
    private TextView? _cellsView;
    private TextView? _stateView;
    private TextView? _protectionStatus;
    private TextView? _afeStatus;
    private TextView? _parameterStatus;
    private TextView? _eventView;
    private TextView? _healthView;
    private TextView? _diagnosticView;
    private TextView? _testView;
    private TextView? _firmwareInboxStatus;
    private LinearLayout? _scanResults;
    private LinearLayout? _protectionRows;
    private LinearLayout? _afeRows;
    private LinearLayout? _firmwareInbox;
    private EditText? _capacityInput;
    private EditText? _socInput;
    private EditText? _cycleInput;
    private EditText? _serialInput;
    private EditText? _heaterStartInput;
    private EditText? _heaterStopInput;
    private CheckBox? _heaterEnable;
    private EditText? _nameInput;
    private EditText? _rawAddressInput;
    private EditText? _rawCountInput;
    private EditText? _rawValueInput;
    private EditText? _expectedSerialInput;
    private Button? _disconnectButton;
    private Button? _otaButton;
    private Button? _chargeMosButton;
    private Button? _dischargeMosButton;
    private Button? _serviceModeButton;
    private TextView? _chargeMosState;
    private TextView? _dischargeMosState;
    private TextView? _mosControlNote;
    private TextView? _serviceModeStatus;

    private int Dp(int value) => (int)(value * Resources!.DisplayMetrics!.Density + 0.5f);

    private void BuildUi() => BuildMobileUi();

    private View BuildOverviewPage() => BuildBatteryPage();

    private View BuildProtectionPage() => BuildMobileProtectionPage();

    private LinearLayout? _d008ParameterPanel;
    private View BuildParameterPage()
    {
        LinearLayout page = Page();
        page.AddView(ActionButton("‹ 返回设备", () => { ShowPage(DevicePage); return Task.CompletedTask; }, false));
        var d008=Page();_d008ParameterPanel=d008;
        d008.AddView(SectionTitle("整套电池类型切换"));
        _batteryTypeStatus = Note("请确认实际电芯类型，并先断开充电器和负载。");
        d008.AddView(_batteryTypeStatus);
        d008.AddView(ActionButton("读取当前电池类型", ReadBatteryTypeFromUiAsync));
        d008.AddView(ActionButton("一键切换磷酸铁锂", () => SwitchBatteryTypeFromUiAsync(1)));
        d008.AddView(ActionButton("一键切换三元锂", () => SwitchBatteryTypeFromUiAsync(2)));
        BuildSocProfileControls(d008);
        page.AddView(d008);
        page.AddView(SectionTitle("SOC 校正"));
        page.AddView(Note("输入目标电量百分比，保存后读取设备确认。"));
        _socInput = LabeledInput(page, "目标 SOC / %", "");
        page.AddView(ActionButton("保存 SOC 并核对", SaveSocAsync));
        BuildC060ParameterControls(page);
        BuildMobileCurrentCalibration(page);
        var legacy=Page();_legacyParameterPanel=legacy;
        legacy.AddView(SectionTitle("业务参数"));
        legacy.AddView(Note("先读取设备当前参数。修改后保存并核对；设备序列号只读。"));
        _parameterStatus = DataBlock("未读取");
        legacy.AddView(_parameterStatus);
        _capacityInput = LabeledInput(legacy, "额定容量 / Ah", "");
        legacy.AddView(ActionButton("保存额定容量", SaveCapacityAsync));
        _cycleInput = LabeledInput(legacy, "循环次数", "");
        legacy.AddView(ActionButton("保存循环次数", SaveCycleAsync));
        _serialInput = LabeledInput(legacy, "设备 SN（只读）", "");
        _serialInput.Enabled = false;
        _heaterEnable = new CheckBox(this) { Text = "启用加热（仍受固件安全条件约束）" };
        legacy.AddView(_heaterEnable);
        _heaterStartInput = LabeledInput(legacy, "加热启动温度 / ℃", "");
        _heaterStopInput = LabeledInput(legacy, "加热停止温度 / ℃", "");
        legacy.AddView(ActionButton("保存整组加热参数", SaveHeaterAsync));
        legacy.AddView(SectionTitle("分组恢复默认"));
        foreach (var item in new[] { ("软件保护", (ushort)1), ("AFE 硬件保护", (ushort)2), ("业务参数", (ushort)3) })
        {
            ushort group = item.Item2;
            legacy.AddView(ActionButton("恢复默认：" + item.Item1, () => ResetParameterGroupAsync(group, item.Item1)));
        }
        page.AddView(legacy);
        return Wrap(page);
    }

    private View BuildDiagnosticPage()
    {
        LinearLayout page = Page();
        page.AddView(ActionButton("‹ 返回设备", () => { ShowPage(DevicePage); return Task.CompletedTask; }, false));
        page.AddView(SectionTitle("高级诊断"));
        page.AddView(Note("快速诊断读取启动、存储、MOS、电流、SOC、功耗和保护；完整诊断额外采集 Trace、Evidence、原始帧并可导出 AI 诊断包。"));
        page.AddView(ActionButton("设备健康检查", ReadHealthAsync));
        _healthView = DataBlock("未检查");
        page.AddView(_healthView);
        var diagButtons = Row();
        diagButtons.AddView(ActionButton("快速诊断", () => ReadDiagnosticsAsync(false)));
        diagButtons.AddView(ActionButton("完整诊断", () => ReadDiagnosticsAsync(true)));
        page.AddView(diagButtons);
        page.AddView(ActionButton("导出最近诊断 ZIP", ExportDiagnosticsAsync, false));
        _diagnosticView = DataBlock("未采集");
        page.AddView(_diagnosticView);
        page.AddView(SectionTitle("自动测试"));
        page.AddView(Note("SOC 测试连续采样并检查范围、容量关系、ETA、Build ID 与跳变；诊断测试重复采集并检查快照、Trace、错误与 Build ID 一致性。报告会自动保存为 JSON。"));
        var testButtons = Row();
        testButtons.AddView(ActionButton("SOC 自动测试", () => RunAutomatedTestAsync("soc")));
        testButtons.AddView(ActionButton("诊断一致性测试", () => RunAutomatedTestAsync("diag")));
        page.AddView(testButtons);
        Button cancelTest = ActionButton("取消自动测试", () => { CancelCurrentOperation(); return Task.CompletedTask; }, false);
        _busyAllowedButtons.Add(cancelTest);
        page.AddView(cancelTest);
        _testView = DataBlock("未测试");
        page.AddView(_testView);
        return Wrap(page);
    }

    private View BuildToolsPage()
    {
        LinearLayout page = Page();
        page.AddView(ActionButton("‹ 返回电池", () => { ShowPage(BatteryPage); return Task.CompletedTask; }, false));
        var deviceTools = new LinearLayout(this) { Orientation = Orientation.Vertical };
        deviceTools.AddView(Note("蓝牙名后缀"));
        _nameInput = Input("蓝牙名后缀", "");
        deviceTools.AddView(_nameInput);
        deviceTools.AddView(ActionButton("修改蓝牙名", RenameAsync));
        deviceTools.AddView(ActionButton("请求设备休眠", SleepAsync));
        deviceTools.AddView(ActionButton("设备连接与设置  ›", () => { ShowPage(DevicePage); return Task.CompletedTask; }, false));
        AddFold(page, "设备操作", deviceTools);

        page.AddView(ActionButton("电池数据记录  ›", () => { ShowPage(LogsPage); return Task.CompletedTask; }, false));

        var debug = new LinearLayout(this) { Orientation = Orientation.Vertical };
        debug.AddView(Note("原始写寄存器是工程功能，每次写入都要求二次确认并读回；不要用它绕过保护参数和 AFE 参数事务。"));
        _rawAddressInput = Input("起始地址（如 0xD000）", "0xD000");
        _rawCountInput = Input("读取数量（1..125）", "1");
        debug.AddView(_rawAddressInput); debug.AddView(_rawCountInput);
        debug.AddView(ActionButton("读取原始寄存器", ReadRawAsync));
        _rawValueInput = Input("单寄存器写值（如 0x0001）", "0x0000");
        debug.AddView(_rawValueInput);
        debug.AddView(ActionButton("写入并读回", WriteRawAsync));
        AddFold(page, "专业调试", debug);
        return Wrap(page);
    }

    private LinearLayout Page()
    {
        var page = new LinearLayout(this) { Orientation = Orientation.Vertical };
        page.SetPadding(Dp(14), Dp(6), Dp(14), Dp(16));
        return page;
    }

    private View Wrap(LinearLayout page)
    {
        var scroll = new ScrollView(this) { FillViewport = true };
        scroll.AddView(page);
        _pages.Add(scroll);
        return scroll;
    }

    private LinearLayout Row()
    {
        var row = new LinearLayout(this) { Orientation = Orientation.Horizontal };
        row.SetGravity(GravityFlags.CenterVertical);
        return row;
    }

    private TextView SectionTitle(string text)
    {
        var view = Label(text, 18, Ink, true);
        view.SetPadding(0, Dp(16), 0, Dp(8));
        return view;
    }

    private TextView Note(string text)
    {
        var view = Label(text, 13, Muted);
        view.SetPadding(0, Dp(4), 0, Dp(8));
        return view;
    }

    private TextView DataBlock(string text)
    {
        var view = new TextView(this) { Text = text, TextSize = 14 };
        view.SetTextColor(Color.ParseColor(Ink));
        view.Background = Rounded("#FFFFFF");
        view.SetPadding(Dp(12), Dp(10), Dp(12), Dp(10));
        var lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MatchParent, ViewGroup.LayoutParams.WrapContent);
        lp.SetMargins(0, Dp(3), 0, Dp(7));
        view.LayoutParameters = lp;
        return view;
    }

    private EditText Input(string hint, string value)
    {
        var input = new EditText(this) { Hint = hint, Text = value, TextSize = 16 };
        input.SetTextColor(Color.ParseColor(Ink));
        input.SetPadding(Dp(12), Dp(10), Dp(12), Dp(10));
        input.Background = Rounded("#EAF0F3", 8);
        input.SetMinimumHeight(Dp(48));
        input.SetSingleLine(true);
        input.ImeOptions = ImeAction.Done;
        return input;
    }

    private EditText LabeledInput(LinearLayout page, string label, string value)
    {
        page.AddView(Note(label));
        var input = Input(label, value);
        if (label.Contains("温度") || label.Contains("容量") || label.Contains("次数") || label.Contains("SOC"))
            input.InputType = global::Android.Text.InputTypes.ClassNumber | global::Android.Text.InputTypes.NumberFlagDecimal | global::Android.Text.InputTypes.NumberFlagSigned;
        page.AddView(input);
        return input;
    }

    private Button ActionButton(string text, Func<Task> action, bool requiresConnection = true)
    {
        var button = MobileButton(text);
        button.Click += async (_, _) =>
        {
            try { await action(); }
            catch (Exception ex) { SetStatus(text + "失败：" + ex.Message, true); AppendLog(ex.ToString()); }
        };
        _actionButtons.Add(button);
        if (requiresConnection) _connectionRequiredButtons.Add(button);
        return button;
    }

    private void ShowPage(int selected)
    {
        if (selected == FirmwarePage && !AppEdition.IsInternal && !_serviceMode)
        {
            selected = DevicePage;
            SetStatus("请先进入服务设置", true);
        }
        if (_upgradeTargetView is not null)
            _upgradeTargetView.Text = string.IsNullOrWhiteSpace(_selectedMac) ? "请先扫描或扫码选择设备" : $"目标设备  {_selectedMac}";
        for (int i = 0; i < _pages.Count; i++) _pages[i].Visibility = i == selected ? ViewStates.Visible : ViewStates.Gone;
        int active = _navDestinations.IndexOf(selected);
        for (int i = 0; i < _navButtons.Count; i++) StyleSelection(_navButtons[i], i == active);
        if (_pageTitle is not null) _pageTitle.Text = selected switch
        { 0 => "我的电池", 1 => "保护参数", 2 => "业务参数", 3 => "高级诊断", 4 => "测试工具", 5 => "记录", 6 => "我的设备", FirmwarePage => "固件升级", _ => "单体电压" };
        if (GetSystemService(InputMethodService) is InputMethodManager keyboard)
            keyboard.HideSoftInputFromWindow(_pageTitle?.WindowToken, HideSoftInputFlags.None);
        CurrentFocus?.ClearFocus();
        UpdateBatteryFreshness();
    }

    private void SetBusy(bool busy) => RunOnUiThread(() =>
    {
        _uiBusy = busy;
        RefreshActionAvailability();
        foreach (EditText input in _protectionInputs.Values.Concat(_afeInputs.Values)) input.Enabled = !busy;
        UpdateBatteryFreshness();
    });

    private Task<bool> ConfirmAsync(string title, string message)
    {
        var tcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        RunOnUiThread(() =>
        {
            var builder = new AlertDialog.Builder(this);
            builder.SetTitle(title);
            builder.SetMessage(message);
            // Builder button callbacks were observed firing immediately on one Android vendor build.
            // Install the real handlers only after Show(), so merely creating the dialog can never
            // authorize a write or OTA operation.
            builder.SetNegativeButton("取消", (_, _) => { });
            builder.SetPositiveButton("确认", (_, _) => { });
            builder.SetOnCancelListener(new DialogCancelListener(() => tcs.TrySetResult(false)));
            AlertDialog? dialog = builder.Create();
            if (dialog is null)
            {
                tcs.TrySetException(new InvalidOperationException("无法创建确认对话框。"));
                return;
            }
            dialog.Show();
            AppendLog($"CONFIRM_SHOWN title='{title}'");
            dialog.GetButton((int)DialogButtonType.Negative)!.Click += (_, _) =>
            {
                AppendLog($"CONFIRM_CANCEL title='{title}'");
                tcs.TrySetResult(false);
                dialog.Dismiss();
            };
            dialog.GetButton((int)DialogButtonType.Positive)!.Click += (_, _) =>
            {
                AppendLog($"CONFIRM_ACCEPT title='{title}'");
                tcs.TrySetResult(true);
                dialog.Dismiss();
            };
        });
        return tcs.Task;
    }

    private sealed class DialogCancelListener(Action cancelled) : Java.Lang.Object, IDialogInterfaceOnCancelListener
    {
        public void OnCancel(IDialogInterface? dialog) => cancelled();
    }
}
