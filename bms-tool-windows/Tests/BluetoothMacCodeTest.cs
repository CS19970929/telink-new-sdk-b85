using BmsTool.Android;
using ZXing;
using ZXing.Common;
using ZXing.QrCode;

static class Test
{
    static int checks;
    static void Check(bool ok,string why){if(!ok)throw new Exception(why);checks++;}
    public static void Main()
    {
        foreach(string text in new[]{"EC:F6:4C:E9:F1:3D","ec:f6:4c:e9:f1:3d","EC-F6-4C-E9-F1-3D","ecf64ce9f13d","  EC:F6:4C:E9:F1:3D\r\n"})
            Check(BluetoothMacCode.TryParse(text,out string value)&&value=="EC:F6:4C:E9:F1:3D","valid payload normalization");
        foreach(string? text in new[]{null,""," ","BT_1234567890","00:00:00:00:00:00","FF:FF:FF:FF:FF:FF","EC:F6:4C:E9:F1","EC:F6:4C:E9:F1:3D:AA","EC:F6-4C:E9:F1:3D","GG:F6:4C:E9:F1:3D","https://host/EC:F6:4C:E9:F1:3D","{\"mac\":\"EC:F6:4C:E9:F1:3D\"}","EC F6 4C E9 F1 3D","EC:F6:4C:E9:F1:3D\ncommand",new string(' ',65)+"ECF64CE9F13D"})
            Check(!BluetoothMacCode.TryParse(text,out string bad)&&bad==string.Empty,"invalid payload cannot produce a target");
        foreach(string text in new[]{"ec:f6:4c:e9:f1:3d","EC-F6-4C-E9-F1-3D","ECF64CE9F13D","wrong label"}) {
            var matrix=new QRCodeWriter().encode(text,BarcodeFormat.QR_CODE,320,320);
            byte[] pixels=new byte[320*320];
            for(int y=0;y<320;y++)for(int x=0;x<320;x++)pixels[y*320+x]=matrix[x,y]?(byte)0:(byte)255;
            for(int rotation=0;rotation<4;rotation++) {
                var reader=new BarcodeReaderGeneric {AutoRotate=true,Options=new DecodingOptions {TryHarder=true,PossibleFormats=new[]{BarcodeFormat.QR_CODE}}};
                var decoded=reader.Decode(new PlanarYUVLuminanceSource(pixels,320,320,0,0,320,320,false));
                Check(decoded?.Text==text,"QR luminance decode at each rotation");
                bool valid=BluetoothMacCode.TryParse(decoded!.Text,out string mac);
                Check(text=="wrong label"?!valid:valid&&mac=="EC:F6:4C:E9:F1:3D","decoded QR retains strict target policy");
                byte[] rotated=new byte[pixels.Length];for(int y=0;y<320;y++)for(int x=0;x<320;x++)rotated[x*320+319-y]=pixels[y*320+x];pixels=rotated;
            }
        }
        Console.WriteLine($"PASS MAC QR parsing/luminance: {checks} checks; no camera or Bluetooth device access");
    }
}
