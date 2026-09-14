#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "bms-tool-windows"
SHARED = TOOL / "Shared"
APP = TOOL / "BmsTool.Windows"
FACTORY = TOOL / "BmsFactoryTest.Windows"
TESTS = TOOL / "Tests"

MODEL = r'''using System.Globalization;

namespace BmsTool.Windows;

[Flags]
public enum AfeHardwareCapability : ushort
{
    Cov = 1 << 0,
    Cuv = 1 << 1,
    Ocd1 = 1 << 2,
    Ocd2 = 1 << 3,
    Occ1 = 1 << 4,
    Occ2 = 1 << 5,
    ShortCircuit = 1 << 6,
    Temperature = 1 << 7,
}

public sealed class AfeHardwareField
{
    public int Index { get; init; }
    public string Name { get; init; } = string.Empty;
    public string Unit { get; init; } = string.Empty;
    public string Current { get; init; } = string.Empty;
    public string Edit { get; set; } = string.Empty;
    public string Range { get; init; } = string.Empty;
    public bool Editable { get; init; }
}

public sealed class AfeHardwareProfileSnapshot
{
    public AfeHardwareProfileSnapshot(ushort[] words)
    {
        if (words.Length != AfeHardwareProfile.ReadWords)
            throw new ArgumentException($"AFE hardware profile read must return {AfeHardwareProfile.ReadWords} words.");
        Words = words.ToArray();
    }

    public ushort[] Words { get; }
    public ushort SchemaVersion => Words[0];
    public ushort ModelId => Words[1];
    public AfeHardwareCapability Capabilities => (AfeHardwareCapability)Words[35];
    public bool Valid => Words[36] == 1;
    public ushort ShuntMicroOhm => Words[37];
    public ushort CellCount => Words[38];
    public ushort WatchdogSeconds => Words[39];
    public string ModelName => ModelId switch
    {
        AfeHardwareProfile.Dvc1124Model => "DVC1124",
        AfeHardwareProfile.Sh3673510Model => "SH3673510/SH36735xx",
        _ => $"Unknown 0x{ModelId:X4}"
    };
}

public static class AfeHardwareProfile
{
    public const ushort RegisterBase = 0x2500;
    public const int WritableWords = 35;
    public const ushort ReadWords = 40;
    public const ushort SchemaVersion = 1;
    public const ushort Dvc1124Model = 0x1124;
    public const ushort Sh3673510Model = 0x3510;

    private sealed record Def(int Index, string Name, string Unit, AfeHardwareCapability Capability, string Description, bool Editable = true);

    private static readonly Def[] Definitions =
    {
        new(0, "Schema", "", 0, "配置结构版本", false),
        new(1, "AFE 型号", "", 0, "0x1124=DVC1124；0x3510=SH3673510", false),
        new(2, "单体过压 COV", "mV", AfeHardwareCapability.Cov, "AFE 硬件触发阈值"),
        new(3, "COV 延时", "ms", AfeHardwareCapability.Cov, "AFE 硬件延时；实际值可能量化"),
        new(4, "COV 恢复", "mV", AfeHardwareCapability.Cov, "MCU 清硬件锁存的恢复阈值"),
        new(5, "COV 恢复稳定时间", "ms", AfeHardwareCapability.Cov, "恢复条件连续满足时间"),
        new(6, "单体欠压 CUV", "mV", AfeHardwareCapability.Cuv, "AFE 硬件触发阈值"),
        new(7, "CUV 延时", "ms", AfeHardwareCapability.Cuv, "AFE 硬件延时；实际值可能量化"),
        new(8, "CUV 恢复", "mV", AfeHardwareCapability.Cuv, "MCU 清硬件锁存的恢复阈值"),
        new(9, "CUV 恢复稳定时间", "ms", AfeHardwareCapability.Cuv, "恢复条件连续满足时间"),
        new(10, "放电过流 OCD1", "0.1A", AfeHardwareCapability.Ocd1, "AFE 一级硬件放电过流"),
        new(11, "OCD1 延时", "ms", AfeHardwareCapability.Ocd1, "AFE 硬件延时"),
        new(12, "放电过流 OCD2", "0.1A", AfeHardwareCapability.Ocd2, "AFE 二级硬件放电过流"),
        new(13, "OCD2 延时", "ms", AfeHardwareCapability.Ocd2, "AFE 硬件延时"),
        new(14, "OCD 恢复电流", "0.1A", AfeHardwareCapability.Ocd1, "硬件过流锁存恢复电流"),
        new(15, "OCD 恢复稳定时间", "ms", AfeHardwareCapability.Ocd1, "负载移除/恢复条件稳定时间"),
        new(16, "充电过流 OCC1", "0.1A", AfeHardwareCapability.Occ1, "AFE 一级硬件充电过流"),
        new(17, "OCC1 延时", "ms", AfeHardwareCapability.Occ1, "AFE 硬件延时"),
        new(18, "充电过流 OCC2", "0.1A", AfeHardwareCapability.Occ2, "仅支持该能力的 AFE 显示"),
        new(19, "OCC2 延时", "ms", AfeHardwareCapability.Occ2, "AFE 硬件延时"),
        new(20, "OCC 恢复电流", "0.1A", AfeHardwareCapability.Occ1, "硬件过流锁存恢复电流"),
        new(21, "OCC 恢复稳定时间", "ms", AfeHardwareCapability.Occ1, "恢复条件稳定时间"),
        new(22, "短路 SC/SCD", "0.1A", AfeHardwareCapability.ShortCircuit, "物理电流阈值；固件转换为芯片量化配置"),
        new(23, "短路延时", "us", AfeHardwareCapability.ShortCircuit, "AFE 硬件短路延时"),
        new(24, "短路恢复稳定时间", "ms", AfeHardwareCapability.ShortCircuit, "0 表示 backend 保持锁存/未启用自动恢复"),
        new(25, "充电高温", "raw", AfeHardwareCapability.Temperature, "温度编码：(°C + 40) × 10"),
        new(26, "充电高温恢复", "raw", AfeHardwareCapability.Temperature, "温度编码：(°C + 40) × 10"),
        new(27, "充电低温", "raw", AfeHardwareCapability.Temperature, "温度编码：(°C + 40) × 10"),
        new(28, "充电低温恢复", "raw", AfeHardwareCapability.Temperature, "温度编码：(°C + 40) × 10"),
        new(29, "放电高温", "raw", AfeHardwareCapability.Temperature, "温度编码：(°C + 40) × 10"),
        new(30, "放电高温恢复", "raw", AfeHardwareCapability.Temperature, "温度编码：(°C + 40) × 10"),
        new(31, "放电低温", "raw", AfeHardwareCapability.Temperature, "温度编码：(°C + 40) × 10"),
        new(32, "放电低温恢复", "raw", AfeHardwareCapability.Temperature, "温度编码：(°C + 40) × 10"),
        new(33, "温度恢复稳定时间", "ms", AfeHardwareCapability.Temperature, "硬件温度锁存恢复稳定时间"),
        new(34, "硬件保护使能掩码", "hex", 0, "01=COV 02=CUV 04=OCD1 08=OCD2 10=OCC1 20=OCC2 40=SC 80=TEMP")
    };

    public static AfeHardwareProfileSnapshot Parse(ushort[] words)
    {
        var snapshot = new AfeHardwareProfileSnapshot(words);
        CheckIdentity(snapshot);
        return snapshot;
    }

    public static void CheckIdentity(AfeHardwareProfileSnapshot snapshot)
    {
        if (snapshot.SchemaVersion != SchemaVersion)
            throw new InvalidDataException($"Unsupported AFE profile schema {snapshot.SchemaVersion}; expected {SchemaVersion}.");
        if (snapshot.ModelId is not (Dvc1124Model or Sh3673510Model))
            throw new InvalidDataException($"Unsupported AFE model 0x{snapshot.ModelId:X4}.");
        if (!snapshot.Valid)
            throw new InvalidDataException("Device reports AFE hardware profile invalid / not migrated.");
        if (snapshot.ShuntMicroOhm == 0 || snapshot.CellCount == 0)
            throw new InvalidDataException("Device AFE profile metadata is incomplete.");
        ushort unknown = (ushort)((ushort)snapshot.Capabilities & ~0x00FFu);
        if (unknown != 0)
            throw new InvalidDataException($"Unknown AFE capability bits: 0x{unknown:X4}.");
    }

    public static IReadOnlyList<AfeHardwareField> Fields(AfeHardwareProfileSnapshot snapshot)
    {
        CheckIdentity(snapshot);
        var result = new List<AfeHardwareField>();
        foreach (Def def in Definitions)
        {
            if (def.Capability != 0 && !snapshot.Capabilities.HasFlag(def.Capability))
                continue;
            ushort value = snapshot.Words[def.Index];
            string display = Format(def.Index, value);
            result.Add(new AfeHardwareField
            {
                Index = def.Index,
                Name = def.Name,
                Unit = def.Unit,
                Current = display,
                Edit = display,
                Range = Range(snapshot, def) + "；" + def.Description,
                Editable = def.Editable,
            });
        }
        return result;
    }

    public static ushort[] Build(AfeHardwareProfileSnapshot snapshot, IEnumerable<AfeHardwareField> rows)
    {
        CheckIdentity(snapshot);
        ushort[] candidate = snapshot.Words.Take(WritableWords).ToArray();
        foreach (AfeHardwareField row in rows)
        {
            if (!row.Editable) continue;
            if (row.Index < 0 || row.Index >= WritableWords)
                throw new InvalidDataException($"Unexpected AFE field index {row.Index}.");
            candidate[row.Index] = ParseValue(row.Index, row.Edit);
        }
        Validate(candidate, snapshot);
        return candidate;
    }

    public static void Validate(ushort[] p, AfeHardwareProfileSnapshot snapshot)
    {
        if (p.Length != WritableWords) throw new InvalidDataException("AFE hardware profile must contain exactly 35 writable words.");
        if (p[0] != SchemaVersion || p[1] != snapshot.ModelId)
            throw new InvalidDataException("Schema/model fields are immutable.");
        ushort caps = (ushort)snapshot.Capabilities;
        if ((p[34] & ~caps) != 0)
            throw new InvalidDataException($"Enable mask 0x{p[34]:X4} contains unsupported capabilities 0x{(p[34] & ~caps):X4}.");
        bool Enabled(AfeHardwareCapability bit) => (p[34] & (ushort)bit) != 0;
        if (Enabled(AfeHardwareCapability.Cov) && (p[2] == 0 || p[4] >= p[2])) throw new InvalidDataException("COV recovery must be lower than COV trip.");
        if (Enabled(AfeHardwareCapability.Cuv) && (p[6] == 0 || p[8] <= p[6])) throw new InvalidDataException("CUV recovery must be higher than CUV trip.");
        if (Enabled(AfeHardwareCapability.Ocd1) && (p[10] == 0 || p[14] >= p[10])) throw new InvalidDataException("OCD recovery must be lower than OCD1.");
        if (Enabled(AfeHardwareCapability.Ocd2) && (p[12] == 0 || p[14] >= p[12])) throw new InvalidDataException("OCD recovery must be lower than OCD2.");
        if (Enabled(AfeHardwareCapability.Occ1) && (p[16] == 0 || p[20] >= p[16])) throw new InvalidDataException("OCC recovery must be lower than OCC1.");
        if (Enabled(AfeHardwareCapability.Occ2) && (p[18] == 0 || p[20] >= p[18])) throw new InvalidDataException("OCC recovery must be lower than OCC2.");
        if (Enabled(AfeHardwareCapability.Temperature))
        {
            foreach (int i in new[] {25,26,27,28,29,30,31,32}) if (p[i] > 1450) throw new InvalidDataException("Temperature raw value must be 0..1450.");
            if (p[26] >= p[25] || p[30] >= p[29]) throw new InvalidDataException("High-temperature recovery must be below trip.");
            if (p[28] <= p[27] || p[32] <= p[31]) throw new InvalidDataException("Low-temperature recovery must be above trip.");
        }

        uint shunt = snapshot.ShuntMicroOhm;
        uint SenseUv(int index) => (uint)p[index] * shunt / 10u;
        if (snapshot.ModelId == Dvc1124Model)
        {
            if (Enabled(AfeHardwareCapability.Cov) && (p[2] < 501 || p[2] > 4595)) throw new InvalidDataException("DVC COV range is 501..4595 mV.");
            if (Enabled(AfeHardwareCapability.Cuv) && p[6] > 4095) throw new InvalidDataException("DVC CUV maximum is 4095 mV.");
            if (p[3] > 8000 || p[7] > 8000 || p[11] > 2048 || p[17] > 2048 || p[13] > 1024 || p[19] > 1024) throw new InvalidDataException("DVC hardware delay exceeds supported range.");
            if (Enabled(AfeHardwareCapability.Ocd1) && SenseUv(10) > 63750) throw new InvalidDataException("DVC OCD1 threshold exceeds AFE range.");
            if (Enabled(AfeHardwareCapability.Occ1) && SenseUv(16) > 63750) throw new InvalidDataException("DVC OCC1 threshold exceeds AFE range.");
            if (Enabled(AfeHardwareCapability.Ocd2) && SenseUv(12) > 256000) throw new InvalidDataException("DVC OCD2 threshold exceeds AFE range.");
            if (Enabled(AfeHardwareCapability.Occ2) && SenseUv(18) > 256000) throw new InvalidDataException("DVC OCC2 threshold exceeds AFE range.");
            if (Enabled(AfeHardwareCapability.ShortCircuit) && (SenseUv(22) < 10000 || SenseUv(22) > 630000)) throw new InvalidDataException("DVC SCD sense threshold must map to 10..630 mV.");
        }
        else
        {
            if (Enabled(AfeHardwareCapability.Cov) && (p[2] == 0 || p[2] > 5115)) throw new InvalidDataException("SH COV must be 1..5115 mV.");
            if (Enabled(AfeHardwareCapability.Cuv) && (p[6] == 0 || p[6] > 5115)) throw new InvalidDataException("SH CUV must be 1..5115 mV.");
            if (p[3] > 10010 || p[7] > 10010 || p[11] > 10010 || p[17] > 10010 || p[13] > 400 || p[23] > 256) throw new InvalidDataException("SH hardware delay exceeds supported range.");
            if (Enabled(AfeHardwareCapability.Ocd1) && SenseUv(10) > 80000) throw new InvalidDataException("SH OCD1 threshold exceeds AFE range.");
            if (Enabled(AfeHardwareCapability.Ocd2) && SenseUv(12) > 160000) throw new InvalidDataException("SH OCD2 threshold exceeds AFE range.");
            if (Enabled(AfeHardwareCapability.Occ1) && SenseUv(16) > 44000) throw new InvalidDataException("SH OCC threshold exceeds AFE range.");
            if (Enabled(AfeHardwareCapability.ShortCircuit) && (p[23] < 2 || p[23] > 256)) throw new InvalidDataException("SH short-circuit delay must be 2..256 us.");
        }
    }

    private static string Range(AfeHardwareProfileSnapshot snapshot, Def def)
    {
        if (!def.Editable) return "只读";
        if (def.Index == 34) return $"capability=0x{(ushort)snapshot.Capabilities:X4}";
        return snapshot.ModelId == Dvc1124Model ? "DVC1124 物理单位" : "SH3673510 物理单位";
    }

    private static string Format(int index, ushort value) => index == 34 ? $"0x{value:X4}" : value.ToString(CultureInfo.InvariantCulture);

    private static ushort ParseValue(int index, string text)
    {
        text = text.Trim();
        if (index == 34 && text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
        {
            if (ushort.TryParse(text[2..], NumberStyles.HexNumber, CultureInfo.InvariantCulture, out ushort hex)) return hex;
        }
        if (ushort.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out ushort value)) return value;
        throw new InvalidDataException($"Invalid value '{text}'. Use an unsigned 16-bit integer{(index == 34 ? " or 0xHHHH" : string.Empty)}.");
    }
}
'''

