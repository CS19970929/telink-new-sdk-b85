using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Media;

namespace BmsTool.Windows;

public partial class MainWindow
{
    private void AddEventLogTab()
    {
        if (MainTabs.Items.OfType<TabItem>().Any(t => string.Equals(t.Header?.ToString(), "事件日志", StringComparison.Ordinal)))
            return;

        var tab = new TabItem { Header = "事件日志" };
        var root = new Grid { Margin = new Thickness(12) };
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });

        var top = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 8) };
        var readButton = new Button { Content = "读取100条日志", Width = 120, Height = 30 };
        readButton.Click += ReadEventLogs_Click;
        top.Children.Add(readButton);

        _eventLogStatus = new TextBlock { Text = "未读取", VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(12, 0, 0, 0) };
        top.Children.Add(_eventLogStatus);
        top.Children.Add(new TextBlock
        {
            Text = "最新记录在前。设备日志只保存事件类型和与上一事件的时间间隔，不保存绝对日期时间。",
            VerticalAlignment = VerticalAlignment.Center,
            Margin = new Thickness(20, 0, 0, 0),
            Foreground = Brushes.DimGray
        });
        Grid.SetRow(top, 0);
        root.Children.Add(top);

        var grid = new DataGrid
        {
            AutoGenerateColumns = false,
            CanUserAddRows = false,
            CanUserDeleteRows = false,
            IsReadOnly = true,
            ItemsSource = _deviceEventLogs
        };
        grid.Columns.Add(new DataGridTextColumn { Header = "序号", Binding = new Binding(nameof(DeviceEventLogRow.Position)), Width = 70 });
        grid.Columns.Add(new DataGridTextColumn { Header = "事件", Binding = new Binding(nameof(DeviceEventLogRow.EventName)), Width = 220 });
        grid.Columns.Add(new DataGridTextColumn { Header = "与上一事件间隔", Binding = new Binding(nameof(DeviceEventLogRow.IntervalText)), Width = 190 });
        grid.Columns.Add(new DataGridTextColumn { Header = "状态", Binding = new Binding(nameof(DeviceEventLogRow.StateText)), Width = new DataGridLength(1, DataGridLengthUnitType.Star) });
        Grid.SetRow(grid, 1);
        root.Children.Add(grid);

        tab.Content = root;
        MainTabs.Items.Insert(Math.Min(3, MainTabs.Items.Count), tab);
    }

    private async void ReadEventLogs_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            _pollTimer.Stop();
            await WaitForCommunicationIdleAsync();
            BmsClient bms = _bms ?? throw new InvalidOperationException("请先连接BMS。");
            _eventLogStatus!.Text = "正在读取100条...";

            ushort[] words = await bms.ReadRegistersAsync(0xC008, 100);
            _deviceEventLogs.Clear();
            int valid = 0;
            for (int i = 0; i < words.Length; i++)
            {
                byte eventId = (byte)(words[i] >> 8);
                byte intervalCode = (byte)words[i];
                bool populated = eventId != 0;
                if (populated) valid++;

                _deviceEventLogs.Add(new DeviceEventLogRow(
                    i + 1,
                    populated ? DecodeEventName(eventId) : "—",
                    populated ? DecodeEventInterval(intervalCode) : "—",
                    populated ? "有效" : "空记录"));
            }

            _eventLogStatus.Text = $"读取完成 · 有效 {valid}/100 · {DateTime.Now:HH:mm:ss}";
            AppendLog($"EVENT_LOG_READ_OK valid={valid}/100", "LOG");
        }
        catch (Exception ex)
        {
            if (_eventLogStatus is not null) _eventLogStatus.Text = "读取失败";
            ShowError("读取设备事件日志失败", ex);
        }
        finally
        {
            StartAutomaticRefresh();
        }
    }

    private static string DecodeEventName(byte id) => id switch
    {
        1 => "BMS启动",
        2 => "进入休眠",
        3 => "均衡开启",
        4 => "加热开启",
        5 => "制冷开启",
        6 => "单体过压",
        7 => "总压过压",
        8 => "充电过流",
        9 => "单体欠压",
        10 => "总压欠压",
        11 => "放电过流",
        12 => "充电低温",
        13 => "放电低温",
        14 => "充电高温",
        15 => "放电高温",
        16 => "单体压差保护",
        17 => "CBC错误",
        18 => "AFE1错误",
        19 => "AFE2错误",
        20 => "EEPROM错误",
        _ => $"未知事件({id})"
    };

    private static string DecodeEventInterval(byte code)
    {
        if (code == 0) return "启动记录 / 0";
        if (code == 171) return "≤1分钟";
        if (code == 170) return ">168小时";
        if (code is >= 1 and <= 168) return $"约 {code} 小时";
        return $"未知间隔编码 {code}";
    }
}
