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
    private readonly List<View> _pages = new();
    private readonly Dictionary<ProtectionParameterRow, EditText> _protectionInputs = new();
    private readonly Dictionary<AfeParameterRow, EditText> _afeInputs = new();
    private readonly List<Button> _actionButtons = new();

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
    private TextView? _monitorStatus;
    private TextView? _firmwareInboxStatus;
    private TextView? _logView;
    private LinearLayout? _scanResults;
    private LinearLayout? _protectionRows;
    private LinearLayout? _afeRows;
    private LinearLayout? _firmwareInbox;
    private EditText? _macInput;
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
    private EditText? _firmwareInput;
    private EditText? _expectedSerialInput;
    private Button? _connectButton;
    private Button? _disconnectButton;
    private Button? _otaButton;

    private int Dp(int value) => (int)(value * Resources!.DisplayMetrics!.Density + 0.5f);

    private void BuildUi() => BuildMobileUi();

    private View BuildOverviewPage() => BuildBatteryPage();

    private View BuildProtectionPage() => BuildMobileProtectionPage();

    private View BuildParameterPage()
    {
        LinearLayout page = Page();
        page.AddView(ActionButton("‹ 返回设备", () => { ShowPage(DevicePage); return Task.CompletedTask; }, false));
        page.AddView(SectionTitle("SOC 校正"));
        page.AddView(Note("输入目标电量百分比，保存后读取设备确认。"));
        _socInput = LabeledInput(page, "目标 SOC / %", "");
        page.AddView(ActionButton("保存 SOC 并核对", SaveSocAsync));
        page.AddView(SectionTitle("业务参数"));
        page.AddView(Note("先读取设备当前参数。修改后保存并核对；设备序列号只读。"));
        page.AddView(ActionButton("读取全部参数", ReadParametersAsync));
        _parameterStatus = DataBlock("未读取");
        page.AddView(_parameterStatus);
        _capacityInput = LabeledInput(page, "额定容量 / Ah", "");
        page.AddView(ActionButton("保存额定容量", SaveCapacityAsync));
        _cycleInput = LabeledInput(page, "循环次数", "");
        page.AddView(ActionButton("保存循环次数", SaveCycleAsync));
        _serialInput = LabeledInput(page, "设备 SN（只读）", "");
        _serialInput.Enabled = false;
        _heaterEnable = new CheckBox(this) { Text = "启用加热（仍受固件安全条件约束）" };
        page.AddView(_heaterEnable);
        _heaterStartInput = LabeledInput(page, "加热启动温度 / ℃", "");
        _heaterStopInput = LabeledInput(page, "加热停止温度 / ℃", "");
        page.AddView(ActionButton("保存整组加热参数", SaveHeaterAsync));
        page.AddView(SectionTitle("分组恢复默认"));
        foreach (var item in new[] { ("软件保护", (ushort)1), ("AFE 硬件保护", (ushort)2), ("业务参数", (ushort)3) })
        {
            ushort group = item.Item2;
            page.AddView(ActionButton("恢复默认：" + item.Item1, () => ResetParameterGroupAsync(group, item.Item1)));
        }
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
        page.AddView(ActionButton("取消自动测试", () => { CancelCurrentOperation(); return Task.CompletedTask; }, false));
        _testView = DataBlock("未测试");
        page.AddView(_testView);
        return Wrap(page);
    }

    private View BuildToolsPage()
    {
        LinearLayout page = Page();
        page.AddView(ActionButton("‹ 返回设备", () => { ShowPage(DevicePage); return Task.CompletedTask; }, false));
        page.AddView(SectionTitle("设备操作"));
        _nameInput = LabeledInput(page, "蓝牙名后缀", "");
        page.AddView(ActionButton("修改蓝牙名", RenameAsync));
        page.AddView(ActionButton("请求设备休眠", SleepAsync));

        page.AddView(SectionTitle("长期监控"));
        page.AddView(Note("每 5 秒保存一次实时快照，可导出 CSV。离开本页不会停止。"));
        var monitorButtons = Row();
        monitorButtons.AddView(ActionButton("开始", StartMonitorAsync));
        monitorButtons.AddView(ActionButton("停止", StopMonitorAsync));
        monitorButtons.AddView(ActionButton("导出 CSV", ExportMonitorAsync, false));
        page.AddView(monitorButtons);
        _monitorStatus = DataBlock("未开始");
        page.AddView(_monitorStatus);

        page.AddView(SectionTitle("OTA 升级"));
        page.AddView(Note("导入固件后，核对目标设备再开始升级。升级完成会自动重连验证。"));
        page.AddView(Note("无需命令行：在微信、浏览器或文件管理器中打开/分享 .bin，选择 BMS Tool 即可自动导入并进入本页。"));
        var inboxButtons = Row();
        inboxButtons.AddView(ActionButton("刷新收件箱", RefreshFirmwareInboxAsync, false));
        inboxButtons.AddView(ActionButton("导入 BIN", PickFirmware, false));
        page.AddView(inboxButtons);
        _firmwareInboxStatus = DataBlock("固件收件箱为空");
        page.AddView(_firmwareInboxStatus);
        _firmwareInbox = new LinearLayout(this) { Orientation = Orientation.Vertical };
        page.AddView(_firmwareInbox);
        _firmwareInput = LabeledInput(page, "固件路径", "");
        _expectedSerialInput = LabeledInput(page, "升级后期望 Serial（可选）", "");
        _otaButton = ActionButton("开始 OTA 并验证", StartOtaAsync);
        page.AddView(_otaButton);
        page.AddView(ActionButton("取消当前操作", () => { CancelCurrentOperation(); return Task.CompletedTask; }, false));

        page.AddView(SectionTitle("专业调试"));
        page.AddView(Note("原始写寄存器是工程功能，每次写入都要求二次确认并读回；不要用它绕过保护参数和 AFE 参数事务。"));
        _rawAddressInput = LabeledInput(page, "起始地址（如 0xD000）", "0xD000");
        _rawCountInput = LabeledInput(page, "读取数量（1..125）", "1");
        page.AddView(ActionButton("读取原始寄存器", ReadRawAsync));
        _rawValueInput = LabeledInput(page, "单寄存器写值（如 0x0001）", "0x0000");
        page.AddView(ActionButton("写入并读回", WriteRawAsync));
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
        for (int i = 0; i < _pages.Count; i++) _pages[i].Visibility = i == selected ? ViewStates.Visible : ViewStates.Gone;
        int active = selected == 0 ? 0 : selected == 1 ? 1 : selected == 5 ? 2 : 3;
        for (int i = 0; i < _navButtons.Count; i++) StyleSelection(_navButtons[i], i == active);
        if (_pageTitle is not null) _pageTitle.Text = selected switch
        { 0 => "我的电池", 1 => "保护参数", 2 => "业务参数", 3 => "高级诊断", 4 => "设备工具", 5 => "日志记录", _ => "我的设备" };
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
