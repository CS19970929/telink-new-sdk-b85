using Android;
using Android.App;
using Android.Content;
using Android.Content.PM;
using Android.Graphics;
using Android.OS;
using Android.Views;
using Android.Widget;
using ZXing;
using ZXing.Common;
using Camera=Android.Hardware.Camera;

namespace BmsTool.Android;

// API1 supplies a bounded NV21 preview on our Android 6+ native Activity without
// a second UI framework or Play Services. Release it on every pause/surface loss.
#pragma warning disable CS0618, CA1422
[Activity(Label="扫码连接",Exported=false,ScreenOrientation=ScreenOrientation.Portrait)]
public sealed class QrScanActivity : Activity,ISurfaceHolderCallback,Camera.IPreviewCallback
{
    internal const string MacExtra="bms_mac";
    private const int CameraPermissionRequest=2001;
    private SurfaceView? _preview;
    private TextView? _message;
    private Camera? _camera;
    private int _width,_height,_decoding,_session;
    private long _nextFrameMs;
    private bool _resumed,_surfaceReady,_permissionRequested,_completed;
    private readonly BarcodeReaderGeneric _reader=new() {
        AutoRotate=true,
        Options=new DecodingOptions {TryHarder=true,PossibleFormats=new[]{BarcodeFormat.QR_CODE}}
    };

