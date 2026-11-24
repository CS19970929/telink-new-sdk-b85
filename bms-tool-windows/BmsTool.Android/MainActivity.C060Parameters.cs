using Android.App;
using Android.Content;
using Android.Views;
using Android.Widget;
using BmsTool.Windows;

namespace BmsTool.Android;

public sealed partial class MainActivity
{
    private const int C060ImportRequest=1003;
    private IReadOnlyList<C060ParameterField> C060ParameterFields=>C060Parameters.FieldsFor(_c060Capture?.Product=="C077");
    private C060ParameterCapture? _c060Capture;
    private BmsClient? _c060CaptureClient,_protectionCaptureClient,_c060ImportClient;
    private LinearLayout? _c060Panel,_c060RowsPanel,_legacyParameterPanel;
    private TextView? _c060Status;
    private CheckBox? _c060Compatibility;
    private Button? _c060SaveButton;
    private string? _c060ImportMac;
    private readonly Dictionary<ushort,EditText> _c060Inputs=new();

    private void BuildC060ParameterControls(LinearLayout page)
    {
        page.AddView(ActionButton("读取全部参数（自动识别）",ReadParametersAsync));
        page.AddView(ActionButton("旧版 C060：核对并读取",()=>WithClientAsync("核对旧版 C060",async(c,t)=>{
            await c.SelectC060ParametersAsync(t);await ReadC060ParametersCoreAsync(c,t);
        })));
        _c060Panel=Page();_c060Panel.Visibility=ViewStates.Gone;
        _c060Panel.AddView(SectionTitle("基本参数"));
        _c060Panel.AddView(Note("读取值与待保存值分开。保存前核对整组，保留未编辑字段；保护参数在保护页保存。"));
        _c060Status=DataBlock("未读取");_c060Panel.AddView(_c060Status);
        _c060Compatibility=new CheckBox(this){Text="允许编辑兼容字段（部分不参与当前运行）"};
        _c060Compatibility.CheckedChange+=(_,_)=>UpdateC060EditorState();
        _c060Panel.AddView(_c060Compatibility);
        _c060SaveButton=ActionButton("保存全部常规参数修改",SaveC060ParametersAsync);
        _c060Panel.AddView(_c060SaveButton);
        _c060Panel.AddView(ActionButton("导出当前参数 JSON",ExportC060ParametersAsync));
        _c060Panel.AddView(ActionButton("导入 JSON 并预览",PickC060Parameters));
        _c060RowsPanel=Page();_c060Panel.AddView(_c060RowsPanel);page.AddView(_c060Panel);
    }
    private void ClearC060ParameterState()
    {
        _c060Capture=null;_c060CaptureClient=null;_protectionCaptureClient=null;_c060Inputs.Clear();
        RunOnUiThread(()=>{
            _c060RowsPanel?.RemoveAllViews();
            if(_c060Panel is not null)_c060Panel.Visibility=ViewStates.Gone;
            if(_legacyParameterPanel is not null)_legacyParameterPanel.Visibility=ViewStates.Visible;
            if(_c060Status is not null)_c060Status.Text="未读取当前设备参数";
        });
    }
    private async Task ReadC060ParametersCoreAsync(BmsClient client,CancellationToken ct)
    {
        var capture=await client.ReadC060ParametersAsync(false,ct);
        capture.Incomplete=new[]{"手机参数包不包含校准数据，不用于整机克隆。"};
        _c060Capture=capture;_c060CaptureClient=client;
        RunOnUiThread(()=>{
            RenderC060Parameters();
            if(capture.Blocks.TryGetValue("Protection",out var words)) {
                var rows=ProtectionParameterCatalog.Create();
                for(int i=0;i<rows.Count;i++)rows[i].LoadFromDevice(words[i]);
                _protectionModel=rows;_protectionCaptureClient=client;RenderProtectionRows(rows);
            }
        });
        SetStatus(capture.Errors.Count==0?"保护与常规参数读取完成":"参数读取有提示；连接保持可用",capture.Errors.Count!=0);
    }
    private void RenderC060Parameters()
    {
        if(_c060Capture is null)return;
        if(_c060Compatibility is not null)_c060Compatibility.Visibility=ViewStates.Gone;
        _c060Panel!.Visibility=ViewStates.Visible;_legacyParameterPanel!.Visibility=ViewStates.Gone;
        _c060RowsPanel!.RemoveAllViews();_c060Inputs.Clear();
        foreach(var group in C060ParameterFields.Where(f=>f.Address>=0x2100).GroupBy(f=>f.Group)) {
            var rows=Page();
            foreach(var field in group) {
                if(!C060Parameters.TryValue(_c060Capture,field,out ushort raw))continue;
                rows.AddView(Note($"{field.Name} · 当前 {field.Format(raw)} {field.Unit}"));
                if(!string.IsNullOrWhiteSpace(field.Hint))rows.AddView(Note(field.Hint));
                if(field.Editable && (!field.FactoryOnly||AppEdition.IsInternal)) {
                    var input=Input(field.Name+" / "+field.Unit,field.Format(raw));
                    input.InputType=global::Android.Text.InputTypes.ClassNumber|global::Android.Text.InputTypes.NumberFlagDecimal|global::Android.Text.InputTypes.NumberFlagSigned;
                    _c060Inputs[field.Address]=input;input.TextChanged+=(_,_)=>UpdateC060EditorState();rows.AddView(input);
                }
            }
            AddFold(_c060RowsPanel,group.Key,rows);
        }
        _c060Status!.Text=$"已读取 {_c060Capture.Blocks.Count} 个块"+(_c060Capture.Errors.Count==0?"":"\n"+string.Join("\n",_c060Capture.Errors))+"\n手机参数包不包含校准数据。";
        UpdateC060EditorState();
    }
    private void UpdateC060EditorState()
    {
        bool current=_client is not null && ReferenceEquals(_client,_c060CaptureClient),valid=true;int changed=0;
        if(_c060Compatibility is not null)_c060Compatibility.Enabled=current&&!_uiBusy&&_mobileCalibrationSession is null;
        foreach(var pair in _c060Inputs) {
            var field=C060ParameterFields.Single(f=>f.Address==pair.Key);
            bool allowed=field.Editable&&(!field.FactoryOnly||AppEdition.IsInternal)&&(!field.Compatibility||_c060Compatibility?.Checked==true);
            pair.Value.Enabled=current&&!_uiBusy&&_mobileCalibrationSession is null&&allowed;
            try {
                ushort raw=field.Parse(pair.Value.Text??"");
                pair.Value.Error=null;
                if(allowed&&_c060Capture is not null&&C060Parameters.TryValue(_c060Capture,field,out ushort old)&&raw!=old)changed++;
            } catch(ArgumentException) {pair.Value.Error="请输入符合单位和范围的数值";valid=false;}
        }
        if(_c060SaveButton is not null) {
            _c060SaveButton.Text=$"保存全部常规参数修改（{changed}项）";
            _c060SaveButton.Enabled=current&&!_uiBusy&&_mobileCalibrationSession is null&&valid&&changed>0&&(AppEdition.IsInternal||_serviceMode);
        }
    }
    private async Task SaveC060ParametersAsync()
    {
        if(!EnsureServiceAccess("保存 设备参数"))return;
        var owner=_client;
        if(owner is null||_c060Capture is null||!ReferenceEquals(owner,_c060CaptureClient)){SetStatus("请先读取当前设备的 设备参数",true);return;}
        var edits=_c060Inputs.Where(p=>p.Value.Enabled).ToDictionary(p=>p.Key,p=>p.Value.Text??"");
        var changes=C060Parameters.CollectChanges(_c060Capture,edits,_c060Compatibility?.Checked==true,AppEdition.IsInternal);
        if(changes.Count==0){SetStatus("设备参数没有修改");return;}
        string detail=string.Join("\n",changes.Select(p=>{
            var f=C060ParameterFields.Single(f=>f.Address==p.Key);C060Parameters.TryValue(_c060Capture,f,out ushort old);
            return $"{f.Name}：{f.Format(old)} → {f.Format(p.Value)} {f.Unit}";
        }));
        if(!await ConfirmAsync("保存 设备参数",detail+"\n\n确认后按完整参数组写入并回读；中断时部分组可能已保存。"))return;
        await WithClientAsync("保存 设备参数",async(c,t)=>{
            if(!ReferenceEquals(c,owner))throw new IOException("连接已变化，请重新读取参数。");
            await c.WriteC060ParameterChangesAsync(changes,(_,_)=>{},_c060Compatibility?.Checked==true,t);
            await ReadC060ParametersCoreAsync(c,t);
        },TimeSpan.FromMinutes(5));
    }
    private Task ExportC060ParametersAsync()
    {
        if(_c060Capture is null||!ReferenceEquals(_client,_c060CaptureClient)){SetStatus("请先读取当前设备参数",true);return Task.CompletedTask;}
        string path=OutputPath($"{_c060Capture.Product}_parameters_{DateTimeOffset.Now:yyyyMMdd_HHmmss}.json");
        C060Parameters.Export(path,_c060Capture);SetStatus("参数 JSON 已保存："+path);return Task.CompletedTask;
    }
    private Task PickC060Parameters()
    {
        if(_c060Capture is null||!ReferenceEquals(_client,_c060CaptureClient)){SetStatus("请先读取当前设备参数",true);return Task.CompletedTask;}
        _c060ImportMac=_connectedMac;_c060ImportClient=_client;
        var intent=new Intent(Intent.ActionOpenDocument);intent.AddCategory(Intent.CategoryOpenable);intent.SetType("*/*");
        StartActivityForResult(intent,C060ImportRequest);return Task.CompletedTask;
    }
    private async Task ImportC060ParametersGuardedAsync(global::Android.Net.Uri uri)
    {
        try {await ImportC060ParametersAsync(uri);}
        catch(Exception ex){SetStatus("导入参数失败："+ex.Message,true);}
    }
    private async Task ImportC060ParametersAsync(global::Android.Net.Uri uri)
    {
        string path=Path.Combine(CacheDir!.AbsolutePath,Guid.NewGuid()+".json");
        try {
            if(_client is null||_c060Capture is null||!ReferenceEquals(_client,_c060CaptureClient)||(_connectedMac!=_c060ImportMac||!ReferenceEquals(_client,_c060ImportClient)))
                throw new IOException("连接已变化，请重新读取后导入。");
            using var input=ContentResolver!.OpenInputStream(uri)??throw new IOException("无法读取参数文件。");
            using(var output=File.Create(path)) {
                byte[] buffer=new byte[8192];int total=0,n;
                while((n=await input.ReadAsync(buffer))>0){total+=n;if(total>1024*1024)throw new InvalidDataException("参数文件超过1MiB");await output.WriteAsync(buffer.AsMemory(0,n));}
            }
            var imported=C060Parameters.Import(path,_c060Capture);
            if(_connectedMac!=_c060ImportMac||!ReferenceEquals(_client,_c060ImportClient)||!ReferenceEquals(_client,_c060CaptureClient))throw new IOException("连接已变化，未载入候选。");
            foreach(var field in C060ParameterFields.Where(f=>f.Editable&&!f.FactoryOnly&&!f.Compatibility))
                if(_c060Inputs.TryGetValue(field.Address,out var edit)&&C060Parameters.TryValue(imported,field,out ushort raw))edit.Text=field.Format(raw);
            if(imported.Blocks.TryGetValue("Protection",out var protection)&&_protectionModel is not null) {
                var candidates=ProtectionParameterCatalog.Create();
                for(int i=0;i<candidates.Count;i++){candidates[i].LoadFromDevice(protection[i]);_protectionModel[i].EditValue=candidates[i].EditValue;}
                RenderProtectionRows(_protectionModel);
            }
            UpdateC060EditorState();SetStatus("已载入候选，尚未写入；保护与常规参数分别确认保存。工厂校准、身份和兼容字段未自动导入。");
        } finally {if(File.Exists(path))File.Delete(path);}
    }
}
