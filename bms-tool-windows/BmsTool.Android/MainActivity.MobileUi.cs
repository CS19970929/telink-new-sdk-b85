using Android.Content;
using Android.Content.PM;
using Android.Graphics;
using Android.Graphics.Drawables;
using Android.Views;
using Android.Widget;
using BmsTool.Windows;

namespace BmsTool.Android;

public sealed partial class MainActivity
{
    // Keep the existing subpage IDs used by firmware import intents.
    private const int BatteryPage = 0, ProtectionPage = 1, BusinessPage = 2, DiagnosticsPage = 3,
        ToolsPage = 4, LogsPage = 5, DevicePage = 6, CellsPage = 7;
    private const string Ink = "#172D3A", Muted = "#647784", Accent = "#126B66", Canvas = "#F3F6F8";
    private TextView? _socValue, _workState, _voltageValue, _currentValue, _capacityValue, _temperatureValue;
    private TextView? _freshnessView, _pageTitle;
    private LinearLayout? _cellGrid, _softwarePanel, _hardwarePanel, _serviceActions;
    private Button? _softwareTab, _hardwareTab, _saveSoftwareButton, _saveHardwareButton;
    private readonly HashSet<Button> _connectionRequiredButtons = new();
    private readonly Queue<string> _visibleLogLines = new();
    private bool _uiBusy, _logPaused;
    private DateTimeOffset? _lastBatteryUiUtc;
    private string? _displayedBatteryMac;

    private void BuildMobileUi()
    {
        var root = new LinearLayout(this) { Orientation = Orientation.Vertical };
        root.SetBackgroundColor(Color.ParseColor(Canvas));
        // Android 15+ forces edge-to-edge for this target SDK. Keep both system
        // bars and the keyboard outside the content; do not guess status-bar height.
        root.SetOnApplyWindowInsetsListener(new MobileInsetsListener());
        var header = new LinearLayout(this) { Orientation = Orientation.Vertical };
        header.SetPadding(Dp(20), Dp(12), Dp(20), Dp(10));
        header.SetBackgroundColor(Color.White);
        _pageTitle = Label("我的电池", 23, Ink, true);
        header.AddView(_pageTitle);
        _connectionView = Label("未连接 · 点击设备选择电池", 12, Muted);
        _connectionView.SetPadding(0, Dp(4), 0, 0);
        _connectionView.Click += (_, _) => ShowPage(DevicePage);
        header.AddView(_connectionView);
        root.AddView(header);
        _statusView = Label("连接电池后，数据每 5 秒更新", 12, Muted);
        _statusView.SetPadding(Dp(20), Dp(7), Dp(20), Dp(7));
        _statusView.SetMaxLines(2);
        root.AddView(_statusView);

        var host = new FrameLayout(this);
        host.AddView(BuildOverviewPage());
        host.AddView(BuildProtectionPage());
        host.AddView(BuildParameterPage());
        host.AddView(AppEdition.IsInternal ? BuildDiagnosticPage() : BuildUnavailablePage("高级诊断仅在测试版提供"));
        host.AddView(AppEdition.IsInternal ? BuildToolsPage() : BuildUnavailablePage("工程工具仅在测试版提供"));
        host.AddView(BuildMobileLogPage());
        host.AddView(BuildMobileDevicePage());
        host.AddView(BuildCellsPage());
        root.AddView(host, new LinearLayout.LayoutParams(-1, 0, 1));

        var nav = Row();
        nav.SetBackgroundColor(Color.White);
        nav.SetPadding(Dp(8), Dp(6), Dp(8), Dp(6));
        int[] destinations = AppEdition.IsInternal
            ? new[] { BatteryPage, ProtectionPage, LogsPage, ToolsPage }
            : new[] { BatteryPage, CellsPage, LogsPage, DevicePage };
        string[] names = AppEdition.IsInternal
            ? new[] { "电池", "保护", "日志", "工具" }
            : new[] { "电池", "单体", "记录", "设备" };
        for (int i = 0; i < names.Length; i++)
        {
            int destination = destinations[i];
            _navDestinations.Add(destination);
            var button = MobileButton(names[i]);
            button.Click += (_, _) => ShowPage(destination);
            _navButtons.Add(button);
            var lp = new LinearLayout.LayoutParams(0, Dp(50), 1);
            lp.SetMargins(Dp(3), 0, Dp(3), 0);
            nav.AddView(button, lp);
        }
        root.AddView(nav);
        SetContentView(root);
        if (OperatingSystem.IsAndroidVersionAtLeast(30))
        {
            int appearance = (int)(WindowInsetsControllerAppearance.LightStatusBars | WindowInsetsControllerAppearance.LightNavigationBars);
            Window?.InsetsController?.SetSystemBarsAppearance(appearance, appearance);
        }
        else
        {
#pragma warning disable CA1422
            if (Window is not null)
                Window.DecorView.SystemUiFlags = SystemUiFlags.LightStatusBar |
                    (OperatingSystem.IsAndroidVersionAtLeast(26) ? SystemUiFlags.LightNavigationBar : 0);
#pragma warning restore CA1422
        }
        root.RequestApplyInsets();
        ShowPage(BatteryPage);
        RefreshActionAvailability();
    }

