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
    private TextView? _diagnosticView;
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

    private void BuildUi()
    {
        var root = new LinearLayout(this) { Orientation = Orientation.Vertical };
        root.SetBackgroundColor(Color.ParseColor("#F4F7F5"));

        var header = new LinearLayout(this) { Orientation = Orientation.Vertical };
        header.SetPadding(Dp(18), Dp(12), Dp(18), Dp(10));
        header.SetBackgroundColor(Color.ParseColor("#214F39"));
        var title = new TextView(this) { Text = "BMS Tool", TextSize = 22 };
        title.SetTextColor(Color.White);
        title.SetTypeface(null, TypefaceStyle.Bold);
        header.AddView(title);
        _connectionView = new TextView(this) { Text = "未连接 · Android BLE", TextSize = 13 };
        _connectionView.SetTextColor(Color.ParseColor("#D3E8DB"));
        header.AddView(_connectionView);
        root.AddView(header);

        _statusView = new TextView(this) { Text = "请选择设备", TextSize = 13 };
        _statusView.SetPadding(Dp(16), Dp(8), Dp(16), Dp(8));
        _statusView.SetBackgroundColor(Color.ParseColor("#E7F0EA"));
        root.AddView(_statusView);

        var navScroll = new HorizontalScrollView(this) { HorizontalScrollBarEnabled = false };
        var nav = new LinearLayout(this) { Orientation = Orientation.Horizontal };
        nav.SetPadding(Dp(6), Dp(4), Dp(6), Dp(4));
        foreach (string name in new[] { "概览", "保护", "参数", "诊断", "工具" })
        {
            int index = _navButtons.Count;
            var button = new Button(this) { Text = name, TextSize = 13 };
            button.SetMinWidth(Dp(74));
            button.Click += (_, _) => ShowPage(index);
            _navButtons.Add(button);
            nav.AddView(button, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WrapContent, Dp(48)));
        }
        navScroll.AddView(nav);
        root.AddView(navScroll);

        var pageHost = new FrameLayout(this);
        pageHost.AddView(BuildOverviewPage());
        pageHost.AddView(BuildProtectionPage());
        pageHost.AddView(BuildParameterPage());
        pageHost.AddView(BuildDiagnosticPage());
        pageHost.AddView(BuildToolsPage());
        root.AddView(pageHost, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MatchParent, 0, 1));
        SetContentView(root);
        ShowPage(0);
    }

    private View BuildOverviewPage()
    {
        LinearLayout page = Page();
        page.AddView(SectionTitle("设备连接"));
        _macInput = Input("BLE MAC", "A4:C1:38:00:30:CF");
        page.AddView(_macInput);
        var actions = Row();
        actions.AddView(ActionButton("扫描", ScanAsync));
        _connectButton = ActionButton("连接", ConnectSelectedAsync);
        actions.AddView(_connectButton);
        _disconnectButton = ActionButton("断开", DisposeConnectionAsync);
        actions.AddView(_disconnectButton);
        page.AddView(actions);
        _scanResults = new LinearLayout(this) { Orientation = Orientation.Vertical };
        page.AddView(_scanResults);

        page.AddView(SectionTitle("实时数据"));
        page.AddView(ActionButton("立即刷新", () => RefreshOverviewAsync(true)));
        _summaryView = DataBlock("电压、电流、SOC 等待连接后显示");
        page.AddView(_summaryView);
        page.AddView(SectionTitle("单体电压"));
        _cellsView = DataBlock("—");
        page.AddView(_cellsView);
        page.AddView(SectionTitle("系统与保护状态"));
        _stateView = DataBlock("—");
        page.AddView(_stateView);
        page.AddView(SectionTitle("设备身份"));
        _identityView = DataBlock("—");
        page.AddView(_identityView);
        return Wrap(page);
    }

    private View BuildProtectionPage()
    {
        LinearLayout page = Page();
        page.AddView(SectionTitle("软件三级保护"));
        page.AddView(Note("读取后按 13 个保护组显示一级、二级、三级、恢复和滤波；保存只写发生变化的完整组并逐组读回。"));
        var buttons = Row();
        buttons.AddView(ActionButton("读取", ReadProtectionAsync));
        buttons.AddView(ActionButton("保存修改", SaveProtectionAsync));
        page.AddView(buttons);
        _protectionStatus = DataBlock("未读取");
        page.AddView(_protectionStatus);
        _protectionRows = new LinearLayout(this) { Orientation = Orientation.Vertical };
        page.AddView(_protectionRows);

        page.AddView(SectionTitle("AFE 硬件保护"));
        page.AddView(Note("Requested 与 Effective 分开显示；保存使用固件授权事务，MTU=23 时自动使用分片提交。"));
        var afeButtons = Row();
        afeButtons.AddView(ActionButton("读取 AFE", ReadAfeAsync));
        afeButtons.AddView(ActionButton("原子应用", SaveAfeAsync));
        page.AddView(afeButtons);
        _afeStatus = DataBlock("未读取");
        page.AddView(_afeStatus);
        _afeRows = new LinearLayout(this) { Orientation = Orientation.Vertical };
        page.AddView(_afeRows);
        return Wrap(page);
    }

    private View BuildParameterPage()
    {
        LinearLayout page = Page();
        page.AddView(SectionTitle("D008 业务参数"));
        page.AddView(Note("参数写入前必须先读取能力窗口；写入后自动读回。SN 为工厂身份字段，Android 客户页面只读。"));
        page.AddView(ActionButton("读取全部参数", ReadParametersAsync));
        _parameterStatus = DataBlock("未读取");
        page.AddView(_parameterStatus);
        _capacityInput = LabeledInput(page, "额定容量 / Ah", "");
        page.AddView(ActionButton("保存额定容量", SaveCapacityAsync));
        _socInput = LabeledInput(page, "当前 SOC / %", "");
        page.AddView(ActionButton("设置并保存 SOC", SaveSocAsync));
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
        page.AddView(SectionTitle("BMS 诊断"));
        page.AddView(Note("快速诊断读取启动、存储、MOS、电流、SOC、功耗和保护；完整诊断额外采集 Trace、Evidence、原始帧并可导出 AI 诊断包。"));
        var diagButtons = Row();
        diagButtons.AddView(ActionButton("快速诊断", () => ReadDiagnosticsAsync(false)));
        diagButtons.AddView(ActionButton("完整诊断", () => ReadDiagnosticsAsync(true)));
        page.AddView(diagButtons);
        page.AddView(ActionButton("导出最近诊断 ZIP", ExportDiagnosticsAsync));
        _diagnosticView = DataBlock("未采集");
        page.AddView(_diagnosticView);
        page.AddView(SectionTitle("设备事件日志"));
        page.AddView(ActionButton("读取 100 条事件", ReadEventsAsync));
        _eventView = DataBlock("未读取");
        page.AddView(_eventView);
        page.AddView(SectionTitle("通信日志"));
        _logView = DataBlock("");
        _logView.SetTextIsSelectable(true);
        page.AddView(_logView);
        return Wrap(page);
    }

    private View BuildToolsPage()
    {
        LinearLayout page = Page();
        page.AddView(SectionTitle("设备操作"));
        _nameInput = LabeledInput(page, "蓝牙名后缀", "");
        page.AddView(ActionButton("修改蓝牙名", RenameAsync));
        page.AddView(ActionButton("请求设备休眠", SleepAsync));

        page.AddView(SectionTitle("长期监控"));
        page.AddView(Note("每 5 秒保存一次实时快照，可导出 CSV。离开本页不会停止。"));
        var monitorButtons = Row();
        monitorButtons.AddView(ActionButton("开始", StartMonitorAsync));
        monitorButtons.AddView(ActionButton("停止", StopMonitorAsync));
        monitorButtons.AddView(ActionButton("导出 CSV", ExportMonitorAsync));
        page.AddView(monitorButtons);
        _monitorStatus = DataBlock("未开始");
        page.AddView(_monitorStatus);

        page.AddView(SectionTitle("OTA 升级"));
        page.AddView(Note("仅接受通过 Telink marker、尺寸和 CRC 预检的 BIN。OTA_SUCCESS 后会重连并核对 Serial；不能只凭重新连上判定成功。"));
        page.AddView(Note("日常测试可由 android-ota-test.ps1 自动导入固件收件箱；App 无需每轮重新安装。"));
        var inboxButtons = Row();
        inboxButtons.AddView(ActionButton("刷新收件箱", RefreshFirmwareInboxAsync));
        inboxButtons.AddView(ActionButton("导入 BIN", PickFirmware));
        page.AddView(inboxButtons);
        _firmwareInboxStatus = DataBlock("固件收件箱为空");
        page.AddView(_firmwareInboxStatus);
        _firmwareInbox = new LinearLayout(this) { Orientation = Orientation.Vertical };
        page.AddView(_firmwareInbox);
        _firmwareInput = LabeledInput(page, "固件路径", "");
        _expectedSerialInput = LabeledInput(page, "升级后期望 Serial（可选）", "");
        _otaButton = ActionButton("开始 OTA 并验证", StartOtaAsync);
        page.AddView(_otaButton);
        page.AddView(ActionButton("取消当前操作", () => { CancelCurrentOperation(); return Task.CompletedTask; }));

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
        page.SetPadding(Dp(14), Dp(8), Dp(14), Dp(30));
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
        var view = new TextView(this) { Text = text, TextSize = 18 };
        view.SetTypeface(null, TypefaceStyle.Bold);
        view.SetTextColor(Color.ParseColor("#173A2A"));
        view.SetPadding(0, Dp(14), 0, Dp(7));
        return view;
    }

    private TextView Note(string text)
    {
        var view = new TextView(this) { Text = text, TextSize = 13 };
        view.SetTextColor(Color.ParseColor("#53645B"));
        view.SetPadding(0, 0, 0, Dp(7));
        return view;
    }

    private TextView DataBlock(string text)
    {
        var view = new TextView(this) { Text = text, TextSize = 14 };
        view.SetTextColor(Color.ParseColor("#17211B"));
        view.SetBackgroundColor(Color.White);
        view.SetPadding(Dp(12), Dp(10), Dp(12), Dp(10));
        var lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MatchParent, ViewGroup.LayoutParams.WrapContent);
        lp.SetMargins(0, Dp(3), 0, Dp(7));
        view.LayoutParameters = lp;
        return view;
    }

    private EditText Input(string hint, string value)
    {
        var input = new EditText(this) { Hint = hint, Text = value, TextSize = 14 };
        input.SetSingleLine(true);
        input.ImeOptions = ImeAction.Done;
        return input;
    }

    private EditText LabeledInput(LinearLayout page, string label, string value)
    {
        page.AddView(Note(label));
        var input = Input(label, value);
        page.AddView(input);
        return input;
    }

    private Button ActionButton(string text, Func<Task> action)
    {
        var button = new Button(this) { Text = text, TextSize = 13 };
        button.Click += async (_, _) => await action();
        _actionButtons.Add(button);
        return button;
    }

    private void ShowPage(int selected)
    {
        for (int i = 0; i < _pages.Count; i++) _pages[i].Visibility = i == selected ? ViewStates.Visible : ViewStates.Gone;
        for (int i = 0; i < _navButtons.Count; i++)
        {
            _navButtons[i].SetTextColor(Color.ParseColor(i == selected ? "#FFFFFF" : "#244A36"));
            _navButtons[i].SetBackgroundColor(Color.ParseColor(i == selected ? "#2E6B4B" : "#EEF3EF"));
        }
    }

    private void SetBusy(bool busy) => RunOnUiThread(() =>
    {
        foreach (Button button in _actionButtons) button.Enabled = !busy;
        if (_disconnectButton is not null) _disconnectButton.Enabled = true;
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
