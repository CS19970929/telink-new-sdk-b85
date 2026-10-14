using Android.Content;
using Android.Text;
using Android.Widget;
using BmsTool.Windows;

namespace BmsTool.Android;

public sealed partial class MainActivity
{
    private TextView? _batteryTypeStatus;

    private Task<string?> PromptServicePasswordAsync()
    {
        var completion = new TaskCompletionSource<string?>(TaskCreationOptions.RunContinuationsAsynchronously);
        RunOnUiThread(() => {
            var input = new EditText(this) { Hint = "服务密码", InputType = InputTypes.ClassText | InputTypes.TextVariationPassword };
            input.SetSingleLine(true);
            var builder = new AlertDialog.Builder(this);
            builder.SetTitle("服务权限验证"); builder.SetView(input);
            builder.SetNegativeButton("取消", (_,_) => { });
            builder.SetPositiveButton("确认", (_,_) => { });
            builder.SetOnCancelListener(new DialogCancelListener(() => completion.TrySetResult(null)));
            var dialog = builder.Create();
            if (dialog is null) { completion.TrySetResult(null); return; }
            dialog.Show();
            dialog.GetButton((int)DialogButtonType.Negative)!.Click += (_,_) => {
                input.Text = string.Empty; completion.TrySetResult(null); dialog.Dismiss();
            };
            dialog.GetButton((int)DialogButtonType.Positive)!.Click += (_,_) => {
                string password = input.Text ?? string.Empty;
                input.Text = string.Empty; completion.TrySetResult(password); dialog.Dismiss();
            };
        });
        return completion.Task;
    }

    private Task ReadBatteryTypeFromUiAsync() => WithClientAsync("读取电池类型", async (client,ct) => {
        var state = await client.ReadBatteryTypeAsync(ct);
        RunOnUiThread(() => _batteryTypeStatus!.Text = state[2] == 1 ? "当前类型：磷酸铁锂" : state[2] == 2 ? "当前类型：三元锂" : "当前类型：自动选择，请核对实际电芯");
    });

    private async Task SwitchBatteryTypeFromUiAsync(ushort chemistry)
    {
        if (!EnsureServiceAccess("切换电池类型")) return;
        var expectedClient = _client;
        if (expectedClient is null) { SetStatus("请先连接设备",true); return; }
        if (AppEdition.IsInternal) {
            string? password = await PromptServicePasswordAsync();
            if (password is null) return;
            if (!BmsServiceAccess.PasswordValid(password)) { SetStatus("服务密码错误",true); return; }
        }
        string name = chemistry == 1 ? "磷酸铁锂" : "三元锂";
        if (!await ConfirmAsync("切换为" + name,
            "请确认实际电芯类型匹配，并先断开充电器和负载。将恢复该类型的软件/硬件保护、均衡和 SOC/OCV 默认；保留容量、均衡许可、实测校准及历史状态。是否继续？")) return;
        await WithClientAsync("切换电池类型",async (client,ct) => {
            if (!ReferenceEquals(expectedClient,client)) throw new InvalidOperationException("连接已改变，请重新确认设备。");
            _parameterCapture = null; _protectionModel = null; _afeModel = null;
            _socProfile = null;
            RunOnUiThread(() => {
                _batteryTypeStatus!.Text = "正在切换，尚未确认设备结果…";
                _protectionInputs.Clear(); _afeInputs.Clear();
                _protectionRows?.RemoveAllViews(); _afeRows?.RemoveAllViews();
                _protectionStatus!.Text = "正在切换，请等待结果后重新读取保护参数";
                _afeStatus!.Text = "正在切换，请等待结果后重新读取硬件保护";
            });
            try {
                var result = await client.SwitchBatteryTypeAsync(chemistry,BmsServiceAccess.DefaultPassword,ct);
                _parameterCapture = null; _protectionModel = null; _afeModel = null;
                _socProfile = result.SocConfiguration;
                RunOnUiThread(() => {
                    _batteryTypeStatus!.Text = result.Summary;
                    ShowSocProfile(result.SocConfiguration);
                    _protectionInputs.Clear(); _afeInputs.Clear();
                    _protectionRows?.RemoveAllViews(); _afeRows?.RemoveAllViews();
                    _protectionStatus!.Text = "电池类型已切换，请重新读取保护参数";
                    _afeStatus!.Text = "电池类型已切换，请重新读取硬件保护";
                });
                SetStatus(result.Summary);
            } catch {
                RunOnUiThread(() => _batteryTypeStatus!.Text = "本次结果未确认；请重新读取电池类型与保护参数。");
                throw;
            }
        });
    }
}