    private View BuildUnavailablePage(string message)
    {
        var page = Page();
        page.AddView(Note(message));
        page.AddView(ActionButton("返回电池", () => { ShowPage(BatteryPage); return Task.CompletedTask; }, false));
        return Wrap(page);
    }

    private View BuildBatteryPage()
    {
        var page = Page();
        var hero = Card();
        hero.Background = Rounded("#E1F2EE", 20);
        hero.AddView(Label("剩余电量", 13, Accent));
        _socValue = Label("— %", 48, Accent, true);
        hero.AddView(_socValue);
        _workState = Label("等待连接电池", 15, Ink, true);
        hero.AddView(_workState);
        _freshnessView = Label("尚未读取数据", 12, Muted);
        _freshnessView.SetPadding(0, Dp(8), 0, Dp(8));
        hero.AddView(_freshnessView);
        var actions = Row();
        AddEqual(actions, ActionButton("选择设备", () => { ShowPage(DevicePage); return Task.CompletedTask; }, false));
        AddEqual(actions, ActionButton("刷新数据", () => RefreshOverviewAsync(true)));
        hero.AddView(actions);
        page.AddView(hero);
        var metrics = Row();
        AddEqual(metrics, Metric("总电压", out _voltageValue));
        AddEqual(metrics, Metric("电流 / A", out _currentValue));
        page.AddView(metrics);
        metrics = Row();
        AddEqual(metrics, Metric("剩余容量", out _capacityValue));
        AddEqual(metrics, Metric("电池温度", out _temperatureValue));
        page.AddView(metrics);
        page.AddView(BuildMosControlCard());
        page.AddView(SectionTitle("电池状态"));
        _stateView = DataBlock("等待连接后读取 MOS 与保护状态");
        page.AddView(_stateView);
        page.AddView(SectionTitle("单体电压"));
        _cellsView = Note("连接后显示单体电压、最高与最低串");
        page.AddView(_cellsView);
        page.AddView(ActionButton("查看全部单体  ›", () => { ShowPage(CellsPage); return Task.CompletedTask; }, false));
        page.AddView(SectionTitle("更多数据"));
        _summaryView = DataBlock("SOH、循环次数、容量与 MOS 温度等待更新");
        page.AddView(_summaryView);
        return Wrap(page);
    }

    private View BuildMosControlCard()
    {
        var card = Card();
        card.AddView(Label("充放电 MOS", 18, Ink, true));
        card.AddView(Note("“允许”只解除用户关闭许可；保护、采样资格和 AFE 仍决定实际是否开启。"));
        var states = Row();
        AddEqual(states, MosStatePanel("充电 MOS", out _chargeMosState));
        AddEqual(states, MosStatePanel("放电 MOS", out _dischargeMosState));
        card.AddView(states);
        var actions = Row();
        _chargeMosButton = ActionButton("读取控制状态", () => ToggleMosAllowedAsync(true));
        _dischargeMosButton = ActionButton("读取控制状态", () => ToggleMosAllowedAsync(false));
        AddEqual(actions, _chargeMosButton); AddEqual(actions, _dischargeMosButton);
        card.AddView(actions);
        _mosControlNote = Note("连接后读取用户许可、产品请求和 AFE 报告");
        card.AddView(_mosControlNote);
        return card;
    }

