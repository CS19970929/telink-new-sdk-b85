using System.Buffers.Binary;
using System.IO;
using System.Windows;
using System.Windows.Controls;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private async void OpenFeidao_Click(object sender, RoutedEventArgs e)
    {
        if (_otaRunning || _parameterBusy || SerialPortList.SelectedItem is not SerialPortEndpoint port) return;
        var connected = _bmsTransport as CommToolCanTransport;
        if (_bms is not null && (connected is null || _connectedSerialPort != port.PortName))
        {
            MessageBox.Show(this, "请先断开当前设备，再选择广播端口。"); return;
        }
        var lines = new TextBlock { Margin = new Thickness(18), TextWrapping = TextWrapping.Wrap, LineHeight = 29 };
        var viewer = new Window { Owner = this, Title = $"飞道广播 · {port.PortName} · 250 kbit/s",
            Width = 740, Height = 300, Content = lines, WindowStartupLocation = WindowStartupLocation.CenterOwner };
        using var stop = new CancellationTokenSource();
        viewer.Closed += (_, _) => stop.Cancel();
        CommToolSerialConnection? own = null;
        var token = stop.Token;
        async Task ObserveAsync()
        {
            try
            {
                if (connected is null) {
                    own = await Task.Run(() => new CommToolSerialConnection(port.PortName, 1500), token);
                    await Task.Run(() => CommToolCanTransport.Configure(own, token), token);
                }
                while (!token.IsCancellationRequested) {
                    var data = connected is null ? await Task.Run(() => own!.Command(0x17, [], token), token)
                        : await connected.ReadFeidaoAsync(token);
                    if (data.Length != 72) throw new InvalidDataException("飞道广播快照长度错误。");
                    lines.Text = string.Join(Environment.NewLine, Enumerable.Range(0, 6).Select(i => FeidaoLine(data, i)));
                    await Task.Delay(700, token);
                }
            }
            catch (OperationCanceledException) when (token.IsCancellationRequested) { }
            catch (Exception ex) { lines.Text = "读取广播失败：" + ex.Message; }
            finally { own?.Dispose(); }
        }
        var task = ObserveAsync();
        viewer.ShowDialog();
        await task;
    }

    private static string FeidaoLine(byte[] snapshot, int index)
    {
        var slot = snapshot.AsSpan(index * 12, 12);
        uint age = BinaryPrimitives.ReadUInt32LittleEndian(slot);
        string id = (0x14F80200u + index).ToString("X8");
        if (age == uint.MaxValue) return id + "  等待广播";
        if (age > (index is 0 or 2 ? 3000u : 10000u)) return $"{id}  数据已过期（{age / 1000} 秒）";
        var data = slot[4..];
        string display = index switch {
            0 => $"{BinaryPrimitives.ReadUInt32BigEndian(data)/1000.0:F2} V · {BinaryPrimitives.ReadInt32BigEndian(data[4..])/1000.0:F2} A",
            1 => $"剩余 {BinaryPrimitives.ReadUInt32BigEndian(data)} mWh · 设计 {BinaryPrimitives.ReadUInt32BigEndian(data[4..])} mWh",
            2 => $"充电状态 {data[0]} · SOC {data[1]}% · 最高温度 {unchecked((sbyte)data[2])} ℃",
            3 => $"SOH {data[0]}% · 循环 {BinaryPrimitives.ReadUInt16BigEndian(data[1..])} 次",
            4 => $"协议版本 {data[0]} · 软件版本 {data[1]}",
            _ => $"状态 0x{data[0]:X2} · 异常码 {data[1]} · 剩余 {BinaryPrimitives.ReadUInt16BigEndian(data[4..])} mAh"
        };
        return $"{id}  {display}    {Convert.ToHexString(data)}";
    }
}
