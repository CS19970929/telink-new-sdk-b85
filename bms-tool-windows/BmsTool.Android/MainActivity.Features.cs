using Android.Content;
using Android.Views;
using Android.Widget;
using BmsTool.Windows;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace BmsTool.Android;

public sealed partial class MainActivity
{
    private IReadOnlyList<ProtectionParameterRow>? _protectionModel;
    private AfeHardwareParameterModel? _afeModel;
    private D008ParameterCapture? _parameterCapture;
    private DiagnosticCapture? _diagnosticCapture;
    private BmsHealthReport? _healthReport;
    private BatterySnapshot? _lastSnapshot;
    private DeviceIdentity? _lastIdentity;
    private MosControlState? _mosControlState;
    private bool _mosControlCapabilityChecked;
    private bool _serviceMode;

    private async Task ScanAsync()
    {
        if (!HasBluetoothPermissions()) { RequestBluetoothPermissions(); return; }
        _scanResults!.RemoveAllViews();
        SetStatus("正在扫描 BT_ / BT- 设备…");
        var seen = new Dictionary<string, AndroidScanDevice>(StringComparer.OrdinalIgnoreCase);
        try
        {
            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(8));
            await AndroidBleScanner.ScanAsync(this, TimeSpan.FromSeconds(7), device => RunOnUiThread(() =>
            {
                seen[device.Mac] = device;
                RenderScanResults(seen.Values.OrderByDescending(item => item.Rssi));
            }), cts.Token);
            SetStatus(seen.Count == 0 ? "未发现 BT_ / BT- 设备；请确认设备未被其他手机/电脑连接" : $"扫描完成，共 {seen.Count} 台设备", seen.Count == 0);
        }
        catch (OperationCanceledException) { SetStatus($"扫描结束，共 {seen.Count} 台设备"); }
        catch (Exception ex) { SetStatus("扫描失败：" + ex.Message, true); }
    }

    private void RenderScanResults(IEnumerable<AndroidScanDevice> devices)
    {
        _scanResults!.RemoveAllViews();
        foreach (AndroidScanDevice device in devices)
        {
            var button = MobileButton($"{device.Name}\n{device.Mac}  ·  信号 {device.Rssi} dBm");
            button.Click += async (_, _) =>
            {
                if (_uiBusy) return;
                _selectedMac = device.Mac;
                await ConnectSelectedAsync();
            };
            _scanResults.AddView(button);
        }
    }

    private async Task ConnectSelectedAsync()
    {
        if (!HasBluetoothPermissions()) { RequestBluetoothPermissions(); return; }
        if(!BluetoothMacCode.TryParse(_selectedMac,out string mac)){SetStatus("请先扫描或扫码选择设备",true);return;}
        _selectedMac=mac;
        SetBusy(true);
        SetStatus("等待当前刷新结束…");
        await _operationGate.WaitAsync();
        try
        {
            await DisposeConnectionCoreAsync();
            SetStatus("连接并探测 BMS…");
            var transport = new AndroidBmsBleTransport(this, mac);
            transport.ConnectionProgress += AppendLog;
            _transport=transport;
            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(45));
            await transport.ConnectAsync(cts.Token);
            var client = new BmsClient(transport);
            client.Log += AppendLog;
            _client=client;
            await client.ProbeAsync(cts.Token);
            _connectedMac = mac;
            _parameterCapture = null;
            ClearC060ParameterState();
            _protectionModel = null;
            _afeModel = null;
            _mosControlState = null;
            _mosControlCapabilityChecked = false;
            RunOnUiThread(() => _connectionView!.Text = "已连接");
            RunOnUiThread(() => _identityView!.Text = $"MAC  {mac}\n正在读取设备信息…");
            SetStatus("连接成功，正在读取实时数据");
            RunOnUiThread(() => ShowPage(BatteryPage));
            try {await RefreshOverviewCoreAsync(client,cts.Token);}
            catch(Exception ex)when(ex is BmsModbusException or TimeoutException or InvalidDataException or OperationCanceledException) {
                SetStatus("基础通信已连接，部分实时信息尚未读取，可手动重试。",true);
                AppendLog("INITIAL_VIEW_UNAVAILABLE "+ex.Message);
            }
        }
        catch (Exception ex)
        {
            await DisposeConnectionCoreAsync();
            SetStatus("连接失败：" + ex.Message, true);
            AppendLog($"CONNECT FAIL type={ex.GetType().Name} message={ex.Message}");
        }
        finally { SetBusy(false); _operationGate.Release(); }
    }

    private async Task DisposeConnectionAsync()
    {
        _operationCts?.Cancel();
        await _operationGate.WaitAsync();
        try
        {
            await DisposeConnectionCoreAsync();
            RunOnUiThread(() => _connectionView!.Text = "未连接 · Android BLE");
            SetStatus("已断开");
        }
        finally { _operationGate.Release(); RunOnUiThread(() => { RefreshActionAvailability(); UpdateBatteryFreshness(); }); }
    }

    private async Task DisposeConnectionCoreAsync(string? upgradeMac = null)
    {
        CancelBatteryLog("实时连接结束，记录已停止");
        _mobileCalibrationStep?.Cancel();await EndMobileCalibrationCoreAsync(false);
        _mobileCalibrationRecord=null;
        BmsClient? client = _client;
        AndroidBmsBleTransport? transport = _transport;
        _client = null;
        _transport = null;
        _connectedMac = null;
        _lastIdentity = null;
        RunOnUiThread(() => { if (_identityView is not null) _identityView.Text = "尚未连接设备"; });
        RunOnUiThread(()=>{if(_connectionView is not null)_connectionView.Text="未连接 · Android BLE";});
        ClearC060ParameterState();
        _mosControlState = null;
        _mosControlCapabilityChecked = false;
        _socProfile = null;
        RunOnUiThread(() => { if (_socProfileStatus is not null) _socProfileStatus.Text = "未连接，旧读数不代表当前设备"; });
        UpdateServiceAfterTransportRelease(upgradeMac);
        if (client is not null) await client.DisposeAsync();
        if (transport is not null) await transport.DisposeAsync();
    }

    private void UpdateServiceAfterTransportRelease(string? upgradeMac)
    {
        if (AppEdition.IsInternal) return;
        // Same-device YMODEM temporarily releases the APP link to open its own GATT.
        bool keepService = _serviceMode && upgradeMac is not null &&
            string.Equals(upgradeMac, _selectedMac, StringComparison.OrdinalIgnoreCase);
        if (!keepService) _serviceMode = false;
        RunOnUiThread(() =>
        {
            if (_batteryTypeStatus is not null) _batteryTypeStatus.Text = "未连接，旧读数不代表当前设备";
            if (keepService) return;
            if (_serviceModeButton is not null) _serviceModeButton.Text = "进入服务设置";
            if (_serviceModeStatus is not null) _serviceModeStatus.Text = "普通模式 · 服务设置已收起";
            if (_serviceActions is not null) _serviceActions.Visibility = ViewStates.Gone;
            if (_pages.Count > ProtectionPage && _pages[ProtectionPage].Visibility == ViewStates.Visible) ShowPage(DevicePage);
        });
    }

    private async Task RefreshLoopAsync(CancellationToken ct)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(5));
        try
        {
            while (await timer.WaitForNextTickAsync(ct))
            {
                RunOnUiThread(UpdateBatteryFreshness);
                BmsClient? client = _client;
                if (_mobileCalibrationSession is not null || client is null || !await _operationGate.WaitAsync(0, ct)) continue;
                try { await RefreshOverviewCoreAsync(client, ct, identity: false); }
                catch (OperationCanceledException) when (ct.IsCancellationRequested) { break; }
                catch (Exception ex) { AppendLog($"AUTO_REFRESH_FAIL {ex.Message}"); }
                finally { _operationGate.Release(); }
            }
        }
        catch (OperationCanceledException) { }
    }

    private Task RefreshOverviewAsync(bool showCompletion) => WithClientAsync("刷新实时数据", async (client, ct) =>
    {
        await RefreshOverviewCoreAsync(client, ct);
        if (showCompletion) SetStatus("实时数据已刷新");
    });

    private async Task RefreshOverviewCoreAsync(BmsClient client, CancellationToken ct, bool identity = true)
    {
        DeviceIdentity? deviceIdentity=identity ? await client.ReadIdentityAsync(_connectedMac??string.Empty,"",ct) : null;
        BatterySnapshot snapshot=await client.ReadBatteryAsync(ct);
        _lastSnapshot=snapshot;
        RunOnUiThread(()=>RenderBatterySnapshot(snapshot,null,null));
        MosControlState? mosControl=null;
        try {mosControl=await client.TryReadMosControlAsync(ct);}
        catch(Exception ex)when(ex is BmsModbusException or TimeoutException) {AppendLog("OPTIONAL_MOS_VIEW_UNAVAILABLE "+ex.Message);}
        _mosControlState=mosControl;_mosControlCapabilityChecked=true;
        RunOnUiThread(()=>SetMosDriverIndicators(snapshot.ChargeMosOn,snapshot.DischargeMosOn));
        if (deviceIdentity is not null) _lastIdentity = deviceIdentity;
        uint? buildId = identity ? await client.TryReadFirmwareBuildIdAsync(ct) : null;
        RunOnUiThread(() => RenderBatterySnapshot(snapshot, deviceIdentity, buildId));
    }

    private Task ReadProtectionAsync() => WithClientAsync("读取软件保护", async (client, ct) =>
    {
        if (client.IsC073) throw new IOException("C073 请读取 SH367309 硬件保护。");
        ushort[] words = await client.ReadProtectionAllAsync(ct);
        var rows = ProtectionParameterCatalog.Create();
        for (int i = 0; i < rows.Count; i++) rows[i].LoadFromDevice(words[i]);
        _protectionModel = rows;
        _protectionCaptureClient=client;
        RunOnUiThread(() => RenderProtectionRows(rows));
        SetStatus("软件保护参数读取完成");
    });

    private void RenderProtectionRows(IReadOnlyList<ProtectionParameterRow> rows)
    {
        _protectionRows!.RemoveAllViews(); _protectionInputs.Clear();
        foreach (var group in rows.GroupBy(row => row.Group))
        {
            var content = new LinearLayout(this) { Orientation = Orientation.Vertical };
            foreach (ProtectionParameterRow row in group)
            {
                content.AddView(Note($"{row.Stage} · 当前 {row.DeviceDisplayValue} {row.Unit}"));
                var input = Input(row.Stage + " / " + row.Unit, row.EditValue);
                input.InputType = global::Android.Text.InputTypes.ClassNumber | global::Android.Text.InputTypes.NumberFlagDecimal | global::Android.Text.InputTypes.NumberFlagSigned;
                _protectionInputs[row] = input;
                input.TextChanged += (_, _) => UpdateProtectionEdits();
                content.AddView(input);
            }
            AddFold(_protectionRows, group.Key, content);
        }
        _protectionStatus!.Text = $"已读取 {rows.Count} 项 · 展开保护组编辑，绿色表示已修改";
        RefreshActionAvailability();
    }

    private async Task SaveProtectionAsync()
    {
        if (!EnsureServiceAccess("保存软件保护")) return;
        if (_protectionModel is null) { SetStatus("请先读取软件保护参数", true); return; }
        var owner=_client;
        if(owner is null||!ReferenceEquals(owner,_protectionCaptureClient)){SetStatus("连接已变化，请重新读取保护参数",true);return;}
        var changes = new Dictionary<ushort, ushort>();
        foreach (ProtectionParameterRow row in _protectionModel)
        {
            row.EditValue = _protectionInputs[row].Text?.Trim() ?? string.Empty;
            if (!row.TryParseEditedValue(out ushort raw)) { _protectionInputs[row].Error = "请输入合法数值"; SetStatus($"{row.CustomerName} 输入无效", true); return; }
            if (raw != row.DeviceValue) changes[row.Address] = raw;
        }
        if (changes.Count == 0) { SetStatus("软件保护参数没有变化"); return; }
        string detail = string.Join("\n", _protectionModel.Where(row => changes.ContainsKey(row.Address))
            .Select(row => $"{row.CustomerName}：{row.DeviceDisplayValue} → {row.EditValue} {row.Unit}"));
        if (!await ConfirmAsync("保存软件保护", detail + "\n\n保存后将逐组读回确认，是否继续？")) return;
        await WithClientAsync("保存软件保护", async (client, ct) =>
        {
            if(!ReferenceEquals(client,owner))throw new IOException("连接已变化，请重新读取保护参数。");
            await client.WriteProtectionChangesAsync(changes, (address, words) =>
            {
                for (int i = 0; i < words.Length; i++) _protectionModel.First(row => row.Address == address + i).LoadFromDevice(words[i]);
            }, ct);
            RunOnUiThread(() => RenderProtectionRows(_protectionModel));
            SetStatus("软件保护参数已写入并逐组读回确认");
        });
    }

    private Task ReadC073ProtectionAsync() => WithClientAsync("读取 SH367309 硬件保护", async (client, ct) =>
    {
        var profile = await C073Protection.ReadAsync(client, ct);
        ct.ThrowIfCancellationRequested();
        RunOnUiThread(() =>
        {
            if (!ReferenceEquals(client, _client) || !client.IsC073) return;
            _c073ProtectionRows!.RemoveAllViews();
            foreach (var field in C077Hardware.Fields)
            {
                int index = field.Index;
                string unit = C073Protection.IsCurrentField(index) ? "A" : field.Unit;
                int saved = C073Protection.DisplayValue(index, field.Value(profile.Saved[index]));
                int actual = C073Protection.DisplayValue(index, profile.Actual[index]);
                _c073ProtectionRows.AddView(Note($"{C073Protection.FieldName(field)} · Flash {saved} {unit} · 芯片 {actual} {unit}"));
            }
            _c073ProtectionStatus!.Text = profile.Matches ? "Flash 与芯片配置字节一致。" : "Flash 与芯片配置有差异。";
        });
        SetStatus("SH367309 硬件保护读取完成");
    }, TimeSpan.FromSeconds(45));

    private Task ReadAfeAsync() => WithClientAsync("读取 AFE 硬件保护", async (client, ct) =>
    {
        AfeHardwareSnapshot snapshot = await new AfeHardwareClient(client).ReadAllAsync(ct);
        var model = new AfeHardwareParameterModel();
        model.Load(snapshot);
        _afeModel = model;
        RunOnUiThread(() => RenderAfeRows(model, snapshot.Info));
        SetStatus("AFE 硬件保护读取完成");
    });

    private void RenderAfeRows(AfeHardwareParameterModel model, AfeHardwareDeviceInfo info)
    {
        _afeRows!.RemoveAllViews(); _afeInputs.Clear();
        _afeStatus!.Text = $"{info.BackendName}\n应用状态：{info.ApplyStateText} · 错误：{info.ErrorText}";
        foreach (var group in model.Rows.GroupBy(row => row.Group))
        {
            var content = new LinearLayout(this) { Orientation = Orientation.Vertical };
            foreach (AfeParameterRow row in group)
            {
                content.AddView(SectionTitle(row.Name));
                content.AddView(Note($"设定 {row.RequestedValue} {row.Unit}   生效 {row.EffectiveValue} {row.Unit}\n{row.EnabledText}"));
                var input = Input(row.Name + " / " + row.Unit, row.EditValue);
                input.InputType = global::Android.Text.InputTypes.ClassNumber | global::Android.Text.InputTypes.NumberFlagDecimal | global::Android.Text.InputTypes.NumberFlagSigned;
                _afeInputs[row] = input;
                input.TextChanged += (_, _) => UpdateProtectionEdits();
                content.AddView(input);
            }
            AddFold(_afeRows, group.Key, content);
        }
        RefreshActionAvailability();
    }

    private async Task SaveAfeAsync()
    {
        if (!EnsureServiceAccess("保存硬件保护")) return;
        if (_afeModel is null) { SetStatus("请先读取 AFE 硬件保护", true); return; }
        foreach (AfeParameterRow row in _afeModel.Rows) row.EditValue = _afeInputs[row].Text?.Trim() ?? string.Empty;
        if (!_afeModel.TryBuildCandidate(out ushort[] candidate, out string error)) { SetStatus(error, true); return; }
        if (!_afeModel.HasChanges(candidate)) { SetStatus("AFE 硬件参数没有变化"); return; }
        string detail = string.Join("\n", _afeModel.Rows.Where(row => candidate[row.WireIndex] != _afeModel.RequestedRaw[row.WireIndex])
            .Select(row => $"{row.Name}：{row.RequestedValue} → {row.EditValue} {row.Unit}"));
        if (!await ConfirmAsync("保存硬件保护", detail + "\n\n将提交整组参数并核对实际生效结果，是否继续？")) return;
        await WithClientAsync("应用 AFE 硬件保护", async (client, ct) =>
        {
            AfeHardwareSnapshot snapshot = await new AfeHardwareClient(client).WriteAllAsync(candidate, ct);
            _afeModel.Load(snapshot);
            RunOnUiThread(() => RenderAfeRows(_afeModel, snapshot.Info));
            SetStatus("AFE 参数已原子应用并读回确认");
        });
    }

    private Task ReadParametersAsync() => WithClientAsync("读取参数", async (client, ct) =>
    {
        await client.ReadIdentityAsync(_connectedMac??string.Empty,"",ct);
        if(client.UsesC060Protocol){await ReadC060ParametersCoreAsync(client,ct);return;}
        D008ParameterCapture capture = await client.ReadD008ParametersAsync(ct);
        _parameterCapture = capture;
        RunOnUiThread(() => ShowParameters(capture));
        SetStatus(capture.Status, capture.Errors.Count != 0);
    });

    private void ShowParameters(D008ParameterCapture capture)
    {
        var status = new StringBuilder(capture.Status);
        if (capture.Blocks.TryGetValue("Balance", out ushort[]? balance))
            status.Append($"\n均衡：{(balance[0] != 0 ? "开启" : "关闭")} · 起始 {balance[1]} mV · 启/停压差 {balance[2]}/{balance[3]} mV");
        if (capture.Errors.Count != 0) status.Append('\n').AppendJoin('\n', capture.Errors);
        _parameterStatus!.Text = status.ToString();
        if (capture.Blocks.TryGetValue("Capacity", out ushort[]? cap)) { _capacityInput!.Text = (cap[0] / 10m).ToString(CultureInfo.CurrentCulture); _cycleInput!.Text = cap[1].ToString(); }
        if (capture.Blocks.TryGetValue("Serial", out ushort[]? serial)) _serialInput!.Text = D008Parameters.Serial(serial);
        if (capture.Blocks.TryGetValue("Heater", out ushort[]? heater))
        {
            _heaterEnable!.Checked = heater[0] != 0;
            _heaterStartInput!.Text = ((heater[1] - 400) / 10m).ToString(CultureInfo.CurrentCulture);
            _heaterStopInput!.Text = ((heater[2] - 400) / 10m).ToString(CultureInfo.CurrentCulture);
        }
    }

    private bool ParametersReady()
    {
        if (_parameterCapture?.Supported == true) return true;
        SetStatus("请先读取并确认设备支持 D008 参数协议", true);
        return false;
    }

    private Task SaveCapacityAsync() => SaveParameterAsync("额定容量", 0x2318, new[] { D008Parameters.Capacity(_capacityInput?.Text ?? "") });
    private async Task SaveSocAsync()
    {
        if (!EnsureServiceAccess("SOC 校正")) return;
        if (!ushort.TryParse(_socInput?.Text?.Trim(), out ushort value) || value > 100)
        {
            SetStatus("SOC 必须为 0～100 的整数", true);
            return;
        }
        if (!await ConfirmAsync("设置当前 SOC", $"将当前 BMS 的 SOC 设置为 {value}%，并立即读取实时数据核对。是否继续？")) return;
        await WithClientAsync("设置 SOC", async (client, ct) =>
        {
            BatterySnapshot snapshot = await client.SetSocAndVerifyAsync(value, ct);
            _lastSnapshot = snapshot;
            RunOnUiThread(() => _socInput!.Text = snapshot.SocPercent.ToString(CultureInfo.InvariantCulture));
            SetStatus($"SOC 已写入并回读确认：{snapshot.SocPercent}%");
        }, TimeSpan.FromSeconds(30));
    }
    private Task SaveCycleAsync()
    {
        if (!ushort.TryParse(_cycleInput?.Text, out ushort value)) { SetStatus("循环次数输入无效", true); return Task.CompletedTask; }
        return SaveParameterAsync("循环次数", 0x2319, new[] { value });
    }
    private Task SaveHeaterAsync()
    {
        ushort start = D008Parameters.Temperature(_heaterStartInput?.Text ?? "");
        ushort stop = D008Parameters.Temperature(_heaterStopInput?.Text ?? "");
        if (start >= stop) { SetStatus("加热启动温度必须低于停止温度", true); return Task.CompletedTask; }
        return SaveParameterAsync("加热参数", 0x2E20, new[] { (ushort)(_heaterEnable?.Checked == true ? 1 : 0), start, stop });
    }
    private async Task SaveParameterAsync(string name, ushort address, ushort[] values)
    {
        if (!EnsureServiceAccess("保存" + name)) return;
        if (!ParametersReady()) return;
        if (!await ConfirmAsync("保存" + name, "设备将同步保存并读回核对，是否继续？")) return;
        await WithClientAsync("保存" + name, async (client, ct) =>
        {
            await client.WriteD008VerifiedAsync(address, values, false, ct);
            _parameterCapture = await client.ReadD008ParametersAsync(ct);
            RunOnUiThread(() => ShowParameters(_parameterCapture));
            SetStatus(name + "已保存并读回确认");
        });
    }

    private async Task ResetParameterGroupAsync(ushort group, string name)
    {
        if (!EnsureServiceAccess("恢复默认")) return;
        if (!ParametersReady()) return;
        if (!await ConfirmAsync("恢复默认：" + name, "只恢复该分组，不清除 SN、校准或日志。是否继续？")) return;
        await WithClientAsync("恢复默认：" + name, async (client, ct) =>
        {
            await client.ResetD008GroupAsync(group, ct);
            _parameterCapture = await client.ReadD008ParametersAsync(ct);
            RunOnUiThread(() => ShowParameters(_parameterCapture));
            SetStatus(name + "已恢复并读回");
        });
    }

    private Task ReadDiagnosticsAsync(bool full) => WithClientAsync(full ? "完整诊断" : "快速诊断", async (client, ct) =>
    {
        DiagnosticCapture capture = await client.ReadDiagnosticsAsync(full, _selectedMac ?? "Android BLE", ct);
        _diagnosticCapture = capture;
        BmsHealthReport health = BmsHealth.Evaluate(capture, _lastIdentity, _lastSnapshot);
        _healthReport = health;
        RunOnUiThread(() =>
        {
            _diagnosticView!.Text = FormatDiagnostics(capture);
            _healthView!.Text = FormatHealth(health);
        });
        SetStatus(capture.Status, capture.Errors.Count != 0);
    }, full ? TimeSpan.FromMinutes(3) : TimeSpan.FromSeconds(60));

    private Task ReadHealthAsync() => WithClientAsync("设备健康检查", async (client, ct) =>
    {
        DeviceIdentity identity = _lastIdentity ?? await client.ReadIdentityAsync(_selectedMac ?? string.Empty, "", ct);
        BatterySnapshot battery = await client.ReadBatteryAsync(ct);
        _lastIdentity = identity;
        _lastSnapshot = battery;
        DiagnosticCapture capture = await client.ReadDiagnosticsAsync(true, _selectedMac ?? "Android BLE", ct);
        _diagnosticCapture = capture;
        BmsHealthReport health = BmsHealth.Evaluate(capture, identity, battery);
        _healthReport = health;
        RunOnUiThread(() =>
        {
            _healthView!.Text = FormatHealth(health);
            _diagnosticView!.Text = FormatDiagnostics(capture);
        });
        SetStatus("健康检查完成：" + health.Summary, health.CriticalCount != 0);
    }, TimeSpan.FromMinutes(3));

    private static string FormatHealth(BmsHealthReport report)
    {
        var builder = new StringBuilder();
        builder.AppendLine($"总体：{report.Overall} · {report.Summary}");
        foreach (BmsHealthCheck check in report.Checks)
        {
            builder.AppendLine($"\n[{check.Status.ToUpperInvariant()}] {check.Title}");
            builder.AppendLine(check.Evidence);
            if (!string.IsNullOrWhiteSpace(check.Recommendation)) builder.AppendLine("建议：" + check.Recommendation);
        }
        return builder.ToString();
    }

    private static string FormatDiagnostics(DiagnosticCapture capture)
    {
        var builder = new StringBuilder();
        builder.AppendLine($"{capture.Status} · Supported={capture.Supported}");
        builder.AppendLine($"SnapshotConsistent={capture.SnapshotConsistent} · TraceConsistent={capture.TraceConsistent}");
        foreach ((string name, List<DiagnosticField> fields) in new[] { ("Current", capture.Current), ("SOC", capture.Soc), ("Power", capture.Power), ("MOS", capture.Mos), ("Protection", capture.Protection), ("Boot", capture.Boot), ("Storage", capture.Storage) })
        {
            builder.AppendLine("\n[" + name + "]");
            foreach (DiagnosticField field in fields) builder.AppendLine($"{field.Field}: {field.Value}");
        }
        builder.AppendLine($"\nTrace={capture.Trace.Count} · Raw frames={capture.Frames.Count} · Evidence={capture.EvidenceBlocks.Count}");
        foreach (string error in capture.Errors) builder.AppendLine("ERROR: " + error);
        return builder.ToString();
    }

    private Task ExportDiagnosticsAsync()
    {
        if (_diagnosticCapture is null) { SetStatus("请先采集诊断", true); return Task.CompletedTask; }
        try
        {
            string path = OutputPath($"BMS_diag_{DateTimeOffset.Now:yyyyMMdd_HHmmss}.zip");
            BmsDiagnostics.Export(path, _diagnosticCapture, _healthReport);
            SetStatus("诊断包已导出：" + path);
        }
        catch (Exception ex) { SetStatus("导出失败：" + ex.Message, true); }
        return Task.CompletedTask;
    }

    private Task RunAutomatedTestAsync(string kind) => WithClientAsync(
        kind == "soc" ? "SOC 自动测试" : "诊断一致性测试",
        async (client, ct) =>
        {
            BmsTestReport report = kind == "soc"
                ? await BmsTestEngine.RunSocAsync(client, _connectedMac ?? "Android BLE", 10,
                    TimeSpan.FromSeconds(1), ct)
                : await BmsTestEngine.RunDiagnosticsAsync(client, _connectedMac ?? "Android BLE", 3,
                    full: true, TimeSpan.FromSeconds(1), ct);
            string path = OutputPath($"BMS_{kind}_test_{DateTimeOffset.Now:yyyyMMdd_HHmmss}.json");
            File.WriteAllText(path, JsonSerializer.Serialize(report, new JsonSerializerOptions(JsonSerializerDefaults.Web)
            {
                WriteIndented = true
            }), new UTF8Encoding(false));
            RunOnUiThread(() => _testView!.Text = FormatTestReport(report) + "\n\n报告：" + path);
            SetStatus(report.Summary, !report.Passed);
        },
        kind == "soc" ? TimeSpan.FromMinutes(2) : TimeSpan.FromMinutes(5));

    private static string FormatTestReport(BmsTestReport report)
    {
        var builder = new StringBuilder();
        builder.AppendLine(report.Summary);
        builder.AppendLine($"设备：{report.Endpoint}");
        builder.AppendLine($"样本：{report.SuccessfulSamples}/{report.RequestedSamples} · Build ID：{report.FirmwareBuildId ?? "unknown"}");
        foreach (BmsTestCheck check in report.Checks)
        {
            builder.AppendLine($"\n[{check.Status.ToUpperInvariant()}] {check.Title}");
            builder.AppendLine(check.Evidence);
            if (!string.IsNullOrWhiteSpace(check.Recommendation))
                builder.AppendLine("建议：" + check.Recommendation);
        }
        return builder.ToString();
    }

    private Task ReadEventsAsync() => WithClientAsync("读取事件日志", async (client, ct) =>
    {
        ushort[] words = await client.ReadRegistersAsync(0xC008, 100, ct);
        var lines = new List<string>();
        for (int i = 0; i < words.Length; i++)
        {
            byte id = (byte)(words[i] >> 8), interval = (byte)words[i];
            if (id == 0) continue;
            lines.Add($"#{i + 1}  {EventName(id)}  ·  {EventInterval(interval)}");
        }
        RunOnUiThread(() => _eventView!.Text = lines.Count == 0 ? "无有效事件" : string.Join("\n", lines));
        SetStatus($"事件日志读取完成，有效 {lines.Count}/100");
    });

    private Task RenameAsync()
    {
        string suffix = _nameInput?.Text?.Trim() ?? string.Empty;
        return WithClientAsync("修改蓝牙名", async (client, ct) =>
        {
            string result = await client.WriteBluetoothNameSuffixAsync(suffix, ct);
            SetStatus("蓝牙名已写入：" + result + "；重启设备后广播名更新");
        });
    }

    private async Task SleepAsync()
    {
        if (!await ConfirmAsync("请求休眠", "设备可能立即断开蓝牙，需满足硬件唤醒条件才能重新连接。是否继续？")) return;
        await WithClientAsync("请求设备休眠", async (client, ct) =>
        {
            await client.WriteSingleRegisterAsync(0x1102, 0x000A, ct);
            SetStatus("休眠命令已确认发送");
        });
    }

    private Task ReadRawAsync()
    {
        if (!TryParseU16(_rawAddressInput?.Text, out ushort address) || !ushort.TryParse(_rawCountInput?.Text, out ushort count) || count is 0 or > 125)
        { SetStatus("原始读取地址或数量无效", true); return Task.CompletedTask; }
        return WithClientAsync("读取原始寄存器", async (client, ct) =>
        {
            ushort[] values = await client.ReadRegistersAsync(address, count, ct);
            SetStatus($"0x{address:X4}: " + string.Join(" ", values.Select(value => value.ToString("X4"))));
        });
    }

    private async Task WriteRawAsync()
    {
        if (!TryParseU16(_rawAddressInput?.Text, out ushort address) || !TryParseU16(_rawValueInput?.Text, out ushort value))
        { SetStatus("原始写地址或值无效", true); return; }
        if (!await ConfirmAsync("危险：原始寄存器写入", $"将写入 0x{address:X4}=0x{value:X4}。保护参数和 AFE profile 应使用专用页面。确认继续？")) return;
        await WithClientAsync("原始写入并读回", async (client, ct) =>
        {
            ushort actual = await client.WriteReadableRegisterAndVerifyAsync(address, value, ct);
            SetStatus($"写入确认：0x{address:X4}=0x{actual:X4}");
        });
    }

    private Task PickFirmware()
    {
        var intent = new Intent(Intent.ActionOpenDocument);
        intent.AddCategory(Intent.CategoryOpenable);
        intent.SetType("application/octet-stream");
        StartActivityForResult(intent, FirmwarePickerRequest);
        return Task.CompletedTask;
    }

    private string FirmwareInboxDirectory()
    {
        return AndroidFirmwareInbox.GetDirectory(this);
    }

    private async Task ImportFirmwareAsync(global::Android.Net.Uri uri)
    {
        string? target = null;
        try
        {
            string name = ReadDisplayName(uri) ?? $"firmware-{DateTimeOffset.Now:yyyyMMdd-HHmmss}.bin";
            name = string.Concat(Path.GetFileName(name).Select(ch => Path.GetInvalidFileNameChars().Contains(ch) ? '_' : ch));
            if(string.IsNullOrWhiteSpace(name))name="firmware.bin";
            target = Path.Combine(FirmwareInboxDirectory(), name);
            if (File.Exists(target))
                target = Path.Combine(FirmwareInboxDirectory(), $"{Path.GetFileNameWithoutExtension(name)}-{Guid.NewGuid():N}{Path.GetExtension(name)}");
            await using (Stream input = ContentResolver!.OpenInputStream(uri)
                ?? throw new IOException("无法读取所选固件。"))
            await using (FileStream output = File.Create(target))
            {
                byte[] buffer = new byte[81920];
                int total = 0;
                while (true)
                {
                    int count = await input.ReadAsync(buffer);
                    if (count == 0) break;
                    total = checked(total + count);
                    if (total > AndroidFirmwareInbox.MaxImageBytes)
                        throw new InvalidDataException("固件超过 App 支持的最大大小。");
                    await output.WriteAsync(buffer.AsMemory(0, count));
                }
            }
            SelectFirmware(target);
            await RefreshFirmwareInboxAsync();
            try
            {
                FirmwareImage image = FirmwareImage.LoadForBluetoothUpgrade(target);
                UpgradeStatus($"固件已就绪：{image.FileName}\n{image.ImageSize:N0} 字节");
            }
            catch (Exception ex) { SetStatus("固件已导入，但 OTA 预检不通过：" + ex.Message, true); }
        }
        catch (Exception ex)
        {
            if (target is not null)
            {
                try { File.Delete(target); }
                catch { }
            }
            SetStatus("导入固件失败：" + ex.Message, true);
        }
    }

    private string? ReadDisplayName(global::Android.Net.Uri uri)
    {
        try
        {
            using global::Android.Database.ICursor? cursor = ContentResolver?.Query(
                uri, new[] { global::Android.Provider.IOpenableColumns.DisplayName }, null, null, null);
            if (cursor?.MoveToFirst() == true)
            {
                int column = cursor.GetColumnIndex(global::Android.Provider.IOpenableColumns.DisplayName);
                if (column >= 0) return cursor.GetString(column);
            }
        }
        catch { }
        return uri.LastPathSegment;
    }

    private Task RefreshFirmwareInboxAsync()
    {
        try
        {
            var files = new DirectoryInfo(FirmwareInboxDirectory()).EnumerateFiles().Where(file=>!file.Name.StartsWith(".",StringComparison.Ordinal))
                .OrderByDescending(file => file.LastWriteTimeUtc).Take(20).ToArray();
            RunOnUiThread(() =>
            {
                _firmwareInbox!.RemoveAllViews();
                _firmwareInboxStatus!.Text = files.Length == 0
                    ? "固件收件箱为空"
                    : $"共 {files.Length} 个固件；点击一项即可选择";
                foreach (FileInfo file in files)
                {
                    string detail;
                    bool valid;
                    try
                    {
                        FirmwareImage image = FirmwareImage.LoadForBluetoothUpgrade(file.FullName);
                        valid = true;
                        detail = $"{image.ImageSize:N0} 字节";
                    }
                    catch (Exception ex)
                    {
                        valid = false;
                        detail = "预检不通过 · " + ex.Message;
                    }
                    var button = new Button(this)
                    {
                        Text = $"{(valid ? "✓" : "!")} {file.Name}\n{detail}",
                        TextSize = 11
                    };
                    button.SetAllCaps(false);
                    button.Click += (_, _) =>
                    {
                        SelectFirmware(file.FullName);
                        UpgradeStatus($"已选择：{file.Name}\n{detail}",!valid);
                    };
                    _firmwareInbox.AddView(button);
                }
            });
        }
        catch (Exception ex) { SetStatus("刷新固件收件箱失败：" + ex.Message, true); }
        return Task.CompletedTask;
    }

    private Task StartOtaAsync() => StartOtaAsync(skipConfirmation: false, requireExistingConnection: false);

    private async Task StartOtaAsync(bool skipConfirmation, bool requireExistingConnection)
    {
        if(_mobileCalibrationSession is not null){UpgradeStatus("请先完成或取消电流校准。",true);return;}
        if (!EnsureServiceAccess("固件升级")) return;
        string mac = (_selectedMac ?? string.Empty).ToUpperInvariant();
        string firmware = _firmwarePath;
        string expectedSerial = _expectedSerialInput?.Text?.Trim() ?? string.Empty;
        if (requireExistingConnection && (_client is null || _transport is null ||
            !string.Equals(mac, _connectedMac, StringComparison.OrdinalIgnoreCase)))
        {
            SetStatus("自动 OTA 已取消：目标 BMS 连接已失效", true);
            AppendLog("AUTO_OTA_SKIPPED reason=connection_changed_before_preflight");
            return;
        }
        FirmwareImage image;
        try { image = FirmwareImage.LoadForBluetoothUpgrade(firmware); }
        catch (Exception ex) { SetStatus("OTA 预检拒绝：" + ex.Message, true); return; }
        if(image.IsSerialPackage) {
            if(skipConfirmation){UpgradeStatus("YMODEM包已导入，请在手机核对设备后手动确认升级。",true);return;}
            await RunSerialUpgradeAsync(image,mac,expectedSerial);return;
        }
        CheckUpgradeEndpoint(mac,requireExistingConnection);
        if (string.IsNullOrWhiteSpace(mac)) { SetStatus("OTA 必须明确指定 MAC", true); return; }
        if (!skipConfirmation && !await ConfirmAsync("开始 OTA", $"目标 {mac}\n固件 {image.FileName}\n大小 {image.ImageSize} bytes\n升级期间请保持电池供电。是否继续？")) return;
        if (skipConfirmation) AppendLog($"AUTO_OTA_CONFIRMED_BY_VSCODE mac={mac} file={image.FileName}");
        SetBusy(true);
        SetStatus("等待当前刷新结束后开始 OTA…");
        await _operationGate.WaitAsync();
        _operationCts?.Cancel();
        _operationCts?.Dispose();
        _operationCts = new CancellationTokenSource(TimeSpan.FromMinutes(6));
        try
        {
            CheckUpgradeEndpoint(mac,requireExistingConnection);
            DeviceIdentity? before = null;
            if (_client is not null) before = await _client.ReadIdentityAsync(mac, "", _operationCts.Token);
            await DisposeConnectionCoreAsync();
            string sha = Convert.ToHexString(SHA256.HashData(image.Bytes));
            AppendLog($"PREFLIGHT file={image.FileName} bytes={image.ImageSize} marker={image.HasD008TlnkStartupMarker} crcTrailer={image.HasValidTelinkCrcTrailer} sha256={sha}");
            bool otaResult;
            await using (var otaTransport = new AndroidOtaBleTransport(this, mac))
            {
                otaTransport.Log += AppendLog;
                await otaTransport.ConnectAsync(_operationCts.Token);
                var ota = new TelinkOtaClient(otaTransport);
                ota.Log += AppendLog;
                ota.Progress += p => SetStatus($"OTA {p.Percent:F1}% · {p.SentBytes}/{p.TotalBytes} bytes · {p.BytesPerSecond:F0} B/s");
                otaResult = await ota.UpgradeAsync(image, OtaTransferMode.LegacyFast, _operationCts.Token);
            }
            if (!otaResult) throw new IOException("设备未返回 OTA_SUCCESS。");
            DeviceIdentity after = await ReadIdentityAfterOtaAsync(mac, _operationCts.Token);
            bool serialMatch = string.IsNullOrWhiteSpace(expectedSerial) || string.Equals(after.Serial, expectedSerial, StringComparison.Ordinal);
            AppendLog($"OTA_VERIFY before='{before?.Serial}' after='{after.Serial}' expected='{expectedSerial}' match={serialMatch}");
            if (!serialMatch) throw new IOException($"OTA 已返回成功，但 Serial 回读不匹配：expected={expectedSerial}, actual={after.Serial}。");
            SetStatus($"OTA 成功 · Serial {after.Serial}");
            AppendLog("TEST_RESULT OTA_OK");
            await ConnectSelectedCoreAfterOtaAsync(mac, _operationCts.Token);
        }
        catch (OperationCanceledException) { SetStatus("OTA 已取消"); AppendLog("TEST_RESULT CANCELLED"); }
        catch (Exception ex) { SetStatus("OTA 失败/未确认：" + ex.Message, true); AppendLog($"TEST_RESULT FAIL type={ex.GetType().Name} message={ex.Message}"); }
        finally { SetBusy(false); _operationGate.Release(); }
    }

    private async Task<DeviceIdentity> ReadIdentityAfterOtaAsync(string mac, CancellationToken ct)
    {
        Exception? last = null;
        for (int attempt = 1; attempt <= 8; attempt++)
        {
            try
            {
                AppendLog($"POST_OTA_RECONNECT attempt={attempt}/8");
                await using var transport = new AndroidBmsBleTransport(this, mac);
                transport.ConnectionProgress += AppendLog;
                await transport.ConnectAsync(ct);
                await using var client = new BmsClient(transport);
                client.Log += AppendLog;
                await client.ProbeAsync(ct);
                return await client.ReadIdentityAsync(mac, "", ct);
            }
            catch (Exception ex) when (attempt < 8 && ex is not OperationCanceledException)
            {
                last = ex;
                await Task.Delay(TimeSpan.FromSeconds(2), ct);
            }
        }
        throw new IOException("OTA 后设备未能重新连接。", last);
    }

    private async Task ConnectSelectedCoreAfterOtaAsync(string mac, CancellationToken ct)
    {
        var transport = new AndroidBmsBleTransport(this, mac);
        _transport=transport;
        transport.ConnectionProgress += AppendLog;
        await transport.ConnectAsync(ct);
        var client = new BmsClient(transport);
        _client=client;
        client.Log += AppendLog;
        await client.ProbeAsync(ct);
        _transport = transport;
        _client = client;
        _connectedMac = mac;
        await RefreshOverviewCoreAsync(client, ct);
        RunOnUiThread(() => _connectionView!.Text = "已连接");
    }

    private async Task ToggleMosAllowedAsync(bool chargeChannel)
    {
        MosControlState? current = _mosControlState;
        if (current is null) { SetStatus("当前固件不支持独立 MOS 控制", true); return; }
        bool allowed = chargeChannel ? current.ChargeAllowed : current.DischargeAllowed;
        string channel = chargeChannel ? "充电" : "放电";
        string action = allowed ? "禁止" : "允许";
        string detail = allowed
            ? $"将{action}{channel} MOS。该设置只保存在 RAM 中，MCU 重启后恢复允许。"
            : $"将{action}{channel} MOS，但保护、采样资格及 AFE 仍可保持关闭。";
        if (!await ConfirmAsync(action + channel + " MOS", detail + "\n\n这不是休眠命令，是否继续？")) return;
        await WithClientAsync(action + channel + " MOS", async (client, ct) =>
        {
            MosControlState state = await client.SetMosChannelAllowedAsync(chargeChannel, !allowed, ct);
            _mosControlState = state;
            _mosControlCapabilityChecked = true;
            RunOnUiThread(() => SetMosDriverIndicators(_lastSnapshot?.ChargeMosOn ?? false, _lastSnapshot?.DischargeMosOn ?? false));
            SetStatus($"{channel} MOS 用户许可已写入并读回；实际输出仍受保护控制");
        }, TimeSpan.FromSeconds(20));
    }

    private async Task EnterServiceModeAsync()
    {
        if (AppEdition.IsInternal) { ShowPage(ProtectionPage); return; }
        if (_serviceMode)
        {
            _serviceMode = false;
            _serviceModeButton!.Text = "进入服务设置";
            _serviceModeStatus!.Text = "普通模式 · 服务设置已收起";
            _serviceActions!.Visibility = ViewStates.Gone;
            SetStatus("已退出服务模式");
            ShowPage(DevicePage);
            return;
        }
        var expectedClient = _client;
        if (!BluetoothMacCode.TryParse(_selectedMac, out string expectedMac))
        {
            SetStatus("请先扫描或扫码选择设备", true);
            return;
        }
        // A selected module may already be in Boot. Service access must not
        // require an APP probe; parameter writes still require a live client.
        string? password = await PromptServicePasswordAsync();
        if (password is null) return;
        if (!BmsServiceAccess.PasswordValid(password)) { SetStatus("服务密码错误", true); return; }
        if (!ReferenceEquals(expectedClient, _client) ||
            !string.Equals(expectedMac, _selectedMac, StringComparison.OrdinalIgnoreCase))
        { SetStatus("设备已改变，请重新验证服务权限", true); return; }
        _serviceMode = true;
        _serviceModeButton!.Text = "退出服务设置";
        _serviceModeStatus!.Text = "服务模式已开启 · 请选择要配置的项目；断开后自动退出";
        _serviceActions!.Visibility = ViewStates.Visible;
        SetStatus("服务模式已开启，请选择配置项目");
    }

    private bool EnsureServiceAccess(string operation)
    {
        if (AppEdition.IsInternal || _serviceMode) return true;
        SetStatus(operation + "需要先从设备页进入服务设置", true);
        return false;
    }

    private string OutputPath(string fileName)
    {
        string directory = GetExternalFilesDir(global::Android.OS.Environment.DirectoryDocuments)?.AbsolutePath
            ?? GetExternalFilesDir(null)?.AbsolutePath ?? FilesDir!.AbsolutePath;
        Directory.CreateDirectory(directory);
        return Path.Combine(directory, fileName);
    }

    private static bool TryParseU16(string? text, out ushort value)
    {
        text = text?.Trim();
        if (text?.StartsWith("0x", StringComparison.OrdinalIgnoreCase) == true)
            return ushort.TryParse(text[2..], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out value);
        return ushort.TryParse(text, out value);
    }

    private static string EventName(byte id) => id switch
    {
        1 => "BMS启动", 2 => "进入休眠", 3 => "均衡开启", 4 => "加热开启", 5 => "制冷开启",
        6 => "单体过压", 7 => "总压过压", 8 => "充电过流", 9 => "单体欠压", 10 => "总压欠压",
        11 => "放电过流", 12 => "充电低温", 13 => "放电低温", 14 => "充电高温", 15 => "放电高温",
        16 => "单体压差保护", 17 => "CBC错误", 18 => "AFE1错误", 19 => "AFE2错误", 20 => "EEPROM错误",
        _ => $"未知事件({id})"
    };

    private static string EventInterval(byte code) => code switch
    {
        0 => "启动记录 / 0", 171 => "≤1分钟", 170 => ">168小时", >= 1 and <= 168 => $"约 {code} 小时", _ => $"未知间隔 {code}"
    };
}