    private LinearLayout MosStatePanel(string title, out TextView state)
    {
        var panel = Card();
        panel.AddView(Label(title, 12, Muted));
        state = Label("未知", 20, Muted, true);
        state.SetPadding(0, Dp(6), 0, Dp(2));
        panel.AddView(state);
        return panel;
    }

    private View BuildCellsPage()
    {
        var page = Page();
        page.AddView(Note("按物理串号显示有效单体；未装串位 61001 不参与统计。最低、最高和均衡串会单独标记。"));
        page.AddView(ActionButton("刷新单体数据", () => RefreshOverviewAsync(true)));
        _cellGrid = new LinearLayout(this) { Orientation = Orientation.Vertical };
        page.AddView(_cellGrid);
        return Wrap(page);
    }

    private View BuildMobileProtectionPage()
    {
        var root = Page();
        root.AddView(ActionButton("‹ 返回设备", () => { ShowPage(DevicePage); return Task.CompletedTask; }, false));
        root.AddView(Note("整页均可上下滚动。切换页签只改变当前显示的保护组，不会写入设备。"));
        var tabs = Row();
        _softwareTab = MobileButton("软件保护");
        _hardwareTab = MobileButton("硬件保护");
        AddEqual(tabs, _softwareTab); AddEqual(tabs, _hardwareTab);
        root.AddView(tabs);
        _softwarePanel = Page();
        _softwarePanel.AddView(Note("先读取设备参数，再展开需要调整的保护组。保存后自动读回核对。"));
        _softwarePanel.AddView(ActionButton("读取软件保护", ReadProtectionAsync));
        _protectionStatus = DataBlock("尚未读取设备参数");
        _softwarePanel.AddView(_protectionStatus);
        _protectionRows = new LinearLayout(this) { Orientation = Orientation.Vertical };
        _softwarePanel.AddView(_protectionRows);
        root.AddView(_softwarePanel);
        _hardwarePanel = Page();
        _hardwarePanel.AddView(Note("设定值是保存的参数，生效值是硬件量化后的结果。两者可能不同。"));
        _hardwarePanel.AddView(ActionButton("读取硬件保护", ReadAfeAsync));
        _afeStatus = DataBlock("尚未读取设备参数");
        _hardwarePanel.AddView(_afeStatus);
        _afeRows = new LinearLayout(this) { Orientation = Orientation.Vertical };
        _hardwarePanel.AddView(_afeRows);
        root.AddView(_hardwarePanel);
        _saveSoftwareButton = ActionButton("保存修改 · 0 项", SaveProtectionAsync);
        _saveHardwareButton = ActionButton("保存修改 · 0 项", SaveAfeAsync);
        root.AddView(_saveSoftwareButton); root.AddView(_saveHardwareButton);
        void Select(bool hardware)
        {
            _softwarePanel.Visibility = hardware ? ViewStates.Gone : ViewStates.Visible;
            _hardwarePanel.Visibility = hardware ? ViewStates.Visible : ViewStates.Gone;
            _saveSoftwareButton.Visibility = hardware ? ViewStates.Gone : ViewStates.Visible;
            _saveHardwareButton.Visibility = hardware ? ViewStates.Visible : ViewStates.Gone;
            StyleSelection(_softwareTab, !hardware); StyleSelection(_hardwareTab, hardware);
        }
        _softwareTab.Click += (_, _) => Select(false);
        _hardwareTab.Click += (_, _) => Select(true);
        Select(false);
        return Wrap(root);
    }

