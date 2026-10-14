using System.Globalization;
using Android.Graphics;
using Android.Text;
using Android.Views;
using Android.Widget;
using BmsTool.Windows;

namespace BmsTool.Android;

public sealed partial class MainActivity
{
    private readonly Dictionary<string, EditText> _socProfileInputs = new();
    private TextView? _socProfileStatus;
    private EditText? _socOcvInput;
    private SocProfileParameters? _socProfile;

    private void BuildSocProfileControls(LinearLayout page)
    {
        var panel = Page();
        panel.AddView(Note("先读取设备当前 profile。保存采用分段暂存 + 一次性提交并完整读回；OCV 每行填写 mV=SOC%，电压严格递增。"));
        panel.AddView(ActionButton("读取 SOC / OCV 参数", ReadSocProfileFromUiAsync));
        _socProfileStatus = DataBlock("尚未读取");
        panel.AddView(_socProfileStatus);
        void Field(string key, string label)
        {
            _socProfileInputs[key] = LabeledInput(panel, label, "");
            _socProfileInputs[key].InputType = InputTypes.ClassNumber;
        }
        Field("deadband", "静置电流死区 / mA");
        Field("rest", "OCV 静置准备 / s");
        Field("band", "OCV 误差带 / %");
        Field("validMin", "有效电压下限 / mV");
        Field("validMax", "有效电压上限 / mV");
        Field("full", "满电同步电压 / mV");
        Field("fullMargin", "满电最小裕量 / mV");
        Field("empty", "空电同步电压 / mV");
        Field("emptyMargin", "空电最大裕量 / mV");
        Field("minBand", "OCV 最小误差带 / %");
        Field("fullDelta", "满电最大压差 / mV");
        Field("terminalDelta", "末端跟踪最大压差 / mV");
        Field("terminalStart", "末端跟踪起始偏移 / mV");
        Field("terminalL1", "末端 L1 偏移 / mV");
        Field("terminalL2", "末端 L2 偏移 / mV");
        Field("terminalL3", "末端 L3 偏移 / mV");
        panel.AddView(Note("OCV 点 / 每行 mV=SOC%"));
        _socOcvInput = new EditText(this) { Hint = "2800=0\n3000=2\n…", TextSize = 15, Gravity = GravityFlags.Top };
        _socOcvInput.SetTextColor(Color.ParseColor(Ink));
        _socOcvInput.SetPadding(Dp(12), Dp(10), Dp(12), Dp(10));
        _socOcvInput.Background = Rounded("#EAF0F3", 8);
        _socOcvInput.SetMinLines(8);
        panel.AddView(_socOcvInput);
        panel.AddView(ActionButton("保存 SOC / OCV 参数并核对", SaveSocProfileFromUiAsync));
        panel.AddView(ActionButton("恢复当前电池类型的 SOC / OCV 默认", ResetSocProfileFromUiAsync));
        AddFold(page, "SOC / OCV 算法参数", panel);
    }

    private Task ReadSocProfileFromUiAsync() => WithClientAsync("读取 SOC / OCV 参数", async (client, ct) =>
    {
        SocProfileParameters profile = await client.ReadSocProfileAsync(ct);
        _socProfile = profile;
        RunOnUiThread(() => ShowSocProfile(profile));
    });

    private async Task SaveSocProfileFromUiAsync()
    {
        if (!EnsureServiceAccess("保存 SOC / OCV 参数")) return;
        SocProfileParameters candidate;
        try { candidate = ReadSocProfileEditor(); }
        catch (Exception ex) { SetStatus("SOC / OCV 输入无效：" + ex.Message, true); return; }
        if (!await ConfirmAsync("保存 SOC / OCV 参数", "这会改变 SOC 估算、静置校正和端点判断。请确认 OCV 数据来自对应电芯验证，是否继续？")) return;
        await WithClientAsync("保存 SOC / OCV 参数", async (client, ct) =>
        {
            SocProfileParameters result = await client.SaveSocProfileAsync(candidate, BmsServiceAccess.DefaultPassword, ct);
            _socProfile = result;
            RunOnUiThread(() => ShowSocProfile(result));
        });
    }

