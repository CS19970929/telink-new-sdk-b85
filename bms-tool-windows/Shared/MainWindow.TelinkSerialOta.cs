namespace BmsTool.Windows;

public partial class MainWindow
{
    private async Task<bool> RunTelinkSerialOtaAsync(FirmwareImage image, CancellationToken ct)
    {
        await using var transport = new BmsSerialTransport();
        await transport.ConnectAsync(_connectedSerialPort!, _serialBaudRate, ct);
        using var client = new TelinkSerialOtaClient(transport);
        client.Log += message => AppendLog(message, "OTA");
        client.Progress += progress => Dispatcher.BeginInvoke(() =>
        {
            OtaProgressBar.Value = progress.Percent;
            OtaProgressText.Text = $"Telink UART/RS485 · {progress.Percent:F1}% · {progress.BytesPerSecond / 1024:F1} KB/s";
        });
        return await client.UpgradeAsync(image, ct);
    }
}