    private View BuildMobileLogPage()
    {
        var root = Page();
        var tabs = Row();
        var eventsTab = MobileButton("设备事件");
        var commTab = MobileButton("通信日志");
        AddEqual(tabs, eventsTab); AddEqual(tabs, commTab);
        root.AddView(tabs);
        var host = new FrameLayout(this);
        var page = Page();
        page.AddView(Note("查看设备记录的事件及相邻记录间隔。"));
        page.AddView(ActionButton("读取设备事件", ReadEventsAsync));
        _eventView = DataBlock("尚未读取设备事件");
        _eventView.SetTextIsSelectable(true);
        page.AddView(_eventView);
        var eventsScroll = new ScrollView(this); eventsScroll.AddView(page);
        host.AddView(eventsScroll);
        page = Page();
        var buttons = Row();
        var pause = ActionButton("暂停显示", () => Task.CompletedTask, false);
        pause.Click += (_, _) => { _logPaused = !_logPaused; pause.Text = _logPaused ? "继续显示" : "暂停显示"; RenderCommunicationLog(); };
        AddEqual(buttons, pause);
        AddEqual(buttons, ActionButton("清空显示", () => { _visibleLogLines.Clear(); _logView!.Text = "暂无通信记录"; return Task.CompletedTask; }, false));
        AddEqual(buttons, ActionButton("复制", () =>
        {
            if (GetSystemService(ClipboardService) is ClipboardManager clipboard)
                clipboard.PrimaryClip = ClipData.NewPlainText("BMS 通信日志", string.Join("\n", _visibleLogLines));
            SetStatus("通信日志已复制"); return Task.CompletedTask;
        }, false));
        page.AddView(buttons);
        page.AddView(Note("页面保留最近 200 行；完整通信日志继续写入本机日志文件。暂停显示不影响采集。"));
        _logView = DataBlock("暂无通信记录");
        _logView.TextSize = 12;
        _logView.Typeface = Typeface.Monospace;
        _logView.SetTextIsSelectable(true);
        page.AddView(_logView);
        var commScroll = new ScrollView(this); commScroll.AddView(page);
        host.AddView(commScroll);
        root.AddView(host, new LinearLayout.LayoutParams(-1, 0, 1));
        void Select(bool communication)
        {
            eventsScroll.Visibility = communication ? ViewStates.Gone : ViewStates.Visible;
            commScroll.Visibility = communication ? ViewStates.Visible : ViewStates.Gone;
            StyleSelection(eventsTab, !communication); StyleSelection(commTab, communication);
            if (communication) RenderCommunicationLog();
        }
        eventsTab.Click += (_, _) => Select(false);
        commTab.Click += (_, _) => Select(true);
        Select(false);
        _pages.Add(root);
        return root;
    }