    private async Task ResetSocProfileFromUiAsync()
    {
        if (!EnsureServiceAccess("恢复 SOC / OCV 默认")) return;
        if (!await ConfirmAsync("恢复 SOC / OCV 默认", "仅恢复当前电池类型的 SOC 算法参数与 OCV 曲线，不改变当前 SOC、容量或历史。是否继续？")) return;
        await WithClientAsync("恢复 SOC / OCV 默认", async (client, ct) =>
        {
            SocProfileParameters result = await client.ResetSocProfileAsync(BmsServiceAccess.DefaultPassword, ct);
            _socProfile = result;
            RunOnUiThread(() => ShowSocProfile(result));
        });
    }

    private void ShowSocProfile(SocProfileParameters p)
    {
        _socProfileStatus!.Text = $"{(p.Chemistry == 1 ? "磷酸铁锂" : "三元锂")} · profile v{p.ProfileVersion} · {p.OcvPoints.Count} 个 OCV 点 · {(p.Persisted ? "设备已保存" : "使用编译默认")}";
        void Set(string key, ushort value) => _socProfileInputs[key].Text = value.ToString(CultureInfo.InvariantCulture);
        Set("deadband",p.CurrentDeadbandMa);Set("rest",p.RestPrepareSeconds);Set("band",p.ErrorBandPercent);
        Set("validMin",p.ValidMinMv);Set("validMax",p.ValidMaxMv);Set("full",p.FullSyncMv);Set("fullMargin",p.FullMinMarginMv);
        Set("empty",p.EmptySyncMv);Set("emptyMargin",p.EmptyMaxMarginMv);Set("minBand",p.MinimumErrorBandPercent);
        Set("fullDelta",p.FullCellDeltaMaxMv);Set("terminalDelta",p.TerminalCellDeltaMaxMv);
        Set("terminalStart",p.TerminalStartOffsetMv);Set("terminalL1",p.TerminalL1OffsetMv);
        Set("terminalL2",p.TerminalL2OffsetMv);Set("terminalL3",p.TerminalL3OffsetMv);
        _socOcvInput!.Text = string.Join("\n", p.OcvPoints.Select(x => $"{x.Millivolts}={x.Percent}"));
    }

    private SocProfileParameters ReadSocProfileEditor()
    {
        if (_socProfile is null) throw new InvalidOperationException("请先读取设备当前 SOC / OCV 参数。");
        ushort U(string key) => ushort.Parse(_socProfileInputs[key].Text?.Trim() ?? "", CultureInfo.InvariantCulture);
        var p = new SocProfileParameters {
            Chemistry=_socProfile.Chemistry,ProfileId=_socProfile.ProfileId,ProfileVersion=_socProfile.ProfileVersion,
            CurrentDeadbandMa=U("deadband"),RestPrepareSeconds=U("rest"),ErrorBandPercent=U("band"),
            ValidMinMv=U("validMin"),ValidMaxMv=U("validMax"),FullSyncMv=U("full"),FullMinMarginMv=U("fullMargin"),
            EmptySyncMv=U("empty"),EmptyMaxMarginMv=U("emptyMargin"),MinimumErrorBandPercent=U("minBand"),
            FullCellDeltaMaxMv=U("fullDelta"),TerminalCellDeltaMaxMv=U("terminalDelta"),
            TerminalStartOffsetMv=U("terminalStart"),TerminalL1OffsetMv=U("terminalL1"),
            TerminalL2OffsetMv=U("terminalL2"),TerminalL3OffsetMv=U("terminalL3")
        };
        foreach (string line in (_socOcvInput?.Text ?? "").Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            string[] pair=line.Split(new[]{'=','，',',',':'},StringSplitOptions.RemoveEmptyEntries|StringSplitOptions.TrimEntries);
            if(pair.Length!=2)throw new FormatException("OCV 点格式应为 mV=SOC%。");
            p.OcvPoints.Add(new SocOcvPoint(ushort.Parse(pair[0],CultureInfo.InvariantCulture),ushort.Parse(pair[1].TrimEnd('%'),CultureInfo.InvariantCulture)));
        }
        return p;
    }
}
