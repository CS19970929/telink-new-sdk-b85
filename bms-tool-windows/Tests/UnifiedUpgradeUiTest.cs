using BmsTool.Windows;
using System.Buffers.Binary;
using System.IO;
using System.Reflection;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using System.Windows.Interop;

static class Test
{
    static void Check(bool ok,string why) { if(!ok)throw new Exception(why); }
    [STAThread] public static void Main(string[] args) {
        string output=Path.GetFullPath(args[1]);Directory.CreateDirectory(output);
        var app=new Application{ShutdownMode=ShutdownMode.OnExplicitShutdown};
        var window=new MainWindow();
        try {
            var load=typeof(MainWindow).GetMethod("LoadFirmwareCandidate",BindingFlags.Instance|BindingFlags.NonPublic)!;
            var path=typeof(MainWindow).GetField("_firmwarePath",BindingFlags.Instance|BindingFlags.NonPublic)!;
            var label=(TextBlock)window.FindName("FirmwareText");
            load.Invoke(window,new object[]{args[0]});
            Check((string?)path.GetValue(window)==args[0] && FirmwareImage.Inspect(args[0]).SerialPackage?.BoardId==0xC0600020,
                "C060 selection validates the actual package identity");
            byte[] sample=new byte[1056];
            void Put(int at,uint value)=>BinaryPrimitives.WriteUInt32LittleEndian(sample.AsSpan(at),value);
            Put(0,SerialFirmwarePackage.Magic);BinaryPrimitives.WriteUInt16LittleEndian(sample.AsSpan(4),2);
            BinaryPrimitives.WriteUInt16LittleEndian(sample.AsSpan(6),32);
            Put(8,0x1234ABCD);Put(12,0x08004000);Put(16,1024);Put(20,0x00010000);
            Put(32,0x20002000);Put(36,0x080040D1);Put(224,0x1234ABCD);Put(228,0x08004000);Put(232,0x00010000);Put(236,900);
            Put(24,SerialFirmwarePackage.Crc32(sample.AsSpan(32)));Put(28,SerialFirmwarePackage.Crc32(sample.AsSpan(0,28)));
            string fixture=Path.Combine(output,"unknown-board.sample");File.WriteAllBytes(fixture,sample);
            load.Invoke(window,new object[]{fixture});
            Check((string?)path.GetValue(window)==fixture && FirmwareImage.Inspect(fixture).SerialPackage?.BoardId==0x1234ABCD,
                "new board selection needs no compiled product table");
            sample[28]^=1;File.WriteAllBytes(fixture,sample);bool refused=false;
            try {load.Invoke(window,new object[]{fixture});}catch(TargetInvocationException ex)when(ex.InnerException is InvalidDataException){refused=true;}
            Check(refused && path.GetValue(window) is null && (label.Text is "未选择固件" or "未选择升级包"),"bad package clears selected candidate before any device action");
            load.Invoke(window,new object[]{args[0]});
            Check(window.FindName("SerialIapRecoveryBox") is null,"the standard upgrade page has one start action");
            var modes=(ComboBox)window.FindName("ConnectionModeBox");
            modes.SelectedIndex=0;
            var devices=(ComboBox)window.FindName("DeviceList");
            devices.ItemsSource=new[]{new DiscoveredDevice(0xECF64CE9F13D,"BT-1234567890",-50)};devices.SelectedIndex=0;
            var select=typeof(MainWindow).GetMethod("SelectYmodemEndpoint",BindingFlags.Instance|BindingFlags.NonPublic)!;
            var address=typeof(MainWindow).GetField("_connectedAddress",BindingFlags.Instance|BindingFlags.NonPublic)!;
            var port=typeof(MainWindow).GetField("_connectedSerialPort",BindingFlags.Instance|BindingFlags.NonPublic)!;
            select.Invoke(window,null);
            Check((ulong?)address.GetValue(window)==0xECF64CE9F13D && port.GetValue(window) is null,
                "standard upgrade pins the selected BLE address without an APP connection");
            devices.SelectedIndex=-1;
            refused=false;
            try { select.Invoke(window,null); } catch(TargetInvocationException ex) when(ex.InnerException is InvalidOperationException) { refused=true; }
            Check(refused,"no selected BLE device must not reuse a stale address");
            modes.SelectedIndex=1;
            var ports=(ComboBox)window.FindName("SerialPortList");
            ports.ItemsSource=new[]{new SerialPortEndpoint("COM77")};
            ports.SelectedIndex=0;
            select.Invoke(window,null);
            Check((string?)port.GetValue(window)=="COM77" && address.GetValue(window) is null,
                "standard upgrade pins the selected serial port without an APP connection");
            var tabs=(TabControl)window.FindName("MainTabs");
            var ota=tabs.Items.OfType<TabItem>().Single(t=>t.Header?.ToString()?.Contains("OTA",StringComparison.OrdinalIgnoreCase)==true);
            Check(ota.Visibility==Visibility.Visible,"OTA page must exist in both editions");tabs.SelectedItem=ota;
            // Detach from the unshown Window: a hidden ancestor otherwise
            // makes RenderTargetBitmap fully transparent. No Loaded scan runs.
            var content=(FrameworkElement)window.Content;window.Content=null;
            using var surface=new HwndSource(new HwndSourceParameters("BMS offline UI verification") { Width=1366,Height=900,PositionX=-32000,PositionY=-32000,WindowStyle=unchecked((int)0x90000000),ExtendedWindowStyle=0x08000080 });
            surface.RootVisual=content;
            content.Measure(new Size(1366,900));content.Arrange(new Rect(0,0,1366,900));content.UpdateLayout();
            window.Dispatcher.Invoke(()=>{},DispatcherPriority.Render);
            Check(content.IsVisible && content.ActualHeight>=800,"offscreen content has a visible layout");
            var bitmap=new RenderTargetBitmap(1366,900,96,96,PixelFormats.Pbgra32);bitmap.Render(content);
            byte[] pixels=new byte[1366*900*4];bitmap.CopyPixels(pixels,1366*4,0);
            Check(Enumerable.Range(0,1366*900).Count(i=>pixels[4*i+3]!=0)>1366*900/2,"rendered page is not transparent");
            var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(bitmap));
            using(var stream=File.Create(Path.Combine(output,"ota-page.png")))encoder.Save(stream);
            Console.WriteLine(JsonSerializer.Serialize(new {status="pass",c060CandidateAccepted=true,unknownBoardAccepted=true,badCrcClearsCandidate=true,
                automaticBootEndpoint=true,otaVisible=true,firmwareLabel=label.Text,screenshot=Path.Combine(output,"ota-page.png")}));
        } finally {window.Close();app.Shutdown();}
    }
}