    private View BuildMobileDevicePage()
    {
        var page = Page();
        page.AddView(SectionTitle("当前设备"));
        _identityView = DataBlock("尚未连接设备");
        _identityView.SetTextIsSelectable(true);
        page.AddView(_identityView);
        _disconnectButton = ActionButton("断开连接", DisposeConnectionAsync);
        page.AddView(_disconnectButton);
        page.AddView(SectionTitle("服务与设备设置"));
        if (AppEdition.IsInternal)
        {
            page.AddView(ActionButton("业务参数与 SOC 校正  ›", () => { ShowPage(BusinessPage); return Task.CompletedTask; }, false));
            page.AddView(ActionButton("测试工具  ›", () => { ShowPage(ToolsPage); return Task.CompletedTask; }, false));
            page.AddView(ActionButton("高级诊断  ›", () => { ShowPage(DiagnosticsPage); return Task.CompletedTask; }, false));
        }
        else
        {
            _serviceModeButton = ActionButton("进入服务设置", EnterServiceModeAsync);
            page.AddView(_serviceModeButton);
            _serviceModeStatus = Note("普通模式 · 保护参数与校正入口已收起");
            page.AddView(_serviceModeStatus);
            _serviceActions = Page();
            _serviceActions.AddView(ActionButton("业务参数、SOC 与电池类型  ›", () => { ShowPage(BusinessPage); return Task.CompletedTask; }, false));
            _serviceActions.AddView(ActionButton("软件 / 硬件保护参数  ›", () => { ShowPage(ProtectionPage); return Task.CompletedTask; }, false));
            _serviceActions.Visibility = ViewStates.Gone;
            page.AddView(_serviceActions);
        }
        var connection = Page();
        connection.AddView(Note("扫描附近设备，点击设备名称即可连接；当前连接不会因展开此区域而断开。"));
        connection.AddView(ActionButton("扫描附近的电池", ScanAsync, false));
        _scanResults = new LinearLayout(this) { Orientation = Orientation.Vertical };
        connection.AddView(_scanResults);
        var manual = new LinearLayout(this) { Orientation = Orientation.Vertical };
        _macInput = LabeledInput(manual, "设备 MAC", "");
        _connectButton = ActionButton("连接指定设备", ConnectSelectedAsync, false);
        manual.AddView(_connectButton);
        AddFold(connection, "手动连接 / 上次设备", manual);
        AddFold(page, "连接其他电池", connection);
        page.AddView(SectionTitle("应用信息"));
        string appVersion = PackageManager?.GetPackageInfo(PackageName!, PackageInfoFlags.MetaData)?.VersionName ?? "unknown";
        page.AddView(Note($"{AppEdition.DisplayName} {appVersion}\nApp Commit {BuildIdentity.Display}"));
        return Wrap(page);
    }

    private TextView Label(string text, float size, string color = Ink, bool bold = false)
    {
        var view = new TextView(this) { Text = text, TextSize = size };
        view.SetTextColor(Color.ParseColor(color));
        if (bold) view.SetTypeface(null, TypefaceStyle.Bold);
        return view;
    }

    private GradientDrawable Rounded(string color, int radius = 14)
    {
        var shape = new GradientDrawable();
        shape.SetColor(Color.ParseColor(color)); shape.SetCornerRadius(Dp(radius));
        return shape;
    }

    private LinearLayout Card()
    {
        var card = new LinearLayout(this) { Orientation = Orientation.Vertical };
        card.SetPadding(Dp(16), Dp(14), Dp(16), Dp(14));
        card.Background = Rounded("#FFFFFF");
        var lp = new LinearLayout.LayoutParams(-1, -2); lp.SetMargins(0, Dp(5), 0, Dp(5));
        card.LayoutParameters = lp;
        return card;
    }

    private LinearLayout Metric(string title, out TextView value)
    {
        var card = Card(); card.AddView(Label(title, 12, Muted));
        value = Label("—", 25, Ink, true); value.SetPadding(0, Dp(8), 0, 0); card.AddView(value);
        return card;
    }

    private void AddEqual(LinearLayout row, View child)
    {
        var lp = new LinearLayout.LayoutParams(0, -2, 1);
        lp.SetMargins(Dp(3), Dp(4), Dp(3), Dp(4)); row.AddView(child, lp);
    }

    private Button MobileButton(string text)
    {
        var button = new Button(this) { Text = text, TextSize = 14 };
        button.SetAllCaps(false); button.SetMinWidth(0); button.SetMinimumWidth(0);
        button.SetMinHeight(Dp(48)); button.SetMinimumHeight(Dp(48));
        button.SetPadding(Dp(10), Dp(8), Dp(10), Dp(8));
        button.SetTextColor(Color.ParseColor(Accent)); button.Background = Rounded("#E4F0EE", 12);
        var lp = new LinearLayout.LayoutParams(-1, -2); lp.SetMargins(0, Dp(5), 0, Dp(5));
        button.LayoutParameters = lp;
        return button;
    }

    private void StyleSelection(Button button, bool selected)
    {
        button.SetTextColor(Color.ParseColor(selected ? "#FFFFFF" : Muted));
        button.Background = Rounded(selected ? Accent : "#F0F4F6", 12);
    }