CLIENT = r'''using System.Buffers.Binary;
using System.IO;

namespace BmsTool.Windows;

public sealed partial class BmsClient
{
    public async Task<AfeHardwareProfileSnapshot> ReadAfeHardwareProfileAsync(CancellationToken ct = default)
    {
        ushort[] words = await ReadRegistersAsync(AfeHardwareProfile.RegisterBase, AfeHardwareProfile.ReadWords, ct);
        return AfeHardwareProfile.Parse(words);
    }

    public async Task WriteAfeHardwareProfileAsync(ushort[] words, CancellationToken ct = default)
    {
        if (words.Length != AfeHardwareProfile.WritableWords)
            throw new ArgumentException("AFE hardware profile must be written atomically as 35 registers.");
        byte[] raw = new byte[AfeHardwareProfile.WritableWords * 2];
        for (int i = 0; i < words.Length; i++)
            BinaryPrimitives.WriteUInt16BigEndian(raw.AsSpan(i * 2, 2), words[i]);
        byte[] request = ModbusRtu.WriteMultiple(AfeHardwareProfile.RegisterBase, raw);
        if (_transport is BmsBleTransport ble && (ble.NegotiatedMtu ?? 23) < request.Length + 3)
            throw new IOException($"AFE 硬件参数必须 35 寄存器原子写入（Modbus 帧 {request.Length} 字节）；当前 BLE MTU 不足。请使用直连串口或 MTU ≥ {request.Length + 3} 的透明通道。");
        byte[] response = await TransactAsync(request, ct);
        ModbusRtu.ValidateWriteMultipleAck(response, AfeHardwareProfile.RegisterBase, AfeHardwareProfile.WritableWords);
    }
}
'''

