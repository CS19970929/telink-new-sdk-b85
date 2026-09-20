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
    private BatterySnapshot? _lastSnapshot;

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
            SetStatus(seen.Count == 0 ? "未发现兼容 BMS；请确认设备未被其他手机/电脑连接" : $"扫描完成，共 {seen.Count} 台设备", seen.Count == 0);
        }
        catch (OperationCanceledException) { SetStatus($"扫描结束，共 {seen.Count} 台设备"); }
        catch (Exception ex) { SetStatus("扫描失败：" + ex.Message, true); }
    }

    private void RenderScanResults(IEnumerable<AndroidScanDevice> devices)
    {
        _scanResults!.RemoveAllViews();
        foreach (AndroidScanDevice device in devices)
        {
            var button = new Button(this) { Text = $"{device.Name}   {device.Mac}   {device.Rssi} dBm", TextSize = 12 };
            button.Click += async (_, _) =>
            {
                _macInput!.Text = device.Mac;
                await ConnectSelectedAsync();
            };
            _scanResults.AddView(button);
        }
    }

    private async Task ConnectSelectedAsync()
    {
        if (!HasBluetoothPermissions()) { RequestBluetoothPermissions(); return; }
        string mac = _macInput?.Text?.Trim() ?? string.Empty;
        if (string.IsNullOrWhiteSpace(mac)) { SetStatus("请输入或扫描选择 BLE MAC", true); return; }
        SetBusy(true);
        SetStatus("等待当前刷新结束…");
        await _operationGate.WaitAsync();
        try
        {
            await DisposeConnectionCoreAsync();
            SetStatus("连接并探测 BMS…");
            var transport = new AndroidBmsBleTransport(this, mac);
            transport.ConnectionProgress += AppendLog;
            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(45));
            await transport.ConnectAsync(cts.Token);
            var client = new BmsClient(transport);
            client.Log += AppendLog;
            await client.ProbeAsync(cts.Token);
            _transport = transport;
            _client = client;
            _parameterCapture = null;
            _protectionModel = null;
            _afeModel = null;
            RunOnUiThread(() => _connectionView!.Text = $"已连接 · {mac} · MTU {transport.NegotiatedMtu}");
            SetStatus("连接成功，正在读取实时数据");
            await RefreshOverviewCoreAsync(client, cts.Token);
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
        _monitorCts?.Cancel();
        await _operationGate.WaitAsync();
        try
        {
            await DisposeConnectionCoreAsync();
            RunOnUiThread(() => _connectionView!.Text = "未连接 · Android BLE");
            SetStatus("已断开");
        }
        finally { _operationGate.Release(); }
    }

    private async Task DisposeConnectionCoreAsync()
    {
        BmsClient? client = _client;
        AndroidBmsBleTransport? transport = _transport;
        _client = null;
        _transport = null;
        if (client is not null) await client.DisposeAsync();
        else if (transport is not null) await transport.DisposeAsync();
    }

    private async Task RefreshLoopAsync(CancellationToken ct)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(5));
        try
        {
            while (await timer.WaitForNextTickAsync(ct))
            {
                BmsClient? client = _client;
                if (client is null || !await _operationGate.WaitAsync(0, ct)) continue;
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
        BatterySnapshot snapshot = await client.ReadBatteryAsync(ct);
        _lastSnapshot = snapshot;
        DeviceIdentity? deviceIdentity = identity
            ? await client.ReadIdentityAsync(_macInput?.Text?.Trim() ?? string.Empty, "", ct)
            : null;
        uint? buildId = identity ? await client.TryReadFirmwareBuildIdAsync(ct) : null;
        RunOnUiThread(() =>
        {
            _summaryView!.Text =
                $"{snapshot.PackVoltageV:F2} V    {snapshot.CurrentA:+0.0;-0.0;0.0} A    SOC {snapshot.SocPercent}%\n" +
                $"{snapshot.WorkState} · SOH {snapshot.SohPercent}% · 循环 {snapshot.CycleCount}\n" +
                $"剩余/满充/额定 {snapshot.CapacityNowAh:F2}/{snapshot.CapacityFullAh:F2}/{snapshot.CapacityFactoryAh:F2} Ah\n" +
                $"温度 {snapshot.MinTempC:F1}～{snapshot.MaxTempC:F1} ℃ · MOS {snapshot.MosTempC:F1} ℃\n" +
                $"单体 {snapshot.MinCellMv}～{snapshot.MaxCellMv} mV · Δ{snapshot.CellDeltaMv} mV";
            _cellsView!.Text = string.Join("    ", snapshot.CellMillivolts
                .Select((value, i) => (value, i))
                .Where(item => item.value != BmsRegisters.MissingCellVoltageMv)
                .Select(item => $"C{item.i + 1}: {item.value}mV"));
            _stateView!.Text =
                $"保护：{snapshot.ProtectionSummary}\nL1 {snapshot.ProtectionLevel1Text}\nL2 {snapshot.ProtectionLevel2Text}\nL3 {snapshot.ProtectionLevel3Text}\n" +
                $"CHG {(snapshot.ChargeMosOn ? "ON" : "OFF")} · DSG {(snapshot.DischargeMosOn ? "ON" : "OFF")} · " +
                $"加热 {(snapshot.HeatingOn ? "ON" : "OFF")} · 制冷 {(snapshot.CoolingOn ? "ON" : "OFF")} · 均衡 {(snapshot.BalancingOn ? "ON" : "OFF")}";
            if (deviceIdentity is not null)
                _identityView!.Text = $"BluetoothName  {deviceIdentity.BluetoothName}\nMAC  {deviceIdentity.Mac}\nSerial  {deviceIdentity.Serial}\nHardware  {deviceIdentity.Hardware}\nSoftware  {deviceIdentity.Software}\nFirmware Build ID  {(buildId.HasValue ? buildId.Value.ToString("x8") : "unavailable")}";
        });
    }

    private Task ReadProtectionAsync() => WithClientAsync("读取软件保护", async (client, ct) =>
    {
        ushort[] words = await client.ReadProtectionAllAsync(ct);
        var rows = ProtectionParameterCatalog.Create();
        for (int i = 0; i < rows.Count; i++) rows[i].LoadFromDevice(words[i]);
        _protectionModel = rows;
        RunOnUiThread(() => RenderProtectionRows(rows));
        SetStatus("软件保护参数读取完成");
    });

    private void RenderProtectionRows(IReadOnlyList<ProtectionParameterRow> rows)
    {
        _protectionRows!.RemoveAllViews();
        _protectionInputs.Clear();
        foreach (IGrouping<string, ProtectionParameterRow> group in rows.GroupBy(row => row.Group))
        {
            _protectionRows.AddView(SectionTitle(group.Key));
            foreach (ProtectionParameterRow row in group)
            {
                var line = Row();
                var label = new TextView(this) { Text = $"{row.Stage}\n{row.Unit}", TextSize = 12, Gravity = GravityFlags.CenterVertical };
                line.AddView(label, new LinearLayout.LayoutParams(Dp(76), ViewGroup.LayoutParams.WrapContent));
                var input = Input(row.CustomerName, row.EditValue);
                line.AddView(input, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WrapContent, 1));
                _protectionInputs[row] = input;
                _protectionRows.AddView(line);
            }
        }
        _protectionStatus!.Text = $"已读取 {rows.Count} 项；修改后点击保存";
    }

    private async Task SaveProtectionAsync()
    {
        if (_protectionModel is null) { SetStatus("请先读取软件保护参数", true); return; }
        var changes = new Dictionary<ushort, ushort>();
        foreach (ProtectionParameterRow row in _protectionModel)
        {
            row.EditValue = _protectionInputs[row].Text?.Trim() ?? string.Empty;
            if (!row.TryParseEditedValue(out ushort raw)) { SetStatus($"{row.CustomerName} 输入无效", true); return; }
            if (raw != row.DeviceValue) changes[row.Address] = raw;
        }
        if (changes.Count == 0) { SetStatus("软件保护参数没有变化"); return; }
        if (!await ConfirmAsync("保存软件保护", $"将修改 {changes.Count} 项。固件会按完整保护组校验并保存，是否继续？")) return;
        await WithClientAsync("保存软件保护", async (client, ct) =>
        {
            await client.WriteProtectionChangesAsync(changes, (address, words) =>
            {
                for (int i = 0; i < words.Length; i++) _protectionModel.First(row => row.Address == address + i).LoadFromDevice(words[i]);
            }, ct);
            RunOnUiThread(() => RenderProtectionRows(_protectionModel));
            SetStatus("软件保护参数已写入并逐组读回确认");
        });
    }

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
        _afeRows!.RemoveAllViews();
        _afeInputs.Clear();
        _afeStatus!.Text = $"{info.BackendName} · Interface v{info.InterfaceVersion} · Apply {info.ApplyStateText} · Error {info.ErrorText}";
        foreach (IGrouping<string, AfeParameterRow> group in model.Rows.GroupBy(row => row.Group))
        {
            _afeRows.AddView(SectionTitle(group.Key));
            foreach (AfeParameterRow row in group)
            {
                var label = Note($"{row.Name} · {row.EnabledText} · Requested {row.RequestedValue} · Effective {row.EffectiveValue} · {row.Unit}");
                _afeRows.AddView(label);
                var input = Input(row.Name, row.EditValue);
                _afeInputs[row] = input;
                _afeRows.AddView(input);
            }
        }
    }

    private async Task SaveAfeAsync()
    {
        if (_afeModel is null) { SetStatus("请先读取 AFE 硬件保护", true); return; }
        foreach (AfeParameterRow row in _afeModel.Rows) row.EditValue = _afeInputs[row].Text?.Trim() ?? string.Empty;
        if (!_afeModel.TryBuildCandidate(out ushort[] candidate, out string error)) { SetStatus(error, true); return; }
        if (!_afeModel.HasChanges(candidate)) { SetStatus("AFE 硬件参数没有变化"); return; }
        if (!await ConfirmAsync("原子应用 AFE 参数", "将通过授权事务提交完整 AFE profile，并核对 Requested/Effective/ApplyState。是否继续？")) return;
        await WithClientAsync("应用 AFE 硬件保护", async (client, ct) =>
        {
            AfeHardwareSnapshot snapshot = await new AfeHardwareClient(client).WriteAllAsync(candidate, ct);
            _afeModel.Load(snapshot);
            RunOnUiThread(() => RenderAfeRows(_afeModel, snapshot.Info));
            SetStatus("AFE 参数已原子应用并读回确认");
        });
    }

    private Task ReadParametersAsync() => WithClientAsync("读取 D008 参数", async (client, ct) =>
    {
        D008ParameterCapture capture = await client.ReadD008ParametersAsync(ct);
        _parameterCapture = capture;
        RunOnUiThread(() => ShowParameters(capture));
        SetStatus(capture.Status, capture.Errors.Count != 0);
    });

    private void ShowParameters(D008ParameterCapture capture)
    {
        _parameterStatus!.Text = capture.Status + (capture.Errors.Count == 0 ? "" : "\n" + string.Join("\n", capture.Errors));
        if (capture.Blocks.TryGetValue("Capacity", out ushort[]? cap)) { _capacityInput!.Text = (cap[0] / 10m).ToString(CultureInfo.CurrentCulture); _cycleInput!.Text = cap[1].ToString(); }
        if (capture.Blocks.TryGetValue("SOC", out ushort[]? soc)) _socInput!.Text = soc[0].ToString();
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
    private Task SaveSocAsync()
    {
        if (!ushort.TryParse(_socInput?.Text, out ushort value) || value > 100) { SetStatus("SOC 必须为 0..100", true); return Task.CompletedTask; }
        return SaveParameterAsync("SOC", 0x1005, new[] { value });
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
        DiagnosticCapture capture = await client.ReadDiagnosticsAsync(full, _macInput?.Text?.Trim() ?? "Android BLE", ct);
        _diagnosticCapture = capture;
        RunOnUiThread(() => _diagnosticView!.Text = FormatDiagnostics(capture));
        SetStatus(capture.Status, capture.Errors.Count != 0);
    }, full ? TimeSpan.FromMinutes(3) : TimeSpan.FromSeconds(60));

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
            BmsDiagnostics.Export(path, _diagnosticCapture);
            SetStatus("诊断包已导出：" + path);
        }
        catch (Exception ex) { SetStatus("导出失败：" + ex.Message, true); }
        return Task.CompletedTask;
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
            if (!name.EndsWith(".bin", StringComparison.OrdinalIgnoreCase)) name += ".bin";
            target = Path.Combine(FirmwareInboxDirectory(), name);
            if (File.Exists(target))
                target = Path.Combine(FirmwareInboxDirectory(), $"{Path.GetFileNameWithoutExtension(name)}-{DateTimeOffset.Now:HHmmss}.bin");
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
            _firmwareInput!.Text = target;
            await RefreshFirmwareInboxAsync();
            try
            {
                FirmwareImage image = FirmwareImage.LoadStrictTelink(target);
                SetStatus($"固件已导入并通过预检：{image.FileName} · {image.ImageSize} bytes");
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
            var files = new DirectoryInfo(FirmwareInboxDirectory()).EnumerateFiles("*.bin")
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
                        FirmwareImage image = FirmwareImage.LoadStrictTelink(file.FullName);
                        valid = true;
                        detail = $"可升级 · {image.ImageSize} bytes";
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
                        _firmwareInput!.Text = file.FullName;
                        SetStatus($"已选择：{file.Name}");
                    };
                    _firmwareInbox.AddView(button);
                }
            });
        }
        catch (Exception ex) { SetStatus("刷新固件收件箱失败：" + ex.Message, true); }
        return Task.CompletedTask;
    }

    private async Task StartOtaAsync()
    {
        string mac = _macInput?.Text?.Trim() ?? string.Empty;
        string firmware = _firmwareInput?.Text?.Trim() ?? string.Empty;
        string expectedSerial = _expectedSerialInput?.Text?.Trim() ?? string.Empty;
        FirmwareImage image;
        try { image = FirmwareImage.LoadStrictTelink(firmware); }
        catch (Exception ex) { SetStatus("OTA 预检拒绝：" + ex.Message, true); return; }
        if (string.IsNullOrWhiteSpace(mac)) { SetStatus("OTA 必须明确指定 MAC", true); return; }
        if (!await ConfirmAsync("开始 OTA", $"目标 {mac}\n固件 {image.FileName}\n大小 {image.ImageSize} bytes\n升级期间请保持电池供电。是否继续？")) return;
        SetBusy(true);
        SetStatus("等待当前刷新结束后开始 OTA…");
        await _operationGate.WaitAsync();
        _operationCts?.Cancel();
        _operationCts?.Dispose();
        _operationCts = new CancellationTokenSource(TimeSpan.FromMinutes(6));
        try
        {
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
        transport.ConnectionProgress += AppendLog;
        await transport.ConnectAsync(ct);
        var client = new BmsClient(transport);
        client.Log += AppendLog;
        await client.ProbeAsync(ct);
        _transport = transport;
        _client = client;
        await RefreshOverviewCoreAsync(client, ct);
        RunOnUiThread(() => _connectionView!.Text = $"已连接 · {mac} · MTU {transport.NegotiatedMtu}");
    }

    private Task StartMonitorAsync()
    {
        if (_client is null) { SetStatus("请先连接 BMS", true); return Task.CompletedTask; }
        _monitorCts?.Cancel();
        _monitorCts?.Dispose();
        _monitorCts = new CancellationTokenSource();
        _monitorRecords.Clear();
        _ = MonitorLoopAsync(_monitorCts.Token);
        _monitorStatus!.Text = "监控中 · 0 条";
        SetStatus("长期监控已开始");
        return Task.CompletedTask;
    }

    private Task StopMonitorAsync()
    {
        _monitorCts?.Cancel();
        _monitorStatus!.Text = $"已停止 · {_monitorRecords.Count} 条";
        return Task.CompletedTask;
    }

    private async Task MonitorLoopAsync(CancellationToken ct)
    {
        using var timer = new PeriodicTimer(TimeSpan.FromSeconds(5));
        try
        {
            while (await timer.WaitForNextTickAsync(ct))
            {
                BmsClient? client = _client;
                if (client is null || !await _operationGate.WaitAsync(0, ct)) continue;
                try
                {
                    BatterySnapshot snapshot = await client.ReadBatteryAsync(ct);
                    _monitorRecords.Add(new MonitorRecord(DateTimeOffset.Now, snapshot));
                    RunOnUiThread(() => _monitorStatus!.Text = $"监控中 · {_monitorRecords.Count} 条 · {DateTime.Now:HH:mm:ss}");
                }
                finally { _operationGate.Release(); }
            }
        }
        catch (OperationCanceledException) { }
        catch (Exception ex) { RunOnUiThread(() => _monitorStatus!.Text = "监控中断：" + ex.Message); }
    }

    private Task ExportMonitorAsync()
    {
        if (_monitorRecords.Count == 0) { SetStatus("没有长期监控记录", true); return Task.CompletedTask; }
        string path = OutputPath($"BMS_monitor_{DateTimeOffset.Now:yyyyMMdd_HHmmss}.csv");
        var csv = new StringBuilder("time,pack_v,current_a,soc,soh,min_cell_mv,max_cell_mv,delta_mv,min_temp_c,max_temp_c,mos_temp_c,cycle,l1,l2,l3\r\n");
        foreach (MonitorRecord record in _monitorRecords)
        {
            BatterySnapshot s = record.Snapshot;
            csv.AppendLine(string.Join(",", record.Time.ToString("O"), s.PackVoltageV.ToString(CultureInfo.InvariantCulture), s.CurrentA.ToString(CultureInfo.InvariantCulture), s.SocPercent, s.SohPercent, s.MinCellMv, s.MaxCellMv, s.CellDeltaMv, s.MinTempC.ToString(CultureInfo.InvariantCulture), s.MaxTempC.ToString(CultureInfo.InvariantCulture), s.MosTempC.ToString(CultureInfo.InvariantCulture), s.CycleCount, s.ProtectionLevel1Raw, s.ProtectionLevel2Raw, s.ProtectionLevel3Raw));
        }
        File.WriteAllText(path, csv.ToString(), new UTF8Encoding(true));
        SetStatus("监控 CSV 已导出：" + path);
        return Task.CompletedTask;
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