    private void AddFold(LinearLayout parent, string title, LinearLayout content)
    {
        var card = Card();
        var toggle = MobileButton(title + "  ＋");
        content.Visibility = ViewStates.Gone;
        toggle.Click += (_, _) =>
        {
            bool open = content.Visibility != ViewStates.Visible;
            content.Visibility = open ? ViewStates.Visible : ViewStates.Gone;
            toggle.Text = title + (open ? "  −" : "  ＋");
        };
        card.AddView(toggle); card.AddView(content); parent.AddView(card);
    }

    private void AppendVisibleLog(string line)
    {
        _visibleLogLines.Enqueue(line);
        while (_visibleLogLines.Count > 200) _visibleLogLines.Dequeue();
        if (!_logPaused && _pages.Count > LogsPage && _pages[LogsPage].Visibility == ViewStates.Visible)
            RenderCommunicationLog();
    }

    private void RenderCommunicationLog()
    {
        if (_logView is not null && !_logPaused)
            _logView.Text = _visibleLogLines.Count == 0 ? "暂无通信记录" : string.Join("\n", _visibleLogLines);
    }

    private void RefreshActionAvailability()
    {
        foreach (Button button in _actionButtons)
        {
            bool busyAllowed = _busyAllowedButtons.Contains(button);
            button.Enabled = (!_uiBusy || busyAllowed) && (!_connectionRequiredButtons.Contains(button) || _client is not null);
            button.Alpha = button.Enabled ? 1f : 0.4f;
        }
        if (_disconnectButton is not null) _disconnectButton.Enabled = _client is not null || _uiBusy;
        if (_chargeMosButton is not null)
        {
            bool enabled = !_uiBusy && _client is not null && _mosControlCapabilityChecked && _mosControlState is not null;
            _chargeMosButton.Enabled = enabled; _chargeMosButton.Alpha = enabled ? 1f : 0.4f;
        }
        if (_dischargeMosButton is not null)
        {
            bool enabled = !_uiBusy && _client is not null && _mosControlCapabilityChecked && _mosControlState is not null;
            _dischargeMosButton.Enabled = enabled; _dischargeMosButton.Alpha = enabled ? 1f : 0.4f;
        }
        UpdateProtectionEdits();
        UpdateC060EditorState();
    }

    private void UpdateProtectionEdits()
    {
        int softwareChanges = 0, hardwareChanges = 0;
        bool softwareValid = true, hardwareValid = true;
        foreach (var (row, input) in _protectionInputs)
        {
            row.EditValue = input.Text?.Trim() ?? "";
            bool valid = row.TryParseEditedValue(out ushort raw);
            bool changed = valid && raw != row.DeviceValue;
            input.Error = valid ? null : "请输入合法数值";
            input.SetTextColor(Color.ParseColor(changed ? Accent : Ink));
            input.Background = Rounded(valid ? (changed ? "#E1F2EE" : "#F3F6F8") : "#FDECE8", 8);
            softwareValid &= valid; if (changed) softwareChanges++;
        }
        foreach (var (row, input) in _afeInputs)
        {
            var parsed = row.Encode(input.Text?.Trim() ?? "");
            bool changed = parsed.Ok && parsed.Wire != _afeModel?.RequestedRaw[row.WireIndex];
            input.Error = parsed.Ok ? null : parsed.Error;
            input.SetTextColor(Color.ParseColor(changed ? Accent : Ink));
            hardwareValid &= parsed.Ok; if (changed) hardwareChanges++;
        }
        if (_saveSoftwareButton is not null)
        {
            _saveSoftwareButton.Text = $"保存修改 · {softwareChanges} 项";
            _saveSoftwareButton.Enabled = !_uiBusy && _client is not null && _protectionModel is not null && softwareValid && softwareChanges > 0;
            _saveSoftwareButton.Alpha = _saveSoftwareButton.Enabled ? 1f : 0.4f;
        }
        if (_saveHardwareButton is not null)
        {
            _saveHardwareButton.Text = $"保存修改 · {hardwareChanges} 项";
            _saveHardwareButton.Enabled = !_uiBusy && _client is not null && _afeModel is not null && hardwareValid && hardwareChanges > 0;
            _saveHardwareButton.Alpha = _saveHardwareButton.Enabled ? 1f : 0.4f;
        }
    }