UI = r'''using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private TabItem? _afeHardwareProfileTab;
    private readonly TextBlock _afeHardwareStatus = new()
    {
        Text = "工程参数。连接后先读取；普通软件三级保护参数不会修改这里。",
        TextWrapping = TextWrapping.Wrap,
        Margin = new Thickness(8)
    };
    private readonly DataGrid _afeHardwareGrid = new();
    private BmsClient? _afeHardwareReadClient;
    private AfeHardwareProfileSnapshot? _afeHardwareSnapshot;
    private bool _afeHardwareBusy;

    private void AddAfeHardwareProfileTab()
    {
        var root = new DockPanel();
        var top = new StackPanel();
        DockPanel.SetDock(top, Dock.Top);
        var buttons = new StackPanel { Orientation = Orientation.Horizontal };
        void AddButton(string title, Func<Task> action)
        {
            var button = new Button { Content = title, Margin = new Thickness(6), Padding = new Thickness(12, 6, 12, 6) };
            button.Click += async (_, _) => await AfeHardwareOperationAsync(action);
            buttons.Children.Add(button);
        }
        AddButton("读取 AFE 硬件保护", AfeHardwareReadAsync);
        AddButton("写入并回读验证", AfeHardwareWriteAsync);
        top.Children.Add(buttons);
        top.Children.Add(_afeHardwareStatus);
        top.Children.Add(new TextBlock
        {
            Margin = new Thickness(8),
            TextWrapping = TextWrapping.Wrap,
            Text = "该页与 g_tParam.protect 软件三级参数完全独立。写入使用 0x2500 整组原子事务；固件校验、持久化、应用 AFE 并回读。Requested 与 AFE 量化后的实际阈值可能不同。D008 未签核的 SCD/WDT/Body-Diode 不会因打开本页而自动启用。"
        });
        root.Children.Add(top);

        _afeHardwareGrid.AutoGenerateColumns = false;
        _afeHardwareGrid.CanUserAddRows = false;
        _afeHardwareGrid.CanUserDeleteRows = false;
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "硬件保护项", Binding = new Binding("Name"), IsReadOnly = true, Width = 230 });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "单位", Binding = new Binding("Unit"), IsReadOnly = true, Width = 70 });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "当前值", Binding = new Binding("Current"), IsReadOnly = true, Width = 100 });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "待写值", Binding = new Binding("Edit") { UpdateSourceTrigger = UpdateSourceTrigger.PropertyChanged }, Width = 110 });
        _afeHardwareGrid.Columns.Add(new DataGridTextColumn { Header = "约束 / 说明", Binding = new Binding("Range"), IsReadOnly = true, Width = new DataGridLength(1, DataGridLengthUnitType.Star) });
        _afeHardwareGrid.BeginningEdit += (_, e) => { if (e.Row.Item is AfeHardwareField f && !f.Editable) e.Cancel = true; };
        root.Children.Add(_afeHardwareGrid);

        _afeHardwareProfileTab = new TabItem { Header = "AFE 硬件保护（工程）", Content = root };
        _afeHardwareProfileTab.Visibility = AfeHardwareUiDefaultVisible() ? Visibility.Visible : Visibility.Collapsed;
        MainTabs.Items.Add(_afeHardwareProfileTab);
    }

    private void SetAfeHardwareProfileTabVisible(bool visible)
    {
        if (_afeHardwareProfileTab is not null)
            _afeHardwareProfileTab.Visibility = visible ? Visibility.Visible : Visibility.Collapsed;
    }

    private async Task AfeHardwareOperationAsync(Func<Task> action)
    {
        if (_afeHardwareBusy) return;
        try
        {
            if (_otaRunning || ShFactoryBusy()) throw new InvalidOperationException("请等待 OTA / 工厂测试完成。");
            _afeHardwareBusy = true;
            MainTabs.IsEnabled = false;
            _pollTimer.Stop();
            await WaitForCommunicationIdleAsync();
            await action();
        }
        catch (Exception ex)
        {
            _afeHardwareStatus.Text = "AFE 硬件参数操作失败：" + ex.Message + "。失败后必须重新读取，不假定参数未保存。";
            AppendLog(_afeHardwareStatus.Text, "AFE-HW");
            ShowError("AFE 硬件保护", ex);
        }
        finally
        {
            _afeHardwareBusy = false;
            MainTabs.IsEnabled = true;
            StartAutomaticRefresh();
        }
    }

    private async Task AfeHardwareReadAsync()
    {
        _afeHardwareReadClient = null;
        var bms = _bms ?? throw new InvalidOperationException("请先连接 BMS。");
        AfeHardwareProfileSnapshot snapshot = await bms.ReadAfeHardwareProfileAsync();
        if (!ReferenceEquals(bms, _bms)) throw new IOException("连接已改变，请重新读取。");
        _afeHardwareSnapshot = snapshot;
        _afeHardwareReadClient = bms;
        _afeHardwareGrid.ItemsSource = AfeHardwareProfile.Fields(snapshot);
        _afeHardwareStatus.Text = $"AFE={snapshot.ModelName}；Schema={snapshot.SchemaVersion}；能力=0x{(ushort)snapshot.Capabilities:X4}；{snapshot.CellCount}S；Rsense={snapshot.ShuntMicroOhm} µΩ；AFE WDT={(snapshot.WatchdogSeconds == 0 ? "关闭" : snapshot.WatchdogSeconds + " s")}；profile=有效。";
        if (_connectionMode == ConnectionMode.Ble)
            _afeHardwareStatus.Text += " 当前为 BLE；35 寄存器写入要求足够 MTU，不满足时请切换直连串口。";
        AppendLog(_afeHardwareStatus.Text, "AFE-HW");
    }

    private async Task AfeHardwareWriteAsync()
    {
        var bms = _bms ?? throw new InvalidOperationException("请先连接 BMS。");
        if (!ReferenceEquals(bms, _afeHardwareReadClient) || _afeHardwareSnapshot is null)
            throw new InvalidOperationException("必须先读取当前连接的 AFE 硬件参数，再进行编辑。");

        _afeHardwareGrid.CommitEdit(DataGridEditingUnit.Cell, true);
        _afeHardwareGrid.CommitEdit(DataGridEditingUnit.Row, true);
        ushort[] candidate = AfeHardwareProfile.Build(_afeHardwareSnapshot, _afeHardwareGrid.Items.Cast<AfeHardwareField>());
        ushort[] original = _afeHardwareSnapshot.Words.Take(AfeHardwareProfile.WritableWords).ToArray();
        if (candidate.SequenceEqual(original))
        {
            _afeHardwareStatus.Text = "AFE 硬件参数未改变，无需写入。";
            return;
        }

        AfeHardwareProfileSnapshot live = await bms.ReadAfeHardwareProfileAsync();
        if (!live.Words.Take(AfeHardwareProfile.WritableWords).SequenceEqual(original))
            throw new IOException("设备 AFE 硬件参数已被其他通道修改，请重新读取后编辑。");
        if (!ReferenceEquals(bms, _bms)) throw new IOException("连接已改变。");

        await bms.WriteAfeHardwareProfileAsync(candidate);
        AfeHardwareProfileSnapshot back = await bms.ReadAfeHardwareProfileAsync();
        if (!back.Words.Take(AfeHardwareProfile.WritableWords).SequenceEqual(candidate))
            throw new IOException("AFE 硬件参数写入后回读不一致。");
        _afeHardwareSnapshot = back;
        _afeHardwareReadClient = bms;
        _afeHardwareGrid.ItemsSource = AfeHardwareProfile.Fields(back);
        _afeHardwareStatus.Text = $"写入成功并回读一致：AFE={back.ModelName}，profile 有效。硬件实际量化值以 AFE readback/状态页为准。";
        AppendLog(_afeHardwareStatus.Text, "AFE-HW");
    }
}
'''

