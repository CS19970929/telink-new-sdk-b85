using ZXing.Net.Maui;

namespace TelinkOta.Mobile;

public partial class QrScannerPage : ContentPage
{
    private readonly TaskCompletionSource<string?> _result =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    private int _completed;

    public QrScannerPage()
    {
        InitializeComponent();
        BarcodeReader.Options = new BarcodeReaderOptions
        {
            Formats = BarcodeFormats.TwoDimensional,
            AutoRotate = true,
            TryHarder = true,
            Multiple = false,
        };
    }

    public Task<string?> Result => _result.Task;

    protected override async void OnAppearing()
    {
        base.OnAppearing();
        var permission = await Permissions.RequestAsync<Permissions.Camera>();
        if (permission != PermissionStatus.Granted)
        {
            HintLabel.Text = "没有相机权限，无法扫码。";
            await DisplayAlert("需要相机权限", "请在系统设置中允许 Telink BMS 使用相机。", "确定");
            await CompleteAsync(null);
            return;
        }

        HintLabel.Text = "请扫描设备二维码";
    }

    private async void BarcodesDetected(object sender, BarcodeDetectionEventArgs e)
    {
        string? value = e.Results.FirstOrDefault()?.Value;
        if (string.IsNullOrWhiteSpace(value)) return;
        await MainThread.InvokeOnMainThreadAsync(() => CompleteAsync(value.Trim()));
    }

    private async void CancelClicked(object sender, EventArgs e) => await CompleteAsync(null);

    private async Task CompleteAsync(string? value)
    {
        if (Interlocked.Exchange(ref _completed, 1) != 0) return;
        BarcodeReader.IsDetecting = false;
        _result.TrySetResult(value);
        if (Navigation.ModalStack.LastOrDefault() == this)
            await Navigation.PopModalAsync();
    }
}