    private void RenderBatterySnapshot(BatterySnapshot snapshot, DeviceIdentity? identity, uint? buildId)
    {
        _lastBatteryUiUtc = snapshot.RealtimeReadUtc;
        _displayedBatteryMac = _connectedMac;
        _socValue!.Text = $"{snapshot.SocPercent}%";
        _workState!.Text = snapshot.WorkState;
        _voltageValue!.Text = $"{snapshot.PackVoltageV:F2} V";
        _currentValue!.Text = $"{snapshot.CurrentA:+0.0;-0.0;0.0}";
        _capacityValue!.Text = $"{snapshot.CapacityNowAh:F1} Ah";
        _temperatureValue!.Text = $"{snapshot.MaxTempC:F1} ℃";
        _stateView!.Text = $"充电 MOS：{(snapshot.ChargeMosOn ? "开启" : "关闭")}    放电 MOS：{(snapshot.DischargeMosOn ? "开启" : "关闭")}\n" +
            $"均衡：{(snapshot.BalancingOn ? "运行中" : "未运行")}    保护：{snapshot.ProtectionSummary}" +
            (snapshot.HasProtection ? $"\n一级：{snapshot.ProtectionLevel1Text}\n二级：{snapshot.ProtectionLevel2Text}\n三级：{snapshot.ProtectionLevel3Text}" : "") +
            $"\n加热：{(snapshot.HeatingOn ? "运行中" : "未运行")}    制冷：{(snapshot.CoolingOn ? "运行中" : "未运行")}";
        _stateView.SetTextColor(Color.ParseColor(snapshot.HasProtection ? "#A33625" : Ink));
        SetMosDriverIndicators(snapshot.ChargeMosOn, snapshot.DischargeMosOn);
        _cellsView!.Text = $"{snapshot.ValidCellCount} 串 · 最低 {snapshot.MinCellMv} mV · 最高 {snapshot.MaxCellMv} mV\n压差 {snapshot.CellDeltaMv} mV";
        _cellGrid!.RemoveAllViews();
        LinearLayout? line = null;
        int count = 0;
        for (int i = 0; i < snapshot.CellMillivolts.Count; i++)
        {
            ushort mv = snapshot.CellMillivolts[i];
            if (mv == BmsRegisters.MissingCellVoltageMv) continue;
            if (count % 3 == 0) { line = Row(); _cellGrid.AddView(line); }
            var cell = Card(); cell.SetPadding(Dp(10), Dp(10), Dp(8), Dp(10));
            string marker = mv == snapshot.MinCellMv ? " · 最低" : mv == snapshot.MaxCellMv ? " · 最高" : "";
            cell.AddView(Label($"{i + 1:00} 串{marker}", 11, Muted));
            cell.AddView(Label($"{mv} mV", 17, Ink, true));
            if (snapshot.IsCellBalancing(i)) cell.AddView(Label("均衡中", 11, Accent));
            AddEqual(line!, cell); count++;
        }
        if (line is not null) while (count++ % 3 != 0) AddEqual(line, new View(this));
        _summaryView!.Text = $"SOH {snapshot.SohPercent}%    循环 {snapshot.CycleCount} 次\n" +
            $"满充 {snapshot.CapacityFullAh:F2} Ah    额定 {snapshot.CapacityFactoryAh:F2} Ah\n" +
            $"电池温度 {snapshot.MinTempC:F1}～{snapshot.MaxTempC:F1} ℃    MOS {snapshot.MosTempC:F1} ℃";
        if (identity is not null)
            _identityView!.Text = $"{identity.BluetoothName}\nMAC  {identity.Mac}\n序列号  {identity.Serial}\n硬件  {identity.Hardware}\n软件  {identity.Software}\nBuild ID  {(buildId.HasValue ? buildId.Value.ToString("x8") : "不可用")}";
        UpdateBatteryFreshness();
    }