TEST = r'''using BmsTool.Windows;

static ushort[] ShWords()
{
    ushort[] w = new ushort[40];
    w[0]=1; w[1]=0x3510; w[2]=4200; w[3]=490; w[4]=4100; w[5]=500;
    w[6]=2500; w[7]=490; w[8]=2700; w[9]=500;
    w[10]=1000; w[11]=490; w[12]=1500; w[13]=50; w[14]=100; w[15]=2000;
    w[16]=500; w[17]=490; w[20]=100; w[21]=500;
    w[22]=3000; w[23]=8; w[24]=2000;
    w[25]=950; w[26]=900; w[27]=400; w[28]=450; w[29]=1000; w[30]=950; w[31]=300; w[32]=350; w[33]=500;
    w[34]=0x00DF; w[35]=0x00DF; w[36]=1; w[37]=250; w[38]=10; w[39]=32;
    return w;
}

static ushort[] DvcWords()
{
    ushort[] w = new ushort[40];
    w[0]=1; w[1]=0x1124; w[2]=3650; w[3]=96; w[4]=3500; w[5]=200;
    w[6]=2500; w[7]=100; w[8]=2700; w[9]=200;
    w[10]=500; w[11]=96; w[12]=1000; w[13]=100; w[14]=100; w[15]=0;
    w[16]=400; w[17]=96; w[18]=800; w[19]=100; w[20]=100; w[21]=0;
    w[34]=0x003F; w[35]=0x007F; w[36]=1; w[37]=200; w[38]=24; w[39]=0;
    return w;
}

void Assert(bool condition,string message) { if(!condition) throw new Exception(message); }

var sh=AfeHardwareProfile.Parse(ShWords());
Assert(sh.ModelName.StartsWith("SH3673510"),"SH model detection");
var shRows=AfeHardwareProfile.Fields(sh);
Assert(shRows.Any(r=>r.Index==25),"SH temperature rows must be visible");
Assert(!shRows.Any(r=>r.Index==18),"SH OCC2 row must be hidden when capability is absent");
var edit=shRows.First(r=>r.Index==2); edit.Edit="4250";
ushort[] changed=AfeHardwareProfile.Build(sh,shRows);
Assert(changed[2]==4250,"edited SH COV must serialize");

var dvc=AfeHardwareProfile.Parse(DvcWords());
var dvcRows=AfeHardwareProfile.Fields(dvc);
Assert(dvc.ModelName=="DVC1124","DVC model detection");
Assert(!dvcRows.Any(r=>r.Index==25),"DVC temperature rows must be capability-hidden");
Assert(dvcRows.Any(r=>r.Index==18),"DVC OCC2 row must be visible");
var mask=dvcRows.First(r=>r.Index==34); mask.Edit="0x007F";
ushort[] withSc=AfeHardwareProfile.Build(dvc,dvcRows);
Assert(withSc[34]==0x007F,"hex enable mask parsing");

bool rejected=false;
try { var bad=ShWords(); bad[4]=4300; AfeHardwareProfile.Validate(bad.Take(35).ToArray(),new AfeHardwareProfileSnapshot(bad)); }
catch(InvalidDataException) { rejected=true; }
Assert(rejected,"invalid COV hysteresis must be rejected locally");
Console.WriteLine("AFE hardware profile codec/validation tests PASS");
'''

