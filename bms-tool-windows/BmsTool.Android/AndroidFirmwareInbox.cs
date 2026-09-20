using Android.App;
using Android.Content;
using Android.Util;
using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace BmsTool.Android;

internal static class AndroidFirmwareInbox
{
    public const string ImportAction = "com.cs.bmstool.android.IMPORT_FIRMWARE";
    public const string AutomationAuthorizationPreferences = "firmware_automation_authorization";
    public const int MaxImageBytes = 2 * 1024 * 1024;

    public static string GetDirectory(Context context)
    {
        string directory = Path.Combine(context.FilesDir!.AbsolutePath, "FirmwareInbox");
        Directory.CreateDirectory(directory);
        return directory;
    }

    public static bool IsValidFileName(string fileName) =>
        Regex.IsMatch(fileName, "^[A-Za-z0-9._-]{1,96}\\.bin$",
            RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
}

[BroadcastReceiver(Enabled = true, Exported = true, Permission = global::Android.Manifest.Permission.Dump)]
[IntentFilter(new[] { AndroidFirmwareInbox.ImportAction })]
internal sealed class FirmwareImportReceiver : BroadcastReceiver
{
    private const string LogTag = "BmsTool.Android";

    public override void OnReceive(Context? context, Intent? intent)
    {
        if (context is null || intent?.Action != AndroidFirmwareInbox.ImportAction) return;
        string? temporary = null;
        try
        {
            string uploadId = intent.GetStringExtra("upload_id") ?? string.Empty;
            string fileName = intent.GetStringExtra("file_name") ?? string.Empty;
            string expectedSha256 = intent.GetStringExtra("sha256") ?? string.Empty;
            string encoded = intent.GetStringExtra("data") ?? string.Empty;
            int index = intent.GetIntExtra("index", -1);
            int total = intent.GetIntExtra("total", -1);
            int offset = intent.GetIntExtra("offset", -1);

            if (!Regex.IsMatch(uploadId, "^[0-9a-f]{16}$", RegexOptions.CultureInvariant) ||
                !AndroidFirmwareInbox.IsValidFileName(fileName) ||
                !Regex.IsMatch(expectedSha256, "^[0-9A-Fa-f]{64}$", RegexOptions.CultureInvariant) ||
                index < 0 || total is < 1 or > 512 || index >= total || offset < 0 || encoded.Length > 12000)
                throw new InvalidDataException("Firmware import metadata is invalid.");

            byte[] chunk = Convert.FromBase64String(encoded);
            string directory = AndroidFirmwareInbox.GetDirectory(context);
            temporary = Path.Combine(directory, $".incoming-{uploadId}.part");
            string target = Path.Combine(directory, fileName);
            using (var stream = new FileStream(temporary, index == 0 ? FileMode.Create : FileMode.Open,
                       FileAccess.Write, FileShare.None))
            {
                if (stream.Length != offset)
                    throw new InvalidDataException($"Firmware import offset mismatch: expected={stream.Length}, received={offset}.");
                stream.Position = stream.Length;
                stream.Write(chunk);
                if (stream.Length > AndroidFirmwareInbox.MaxImageBytes)
                    throw new InvalidDataException("Firmware import exceeds the maximum supported size.");
            }

            if (index != total - 1) return;
            byte[] bytes = File.ReadAllBytes(temporary);
            string actualSha256 = Convert.ToHexString(SHA256.HashData(bytes));
            if (!actualSha256.Equals(expectedSha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException($"Firmware import SHA-256 mismatch: expected={expectedSha256}, actual={actualSha256}.");
            File.Move(temporary, target, true);
            ISharedPreferencesEditor? authorization = context
                .GetSharedPreferences(AndroidFirmwareInbox.AutomationAuthorizationPreferences, FileCreationMode.Private)
                ?.Edit();
            authorization?.PutString("upload_id", uploadId);
            authorization?.PutString("file_name", fileName);
            authorization?.PutString("sha256", actualSha256);
            authorization?.PutLong("expires_utc_ms", DateTimeOffset.UtcNow.AddMinutes(5).ToUnixTimeMilliseconds());
            authorization?.Apply();
            Log.Info(LogTag, $"FIRMWARE_IMPORT_OK upload={uploadId} path={target} bytes={bytes.Length} sha256={actualSha256}");
        }
        catch (Exception ex)
        {
            if (temporary is not null)
            {
                try { File.Delete(temporary); }
                catch { }
            }
            Log.Error(LogTag, $"FIRMWARE_IMPORT_FAIL type={ex.GetType().Name} message={ex.Message}");
        }
    }
}
