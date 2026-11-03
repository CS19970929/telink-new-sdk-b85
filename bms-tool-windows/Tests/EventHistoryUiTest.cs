using System.Collections;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using BmsTool.Windows;
static class Test
{
 const BindingFlags Private=BindingFlags.Instance|BindingFlags.NonPublic;
 static void Check(bool b,string why){if(!b)throw new Exception(why);}
 [STAThread] static void Main(string[] args)
 {
  var app=new Application();var window=new MainWindow();var type=typeof(MainWindow);
  object? Field(string n)=>type.GetField(n,Private)!.GetValue(window);
  void Call(string n)=>type.GetMethod(n,Private)!.Invoke(window,null);
  Call("AddEventLogTab");var tabs=(TabControl)window.FindName("MainTabs");
  var tab=tabs.Items.OfType<TabItem>().Single(t=>Equals(t.Header,"事件日志"));tabs.SelectedItem=tab;
  var rows=(IList)Field("_deviceEventLogs")!;
  rows.Add(new DeviceEventLogRow(1,"单体欠压","—","发生",10,new DateTimeOffset(2026,11,3,12,34,56,TimeSpan.Zero),120,"软件保护","3级",2790,3500,-123,250,600,3));
  rows.Add(new DeviceEventLogRow(2,"AFE通信异常","—","稳定恢复",9,null,90,"系统","—",null,null,null,null,600));
  var view=CollectionViewSource.GetDefaultView(rows);Check(view.Cast<object>().Count()==2,"默认显示两条");
  ((ComboBox)Field("_eventSource")!).SelectedIndex=2;Call("RefreshEventFilter");Check(view.Cast<object>().Count()==1,"来源筛选");
  ((ComboBox)Field("_eventSource")!).SelectedIndex=0;
  ((DatePicker)Field("_eventFrom")!).SelectedDate=new DateTime(2026,11,3);Call("RefreshEventFilter");Check(view.Cast<object>().Count()==1,"日期筛选排除未知时间");
  ((DatePicker)Field("_eventFrom")!).SelectedDate=null;
  ((TextBox)Field("_eventSearch")!).Text="AFE";Call("RefreshEventFilter");Check(view.Cast<object>().Count()==1,"事件名称筛选");
  ((TextBox)Field("_eventSearch")!).Text="";Call("RefreshEventFilter");
  foreach(var size in new[]{new Size(1440,900),new Size(1000,700)}){
   var root=(FrameworkElement)window.Content;root.Measure(size);root.Arrange(new Rect(size));root.UpdateLayout();
   var bitmap=new RenderTargetBitmap((int)size.Width,(int)size.Height,96,96,PixelFormats.Pbgra32);bitmap.Render(root);
   var png=new PngBitmapEncoder();png.Frames.Add(BitmapFrame.Create(bitmap));
   System.IO.Directory.CreateDirectory(args[0]);using var file=System.IO.File.Create(System.IO.Path.Combine(args[0],$"event-ui-{(int)size.Width}.png"));png.Save(file);
  }
  Call("ClearDeviceDisplays");Check(rows.Count==0,"断线清空事件显示");
  window.Close();app.Shutdown();Console.WriteLine("PASS real WPF event UI: shared page, date/source/name filters, unknown time, 1440/1000 layout, stale cleanup; no device IO");
 }
}