TEST_PS1 = r'''$ErrorActionPreference="Stop"
$root=Split-Path -Parent $MyInvocation.MyCommand.Path
$testRoot=Join-Path $env:LOCALAPPDATA ("CodexTemp\bms-tool-windows\afe-profile-tests-"+[Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$model=[System.Security.SecurityElement]::Escape((Join-Path $root "Shared\AfeHardwareProfile.cs"))
$test=[System.Security.SecurityElement]::Escape((Join-Path $root "Tests\AfeHardwareProfileTest.cs"))
@"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><Compile Include="$model"/><Compile Include="$test"/></ItemGroup></Project>
"@ | Set-Content -LiteralPath (Join-Path $testRoot "Test.csproj") -Encoding UTF8
& dotnet run --project (Join-Path $testRoot "Test.csproj") -c Release
if($LASTEXITCODE -ne 0) {throw "AFE hardware profile codec tests failed"}
'''

(SHARED / "AfeHardwareProfile.cs").write_text(MODEL, encoding="utf-8")
(APP / "BmsClient.AfeHardwareProfile.cs").write_text(CLIENT, encoding="utf-8")
(SHARED / "MainWindow.AfeHardwareProfile.cs").write_text(UI, encoding="utf-8")
(TESTS / "AfeHardwareProfileTest.cs").write_text(TEST, encoding="utf-8")
(TOOL / "test-afe-hardware-profile.ps1").write_text(TEST_PS1, encoding="utf-8")