    private void SetMosDriverIndicators(bool chargeOn, bool dischargeOn)
    {
        if (_chargeMosState is null || _dischargeMosState is null) return;
        MosControlState? state = _mosControlState;
        bool chargeActual = state?.DriverStateValid == true ? state.ChargeDriverOn : chargeOn;
        bool dischargeActual = state?.DriverStateValid == true ? state.DischargeDriverOn : dischargeOn;
        string evidence = state?.DriverStateValid == true ? "AFE 报告" : "状态指示";
        _chargeMosState.Text = state?.ChargeAllowed == false ? "用户关闭" : chargeActual ? "已开启" : "已关闭";
        _dischargeMosState.Text = state?.DischargeAllowed == false ? "用户关闭" : dischargeActual ? "已开启" : "已关闭";
        _chargeMosState.SetTextColor(Color.ParseColor(state?.ChargeAllowed == false ? "#A33625" : chargeActual ? Accent : Muted));
        _dischargeMosState.SetTextColor(Color.ParseColor(state?.DischargeAllowed == false ? "#A33625" : dischargeActual ? Accent : Muted));
        if (_chargeMosButton is not null)
            _chargeMosButton.Text = !_mosControlCapabilityChecked ? "读取中" : state is null ? "固件不支持" : state.ChargeAllowed ? "禁止充电" : "允许充电";
        if (_dischargeMosButton is not null)
            _dischargeMosButton.Text = !_mosControlCapabilityChecked ? "读取中" : state is null ? "固件不支持" : state.DischargeAllowed ? "禁止放电" : "允许放电";
        if (_mosControlNote is not null)
        {
            _mosControlNote.Text = !_mosControlCapabilityChecked ? "正在读取独立 MOS 控制能力…" : state is null
                ? "当前固件不支持独立 MOS 控制；上方仅显示实时状态。"
                : $"{evidence} · 产品请求 充{(state.ChargeRequested ? "开" : "关")}/放{(state.DischargeRequested ? "开" : "关")} · " +
                  $"保护门禁 {(state.OutputEnabled && !state.CommunicationInhibited && !state.FaultLatched ? "允许输出" : "限制输出")}";
        }
        RefreshActionAvailability();
    }

    private void UpdateBatteryFreshness()
    {
        if (_freshnessView is null) return;
        bool fresh = _client is not null && _displayedBatteryMac == _connectedMac &&
            _lastBatteryUiUtc is DateTimeOffset read && (DateTimeOffset.UtcNow - read).TotalSeconds < 15;
        _freshnessView.Text = fresh ? $"实时数据 · 更新于 {_lastBatteryUiUtc!.Value.ToLocalTime():HH:mm:ss}" :
            _lastBatteryUiUtc.HasValue ? "数据已过期 · 当前显示上次读取结果" : "尚未读取数据 · 请先选择设备";
        _freshnessView.SetTextColor(Color.ParseColor(fresh ? Accent : "#986126"));
        _socValue!.Alpha = fresh || !_lastBatteryUiUtc.HasValue ? 1f : 0.45f;
    }

    private sealed class MobileInsetsListener : Java.Lang.Object, View.IOnApplyWindowInsetsListener
    {
        public WindowInsets OnApplyWindowInsets(View? view, WindowInsets? insets)
        {
            if (view is null || insets is null) return insets!;
            if (OperatingSystem.IsAndroidVersionAtLeast(30))
            {
                var bars = insets.GetInsets(WindowInsets.Type.SystemBars() | WindowInsets.Type.DisplayCutout() | WindowInsets.Type.Ime());
                view.SetPadding(bars.Left, bars.Top, bars.Right, bars.Bottom);
            }
            else
            {
#pragma warning disable CA1422
                view.SetPadding(insets.SystemWindowInsetLeft, insets.SystemWindowInsetTop, insets.SystemWindowInsetRight, insets.SystemWindowInsetBottom);
#pragma warning restore CA1422
            }
            return insets;
        }
    }
}
