using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using BmsTool.Windows;
static class AfeEditorUiTest {
    static void Check(bool ok,string message) {if(!ok)throw new Exception(message);}
    [STAThread] static void Main(string[] args) {
        var app=new Application();var window=new MainWindow();
        const BindingFlags flags=BindingFlags.Instance|BindingFlags.NonPublic;
        typeof(MainWindow).GetMethod("AddAfeHardwareTab",flags)!.Invoke(window,null);
        T Field<T>(string name)=>(T)typeof(MainWindow).GetField(name,flags)!.GetValue(window)!;
        var model=Field<AfeHardwareParameterModel>("_afeHardwareModel");
        ushort[] p={1,0x3510,3760,1000,3500,1000,2400,3000,2800,1000,200,140,300,50,100,2000,200,140,0,0,10,100,600,64,2000,1050,850,200,300,1150,1050,0,100,1000,0x13df};
        var info=new AfeHardwareDeviceInfo(0x3510,0x1bdf,true,667,8,32,false,1,0,2){ShuntElementMicroOhm=2000,ShuntParallelCount=3,HardwareFeatures=7,FeatureStatus=new ushort[]{1,2,256,0,0,0}};
        model.Load(new(p,p,info));typeof(MainWindow).GetMethod("ApplyAfeDeviceInfo",flags)!.Invoke(window,new object[]{info});
        var grid=Field<DataGrid>("_afeHardwareGrid");
        var column=grid.Columns.OfType<DataGridTemplateColumn>().Single(c=>Equals(c.Header,"开关"));
        Check(model.Rows.Count(r=>r.IsSwitch)==9,"五种阈值保护和四个温度通道均显示");
        Check(grid.Columns.Any(c=>Equals(c.Header,"来源"))&&grid.Columns.Any(c=>Equals(c.Header,"预计生效")),"来源与预计生效列");
        column.CellTemplate.Seal();
        var sc=model.Rows.Single(r=>r.IsSwitch&&r.CapabilityMask==64);
        var toggle=(CheckBox)column.CellTemplate.LoadContent();toggle.DataContext=sc;
        System.Windows.Threading.Dispatcher.CurrentDispatcher.Invoke(()=>{},System.Windows.Threading.DispatcherPriority.DataBind);
        toggle.GetBindingExpression(ToggleButton.IsCheckedProperty)!.UpdateTarget();
        toggle.GetBindingExpression(UIElement.IsEnabledProperty)!.UpdateTarget();
        Check(toggle.IsEnabled&&toggle.IsChecked==true,$"真实模板读取开关值 enabled={toggle.IsEnabled}, checked={toggle.IsChecked}, source={sc.SwitchValue}");
        toggle.IsChecked=false;toggle.GetBindingExpression(ToggleButton.IsCheckedProperty)!.UpdateSource();
        Check(model.TryBuildCandidate(out var candidate,out string error)&& (candidate[34]&64)==0,error);
        var nc=(CheckBox)column.CellTemplate.LoadContent();nc.DataContext=model.Rows.Single(r=>r.IsTemperatureChannel&&r.CapabilityMask==1024);
        System.Windows.Threading.Dispatcher.CurrentDispatcher.Invoke(()=>{},System.Windows.Threading.DispatcherPriority.DataBind);
        nc.GetBindingExpression(UIElement.IsEnabledProperty)!.UpdateTarget();
        Check(!nc.IsEnabled,"NC通道真实控件禁用");
        Check(Field<TextBlock>("_afeHardwareDeviceInfo").Text.Contains("本产品不支持")&&Field<TextBlock>("_afeHardwareDeviceInfo").Text.Contains("压差"),"AFE页显示硬件能力和均衡原因");
        var tab=Field<TabItem>("_afeHardwareTab");tab.Visibility=Visibility.Visible;
        ((TabControl)window.FindName("MainTabs")).SelectedItem=tab;
        window.Width=1500;window.Height=900;window.Measure(new Size(1500,900));window.Arrange(new Rect(0,0,1500,900));window.UpdateLayout();
        if(args.Length>0) {
            var root=(Grid)tab.Content;tab.Content=null;root.Background=Brushes.White;
            root.Measure(new Size(1500,900));root.Arrange(new Rect(0,0,1500,900));root.UpdateLayout();
            var image=new RenderTargetBitmap(1500,900,96,96,PixelFormats.Pbgra32);image.Render(root);
            var png=new PngBitmapEncoder();png.Frames.Add(BitmapFrame.Create(image));
            Directory.CreateDirectory(args[0]);using var file=File.Create(Path.Combine(args[0],"afe-editor.png"));png.Save(file);
        }
        window.Close();Console.WriteLine("PASS AFE editor UI: switches, NC gate, live binding, source/preview columns and feature reasons");
    }
}
