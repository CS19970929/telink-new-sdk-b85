using System.IO;
namespace BmsTool.Windows;
public partial class MainWindow
{
    private CancellationToken AutomaticMonitoringToken =>
#if BMS_FACTORY_APP
        _automaticMonitoringStop.Token;
#else
        CancellationToken.None;
#endif

    // A metadata/extension failure must not replace a successful base-data read with a link failure.
    private async Task RefreshOptionalDeviceViewAsync(string feature,Func<Task> read)
    {
        try {await read();}
        catch(Exception ex) when(ex is IOException or TimeoutException or NotSupportedException) {
            AppendLog($"OPTIONAL_VIEW_UNAVAILABLE feature={feature}; {ex.Message}; 主通信保持", "CAPABILITY");
        }
    }
}