# Customer app: instantiate hidden engineering tab, support a separate explicit
# password for special-customer engineering access.
p = APP / "MainWindow.xaml.cs"
s = p.read_text(encoding="utf-8")
if 'private bool AfeHardwareUiDefaultVisible()' not in s:
    s = s.replace('    private bool ShFactoryBusy() => false;\n',
                  '    private bool ShFactoryBusy() => false;\n    private bool AfeHardwareUiDefaultVisible() => false;\n', 1)
if 'AddAfeHardwareProfileTab();' not in s:
    s = s.replace('        InitializeComponent();\n', '        InitializeComponent();\n        AddAfeHardwareProfileTab();\n', 1)
p.write_text(s, encoding="utf-8")

p = APP / "MainWindow.CustomerAccess.cs"
s = p.read_text(encoding="utf-8")
if 'AfeHardwareFeaturesPassword' not in s:
    s = s.replace('    private const string ProtectedFeaturesPassword = "hs456";\n',
                  '    private const string ProtectedFeaturesPassword = "hs456";\n    private const string AfeHardwareFeaturesPassword = "hs456afe";\n', 1)
old = '''        if (!string.Equals(passwordBox.Password, ProtectedFeaturesPassword, StringComparison.Ordinal))\n        {\n            AppendLog("客户版高级功能密码验证失败。", "ACCESS");\n            MessageBox.Show("密码错误。", "验证失败", MessageBoxButton.OK, MessageBoxImage.Warning);\n            return;\n        }\n\n        _protectedFeaturesUnlocked = true;\n        ProtectionTab.Visibility = Visibility.Visible;\n        OtaTab.Visibility = Visibility.Visible;\n        ProtectedFeaturesButton.Content = "锁定高级功能";\n        AppendLog("客户版高级功能已解锁：软件保护/BMS 参数、OTA。", "ACCESS");\n        MainTabs.SelectedItem = ProtectionTab;\n'''
new = '''        bool standard = string.Equals(passwordBox.Password, ProtectedFeaturesPassword, StringComparison.Ordinal);\n        bool afeEngineer = string.Equals(passwordBox.Password, AfeHardwareFeaturesPassword, StringComparison.Ordinal);\n        if (!standard && !afeEngineer)\n        {\n            AppendLog("客户版高级功能密码验证失败。", "ACCESS");\n            MessageBox.Show("密码错误。", "验证失败", MessageBoxButton.OK, MessageBoxImage.Warning);\n            return;\n        }\n\n        _protectedFeaturesUnlocked = true;\n        ProtectionTab.Visibility = Visibility.Visible;\n        OtaTab.Visibility = Visibility.Visible;\n        SetAfeHardwareProfileTabVisible(afeEngineer);\n        ProtectedFeaturesButton.Content = "锁定高级功能";\n        AppendLog(afeEngineer\n            ? "客户版工程功能已解锁：软件保护/BMS 参数、OTA、AFE 硬件保护。"\n            : "客户版高级功能已解锁：软件保护/BMS 参数、OTA；AFE 硬件保护仍隐藏。", "ACCESS");\n        MainTabs.SelectedItem = afeEngineer && _afeHardwareProfileTab is not null ? _afeHardwareProfileTab : ProtectionTab;\n'''
if old not in s:
    raise SystemExit('Customer access unlock block not found')