    protected override void OnCreate(Bundle? savedInstanceState)
    {
        base.OnCreate(savedInstanceState);
        Window?.AddFlags(WindowManagerFlags.KeepScreenOn);
        var root=new LinearLayout(this){Orientation=Orientation.Vertical};
        root.SetPadding(20,24,20,24);
        // Respect system bars, including Android 15+ enforced edge-to-edge.
        root.SetOnApplyWindowInsetsListener(new MainActivity.MobileInsetsListener());
        _message=new TextView(this){Text="将蓝牙MAC二维码完整放入画面",TextSize=18};
        root.AddView(_message);
        _preview=new SurfaceView(this);
        _preview.Holder?.AddCallback(this);
        root.AddView(_preview,new LinearLayout.LayoutParams(-1,0,1));
        var cancel=new Button(this){Text="取消 / 返回设备"};
        cancel.Click+=(_,_)=>Finish();root.AddView(cancel);
        SetContentView(root);
        if(CheckSelfPermission(Manifest.Permission.Camera)!=Permission.Granted) {
            _permissionRequested=true;
            RequestPermissions(new[]{Manifest.Permission.Camera},CameraPermissionRequest);
        }
    }
    protected override void OnResume(){base.OnResume();_resumed=true;TryStartCamera();}
    protected override void OnPause(){_resumed=false;StopCamera();base.OnPause();}
    public void SurfaceCreated(ISurfaceHolder holder){_surfaceReady=true;TryStartCamera();}
    public void SurfaceChanged(ISurfaceHolder holder,Format format,int width,int height){}
    public void SurfaceDestroyed(ISurfaceHolder holder){_surfaceReady=false;StopCamera();}
    public override void OnRequestPermissionsResult(int requestCode,string[] permissions,Permission[] grantResults)
    {
        base.OnRequestPermissionsResult(requestCode,permissions,grantResults);
        if(requestCode!=CameraPermissionRequest)return;
        _permissionRequested=false;
        if(CheckSelfPermission(Manifest.Permission.Camera)==Permission.Granted)TryStartCamera();
        else _message!.Text="相机权限未开启，可返回使用蓝牙扫描。";
    }
    private void TryStartCamera()
    {
        if(!_resumed||!_surfaceReady||_completed||_camera is not null||_permissionRequested||CheckSelfPermission(Manifest.Permission.Camera)!=Permission.Granted)return;
        try {
            int id=-1;var info=new Camera.CameraInfo();
            for(int i=0;i<Camera.NumberOfCameras;i++){Camera.GetCameraInfo(i,info);if(info.Facing==global::Android.Hardware.CameraFacing.Back){id=i;break;}}
            if(id<0)throw new IOException("没有可用的后置相机");
            Camera.GetCameraInfo(id,info);
            _camera=Camera.Open(id)??throw new IOException("无法打开相机");
            var parameters=_camera.GetParameters()??throw new IOException("无法读取相机配置");
            var sizes=parameters.SupportedPreviewSizes??throw new IOException("相机没有预览尺寸");
            var size=sizes.Where(s=>s.Width>=320&&s.Height>=240&&s.Width<=1280&&s.Height<=960)
                .OrderBy(s=>Math.Abs(s.Width*s.Height-640*480)).FirstOrDefault()
                ??throw new IOException("相机没有支持的扫码预览尺寸");
            _width=size.Width;_height=size.Height;
            if(parameters.SupportedPreviewFormats?.Any(value=>value.IntValue()==(int)ImageFormatType.Nv21)!=true)throw new IOException("相机不支持NV21预览");
            parameters.SetPreviewSize(_width,_height);parameters.PreviewFormat=ImageFormatType.Nv21;
            if(parameters.SupportedFocusModes?.Contains(Camera.Parameters.FocusModeContinuousPicture)==true)
                parameters.FocusMode=Camera.Parameters.FocusModeContinuousPicture;
            _camera.SetParameters(parameters);
            // The Activity is portrait; account for sensor orientation and display rotation.
            int rotation=(int)(WindowManager?.DefaultDisplay?.Rotation??SurfaceOrientation.Rotation0)*90;
            _camera.SetDisplayOrientation((info.Orientation-rotation+360)%360);
            _camera.SetPreviewDisplay(_preview!.Holder);
            _camera.SetPreviewCallbackWithBuffer(this);
            int bytes=checked(_width*_height*3/2);
            _camera.AddCallbackBuffer(new byte[bytes]);_camera.AddCallbackBuffer(new byte[bytes]);
            _camera.StartPreview();
            _message!.Text="将蓝牙MAC二维码完整放入画面";
        } catch(Exception ex) {
            StopCamera();_message!.Text="无法扫码："+ex.Message+"。可返回设备页扫描。";
        }
    }
    public void OnPreviewFrame(byte[]? data,Camera? camera)
    {
        if(data is null||camera is null||_camera is null||!camera.Equals(_camera)||!_resumed||_completed)return;
        try {
            long now=SystemClock.ElapsedRealtime();
            if(now<_nextFrameMs||Interlocked.CompareExchange(ref _decoding,1,0)!=0)return;
            _nextFrameMs=now+250;
            int width=_width,height=_height,session=_session;
            int count=checked(width*height);
            if(data.Length<count){Interlocked.Exchange(ref _decoding,0);return;}
            // Camera owns its callback buffer. Decode a private luminance copy so
            // preview can keep running and no released camera is used by workers.
            byte[] luminance=new byte[count];Array.Copy(data,luminance,count);
            _=Task.Run(()=>{
                try {
                    var source=new PlanarYUVLuminanceSource(luminance,width,height,0,0,width,height,false);
                    string? text=_reader.Decode(source)?.Text;
                    if(text is null)return;
                    bool valid=BluetoothMacCode.TryParse(text,out string mac);
                    RunOnUiThread(()=>{
                        if(!_resumed||_completed||session!=_session)return;
                        if(!valid){_message!.Text="二维码内容不是有效蓝牙MAC，请扫描设备标签。";return;}
                        _completed=true;StopCamera();
                        SetResult(global::Android.App.Result.Ok,new Intent().PutExtra(MacExtra,mac));Finish();
                    });
                } catch(Exception ex) {global::Android.Util.Log.Warn("BmsQrScan",ex.Message);}
                finally {Interlocked.Exchange(ref _decoding,0);}
            });
        } finally {
            // Requeue only to the same still-owned camera.
            if(_camera is not null&&camera.Equals(_camera))camera.AddCallbackBuffer(data);
        }
    }
    private void StopCamera()
    {
        _session++;
        Camera? camera=_camera;_camera=null;
        if(camera is null)return;
        try {camera.SetPreviewCallbackWithBuffer(null);camera.StopPreview();}
        catch(Java.Lang.RuntimeException){}
        finally {camera.Release();camera.Dispose();}
    }
}
#pragma warning restore CS0618, CA1422
