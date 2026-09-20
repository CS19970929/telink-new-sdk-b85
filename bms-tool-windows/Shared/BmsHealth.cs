namespace BmsTool.Windows;

public static class BmsHealthStatus
{
    public const string Pass = "pass";
    public const string Info = "info";
    public const string Warning = "warning";
    public const string Critical = "critical";
    public const string Unknown = "unknown";
}

public sealed record BmsHealthCheck(
    string Id,
    string Category,
    string Status,
    string Title,
    string Evidence,
    string Recommendation);

public sealed class BmsHealthReport
{
    public DateTimeOffset CapturedUtc { get; init; } = DateTimeOffset.UtcNow;
    public string Overall { get; init; } = BmsHealthStatus.Unknown;
    public string Summary { get; init; } = "未评估";
    public int CriticalCount { get; init; }
    public int WarningCount { get; init; }
    public int UnknownCount { get; init; }
    public IReadOnlyList<BmsHealthCheck> Checks { get; init; } = Array.Empty<BmsHealthCheck>();
}

public static class BmsHealth
{
    public static BmsHealthReport Evaluate(
        DiagnosticCapture capture,
        DeviceIdentity? identity = null,
        BatterySnapshot? battery = null)
    {
        var checks = new List<BmsHealthCheck>();
        void Add(string id, string category, string status, string title, string evidence, string recommendation = "")
            => checks.Add(new(id, category, status, title, evidence, recommendation));

        if (!capture.Supported || capture.Words is not { Length: 256 } words)
        {
            Add("diagnostics.support", "通信", BmsHealthStatus.Critical, "固件诊断不可用",
                capture.Status, "升级到支持 Diagnostics schema 的固件，或检查 Modbus 通信。 ");
            return Build(checks);
        }

        Add("diagnostics.support", "通信", BmsHealthStatus.Pass, "诊断协议可用",
            $"schema={words[1]}, capability=0x{words[2]:X4}");
        Add("diagnostics.snapshot", "通信",
            capture.SnapshotConsistent ? BmsHealthStatus.Pass : BmsHealthStatus.Critical,
            capture.SnapshotConsistent ? "启动快照一致" : "启动快照不一致",
            capture.SnapshotConsistent ? "采集期间未发现设备重启" : "快照未冻结或采集期间设备发生重启",
            capture.SnapshotConsistent ? "" : "重新采集；若重复出现，检查复位、供电和 watchdog。 ");

        if (capture.Trace.Count != 0)
            Add("diagnostics.trace", "通信",
                capture.TraceConsistent ? BmsHealthStatus.Pass : BmsHealthStatus.Warning,
                capture.TraceConsistent ? "Trace 分页一致" : "Trace 分页发生变化",
                $"entries={capture.Trace.Count}",
                capture.TraceConsistent ? "" : "保留本次证据并重新采集一次完整诊断。 ");

        if (capture.Errors.Count != 0)
            Add("diagnostics.errors", "通信", BmsHealthStatus.Warning, "诊断存在部分错误",
                string.Join("；", capture.Errors), "查看原始帧并重试失败的只读项。 ");

        bool production = (words[13] & 4) != 0;
        bool dirty = (words[13] & 8) != 0;
        Add("firmware.build_type", "固件", production ? BmsHealthStatus.Pass : BmsHealthStatus.Warning,
            production ? "量产构建" : "开发/测试构建",
            production ? "PRODUCTION" : "DEVELOPMENT / TEST",
            production ? "" : "正式交付前使用可追溯的 production build。 ");
        Add("firmware.git", "固件", dirty ? BmsHealthStatus.Warning : BmsHealthStatus.Pass,
            dirty ? "固件构建工作区不干净" : "固件构建工作区干净",
            dirty ? "DIRTY：提交号不能唯一复现固件" : "CLEAN",
            dirty ? "重新从干净提交构建并记录 Build ID。 " : "");
        uint buildId = BmsDiagnostics.U32(words, 22);
        Add("firmware.build_id", "固件", buildId == 0 ? BmsHealthStatus.Unknown : BmsHealthStatus.Pass,
            buildId == 0 ? "Build ID 不可用" : "Build ID 可用",
            buildId == 0 ? "0" : buildId.ToString("x8"));

        Add("boot.afe", "启动", words[25] == 1 ? BmsHealthStatus.Pass : BmsHealthStatus.Critical,
            words[25] == 1 ? "AFE 配置初始化成功" : "AFE 配置初始化异常",
            BmsDiagnostics.Result(words[25]), "检查 AFE 通信、固定配置下发和 readback。 ");
        Add("boot.parameters", "启动", words[26] == 1 ? BmsHealthStatus.Pass : BmsHealthStatus.Critical,
            words[26] == 1 ? "参数加载/校验成功" : "参数加载/校验异常",
            BmsDiagnostics.Result(words[26]), "检查 Config 记录、schema 和参数校验失败项。 ");

        foreach (var domain in new[] { (Name: "CONFIG", Word: 180), (Name: "STATE", Word: 181), (Name: "EVENT", Word: 183) })
        {
            ushort result = words[domain.Word];
            string status = result == 1 ? BmsHealthStatus.Pass : result == 0 ? BmsHealthStatus.Info : BmsHealthStatus.Warning;
            Add("storage." + domain.Name.ToLowerInvariant(), "存储", status,
                domain.Name + (result == 1 ? " 最近操作成功" : " 最近操作需要检查"),
                BmsDiagnostics.Result(result), result is 0 or 1 ? "" : "检查底层首次/最近失败地址和 Flash 供电。 ");
        }

        if ((words[2] & BmsDiagnostics.RuntimeCapability) != 0 && words[192] >= 1)
        {
            bool sampleValid = (words[193] & 1) != 0;
            Add("runtime.sample", "采样", sampleValid ? BmsHealthStatus.Pass : BmsHealthStatus.Critical,
                sampleValid ? "AFE 采样有效" : "AFE 采样无效或过期",
                $"runtime={words[192]}, flags=0x{words[193]:X4}",
                sampleValid ? "" : "检查 AFE 通信、采样周期和数据新鲜度。 ");
        }
        else
        {
            Add("runtime.sample", "采样", BmsHealthStatus.Unknown, "运行态采样证据不可用",
                "固件未声明 runtime diagnostics capability");
        }

        ushort level1 = words[222], level2 = words[223], level3 = words[224];
        string protectionStatus = level3 != 0 ? BmsHealthStatus.Critical :
            level2 != 0 || level1 != 0 ? BmsHealthStatus.Warning : BmsHealthStatus.Pass;
        Add("protection.runtime", "保护", protectionStatus,
            protectionStatus == BmsHealthStatus.Pass ? "当前无软件保护" : "当前存在软件保护",
            $"L1=0x{level1:X4}, L2=0x{level2:X4}, L3=0x{level3:X4}",
            protectionStatus == BmsHealthStatus.Pass ? "" : "结合 protection_runtime、单体和温度原始值核对触发原因。 ");

        if (battery is not null)
        {
            int calculatedDelta = battery.MaxCellMv - battery.MinCellMv;
            bool cellSummaryConsistent = battery.ValidCellCount > 0 && calculatedDelta == battery.CellDeltaMv;
            Add("cells.summary", "采样", cellSummaryConsistent ? BmsHealthStatus.Pass : BmsHealthStatus.Warning,
                cellSummaryConsistent ? "单体汇总自洽" : "单体汇总不一致",
                $"count={battery.ValidCellCount}, min={battery.MinCellMv}mV, max={battery.MaxCellMv}mV, delta={battery.CellDeltaMv}mV",
                cellSummaryConsistent ? "数值仅作观测；是否安全由产品保护参数判断。 " : "检查缺失串哨兵、min/max/delta 计算和采样帧。 ");
        }

        if (identity is not null)
        {
            bool identityComplete = !string.IsNullOrWhiteSpace(identity.Hardware) &&
                !string.Equals(identity.Hardware, "未知", StringComparison.OrdinalIgnoreCase) &&
                !string.IsNullOrWhiteSpace(identity.Software);
            Add("identity", "身份", identityComplete ? BmsHealthStatus.Pass : BmsHealthStatus.Warning,
                identityComplete ? "设备身份可读" : "设备身份不完整",
                $"SN={identity.Serial}, HW={identity.Hardware}, SW={identity.Software}");

            if (string.Equals(identity.Hardware, "D008", StringComparison.OrdinalIgnoreCase))
            {
                if (capture.EvidenceBlocks.TryGetValue("D008Capability", out ushort[]? d008) && d008.Length >= 2)
                {
                    bool supported = d008[0] == D008Parameters.Magic && D008Parameters.SupportsProtocol(d008[1]);
                    Add("d008.protocol", "协议", supported ? BmsHealthStatus.Pass : BmsHealthStatus.Critical,
                        supported ? "D008 参数协议受支持" : "D008 参数协议不受支持",
                        $"magic=0x{d008[0]:X4}, version={d008[1]}",
                        supported ? "" : "禁止参数写入；升级客户端或固件协议。 ");
                }
                else
                {
                    Add("d008.protocol", "协议", BmsHealthStatus.Unknown, "未采集 D008 参数能力",
                        "运行完整健康检查可采集 capability evidence。 ");
                }
            }
        }

        Add("mos.physical_feedback", "MOS", BmsHealthStatus.Info, "物理 MOS 反馈不可用",
            "当前只有 Requested、AFE Command 和 CHGF/DSGF；没有 Gate/Vgs 物理反馈",
            "需要确认 MOS 实际导通时必须使用硬件测量或新增反馈电路。 ");

        return Build(checks);
    }

    private static BmsHealthReport Build(List<BmsHealthCheck> checks)
    {
        int critical = checks.Count(x => x.Status == BmsHealthStatus.Critical);
        int warning = checks.Count(x => x.Status == BmsHealthStatus.Warning);
        int unknown = checks.Count(x => x.Status == BmsHealthStatus.Unknown);
        string overall = critical != 0 ? BmsHealthStatus.Critical :
            warning != 0 ? BmsHealthStatus.Warning : BmsHealthStatus.Pass;
        string summary = critical != 0 ? $"发现 {critical} 项严重异常、{warning} 项警告" :
            warning != 0 ? $"发现 {warning} 项警告" : "未发现已声明的软件异常";
        return new BmsHealthReport
        {
            Overall = overall,
            Summary = summary,
            CriticalCount = critical,
            WarningCount = warning,
            UnknownCount = unknown,
            Checks = checks
        };
    }
}