s = s.replace(old, new, 1)
s = s.replace('        if (_otaRunning || _shBusy)\n', '        if (_otaRunning || _shBusy || _afeHardwareBusy)\n', 1)
if 'SetAfeHardwareProfileTabVisible(false);' not in s:
    s = s.replace('        OtaTab.Visibility = Visibility.Collapsed;\n', '        OtaTab.Visibility = Visibility.Collapsed;\n        SetAfeHardwareProfileTabVisible(false);\n', 1)
s = s.replace('MainTabs.SelectedItem == _sh3520Tab)', 'MainTabs.SelectedItem == _sh3520Tab || MainTabs.SelectedItem == _afeHardwareProfileTab)')
p.write_text(s, encoding="utf-8")

# Factory app: engineering page visible by default.
p = FACTORY / "MainWindow.xaml.cs"
s = p.read_text(encoding="utf-8")
if 'private bool AfeHardwareUiDefaultVisible()' not in s:
    s = s.replace('    private bool ShFactoryBusy() => _factoryTestCts is not null || _flashTestCts is not null;\n',
                  '    private bool ShFactoryBusy() => _factoryTestCts is not null || _flashTestCts is not null;\n    private bool AfeHardwareUiDefaultVisible() => true;\n', 1)
if 'AddAfeHardwareProfileTab();' not in s:
    s = s.replace('        InitializeComponent();\n', '        InitializeComponent();\n        AddAfeHardwareProfileTab();\n', 1)
