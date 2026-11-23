using BmsTool.Android;
using BmsTool.Windows;

static class Tests
{
    private const string Mac="EC:F6:4C:E9:F1:3D";
    private static int checks;
    static void Check(bool ok,string why){if(!ok)throw new Exception(why);checks++;}
    static async Task ChangedDuringPrompt(Action<ServiceState> change)
    {
        var state=new ServiceState{_selectedMac=Mac};
        var reply=new TaskCompletionSource<string?>(TaskCreationOptions.RunContinuationsAsynchronously);
        state.Prompt=()=>reply.Task;
        Task pending=state.Enter();change(state);reply.SetResult(BmsServiceAccess.DefaultPassword);await pending;
        Check(!state._serviceMode,"selection/session changed during password prompt cannot authorize upgrade");
    }
    public static async Task Main()
    {
        AppEdition.IsInternal=false;
        var state=new ServiceState();state.Go(8);
        Check(state.CurrentPage==6,"customer firmware deep-link returns to service settings before authentication");
        Check(!state.Access(),"customer upgrade denied outside service mode");
        await state.Enter();Check(state.PromptCalls==0&&!state._serviceMode,"no remembered target or implicit first device");
        state=new ServiceState{_selectedMac="bad"};await state.Enter();
        Check(state.PromptCalls==0,"invalid target cannot open service access");
        state=new ServiceState{_selectedMac=Mac};await state.Enter();
        Check(state._client is null&&state._serviceMode,"explicitly selected Boot module can enter service mode without APP probe");
        Check(state.ServiceActionsVisible&&state.Access(),"customer controls and operation guard authorize together");
        state.Go(8);Check(state.CurrentPage==8,"authenticated customer can open firmware page");
        state.ReleaseTransport(Mac);
        Check(state.Access()&&state.ServiceActionsVisible&&state.CurrentPage==8&&state.PromptCalls==1,
            "same-device YMODEM handoff lets the customer retry from the firmware page");
        await state.Enter();Check(!state._serviceMode&&!state.ServiceActionsVisible&&state.CurrentPage==6,"exit hides controls and closes service page");
        Check(!state.Access(),"upgrade guard rejects after exit");
        state.ReleaseTransport(Mac);
        Check(!state.Access(),"same-device handoff does not grant service access by itself");
        await state.Enter();state.ReleaseTransport();
        Check(!state.Access()&&!state.ServiceActionsVisible,"manual disconnect clears service access");
        await state.Enter();state._selectedMac="00:11:22:33:44:55";state.ReleaseTransport(Mac);
        Check(!state.Access()&&!state.ServiceActionsVisible,"changing the upgrade target clears service access");
        state=new ServiceState{_selectedMac=Mac,Prompt=()=>Task.FromResult<string?>("wrong")};await state.Enter();
        Check(!state._serviceMode,"wrong password cannot authorize upgrade");
        state=new ServiceState{_selectedMac=Mac,Prompt=()=>Task.FromResult<string?>(null)};await state.Enter();
        Check(!state._serviceMode,"cancelled password cannot authorize upgrade");
        await ChangedDuringPrompt(s=>s._selectedMac="00:11:22:33:44:55");
        await ChangedDuringPrompt(s=>s._selectedMac=null);
        await ChangedDuringPrompt(s=>s._client=new object());
        state=new ServiceState{_selectedMac=Mac,_client=new object()};await state.Enter();
        Check(state._serviceMode,"stable live APP connection keeps normal service workflow");
        AppEdition.IsInternal=true;state=new ServiceState();state.Go(8);
        Check(state.CurrentPage==8&&state.Access(),"internal edition retains engineer firmware access");
        await state.Enter();Check(state.PromptCalls==0&&state.CurrentPage==1,"internal service action needs no customer password");
        Console.WriteLine($"PASS Android service access: {checks} checks; production methods, no GATT or device writes");
    }
}

// UI stand-ins test navigation/authentication policy, not native rendering.
enum ViewStates{Visible,Gone}
enum HideSoftInputFlags{None}
sealed class InputMethodManager{public void HideSoftInputFromWindow(object? token,HideSoftInputFlags flags){}}
sealed class UiElement
{
    public string? Text;
    public ViewStates Visibility=ViewStates.Gone;
    public object? WindowToken=>null;
    public void ClearFocus(){}
}
partial class ServiceState
{
    private const int FirmwarePage=8,DevicePage=6,ProtectionPage=1;
    private const string InputMethodService="input";
    public object? _client;
    public string? _selectedMac;
    public bool _serviceMode;
    private readonly UiElement _serviceModeButton=new(),_serviceModeStatus=new(),_serviceActions=new();
    private readonly UiElement _pageTitle=new(),_upgradeTargetView=new(),_batteryTypeStatus=new();
    private readonly List<UiElement> _pages=Enumerable.Range(0,9).Select(_=>new UiElement()).ToList();
    private readonly List<UiElement> _navButtons=new();
    private readonly List<int> _navDestinations=new();
    public Func<Task<string?>> Prompt=()=>Task.FromResult<string?>(BmsServiceAccess.DefaultPassword);
    public int PromptCalls;
    private UiElement? CurrentFocus=>null;
    public int CurrentPage=>_pages.FindIndex(p=>p.Visibility==ViewStates.Visible);
    public bool ServiceActionsVisible=>_serviceActions.Visibility==ViewStates.Visible;
    private Task<string?> PromptServicePasswordAsync(){PromptCalls++;return Prompt();}
    private void SetStatus(string text,bool error=false){}
    private void UpdateBatteryFreshness(){}
    private object? GetSystemService(string name)=>null;
    private void StyleSelection(UiElement button,bool selected){}
    private void RunOnUiThread(Action action)=>action();
    public void ReleaseTransport(string? upgradeMac=null)=>UpdateServiceAfterTransportRelease(upgradeMac);
    public Task Enter()=>EnterServiceModeAsync();
    public bool Access()=>EnsureServiceAccess("固件升级");
    public void Go(int page)=>ShowPage(page);
}
namespace BmsTool.Android {static class AppEdition{public static bool IsInternal{get;set;}}}