p.write_text(s, encoding="utf-8")

# Factory project links the new BmsClient partial stored with the customer app.
p = FACTORY / "BmsFactoryTest.Windows.csproj"
s = p.read_text(encoding="utf-8")
line = '    <Compile Include="..\\BmsTool.Windows\\BmsClient.AfeHardwareProfile.cs" Link="Shared\\BmsClient.AfeHardwareProfile.cs" />\n'
if 'BmsClient.AfeHardwareProfile.cs' not in s:
    s = s.replace('    <Compile Include="..\\BmsTool.Windows\\BmsClient.cs" Link="Shared\\BmsClient.cs" />\n',
                  '    <Compile Include="..\\BmsTool.Windows\\BmsClient.cs" Link="Shared\\BmsClient.cs" />\n' + line, 1)
p.write_text(s, encoding="utf-8")

# Documentation: clarify the two passwords are UI exposure only, never a security boundary.
doc = TOOL / "docs" / "afe_hardware_profile.md"
doc.write_text('''# AFE 硬件保护工程页\n\n- 通用协议窗口：`0x2500`，前 35 个寄存器为原子可写 profile，后 5 个为 capability/status 元数据。\n- 支持 `DVC1124 (0x1124)` 和 `SH3673510/SH36735xx (0x3510)` 自动识别。\n- 串口和 BLE 共用 `BmsClient`；35 寄存器写入不能拆帧，BLE MTU 不够时必须改用直连串口。\n- 客户版普通高级密码只显示软件保护/OTA；AFE 工程密码 `hs456afe` 才显示 AFE 硬件保护页。该密码仅用于隐藏 UI，不是安全边界；固件仍负责所有范围、能力、持久化、AFE apply/readback 校验。\n- 工厂版默认显示 AFE 工程页。\n- 软件三级保护和 AFE 硬件保护不自动同步。\n''', encoding="utf-8")

print('Universal AFE hardware protection Windows tool staged')
