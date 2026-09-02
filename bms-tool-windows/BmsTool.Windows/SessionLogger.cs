using System.Runtime.InteropServices;
using System.Text;

namespace BmsTool.Windows;

public sealed class SessionLogger : IDisposable
{
    private readonly object _gate = new();
    private readonly StreamWriter _writer;

    public string FilePath { get; }

    public SessionLogger()
    {
        string root = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "BmsAssistant",
            "Logs");
        Directory.CreateDirectory(root);
        FilePath = Path.Combine(root, $"BmsAssistant_{DateTime.Now:yyyyMMdd_HHmmss_fff}.log");
        _writer = new StreamWriter(new FileStream(FilePath, FileMode.Create, FileAccess.Write, FileShare.ReadWrite), new UTF8Encoding(false))
        {
            AutoFlush = true
        };

        Write("SESSION", "================ BMS Assistant session start ================");
        Write("SESSION", $"LocalTime={DateTime.Now:O}");
        Write("SESSION", $"UTC={DateTime.UtcNow:O}");
        Write("SESSION", $"OS={Environment.OSVersion}");
        Write("SESSION", $"Framework={RuntimeInformation.FrameworkDescription}");
        Write("SESSION", $"ProcessArch={RuntimeInformation.ProcessArchitecture}; OSArch={RuntimeInformation.OSArchitecture}; Is64Bit={Environment.Is64BitProcess}");
        Write("SESSION", $"Machine={Environment.MachineName}; UserInteractive={Environment.UserInteractive}");
        Write("SESSION", $"LogFile={FilePath}");
    }

    public void Write(string category, string message)
    {
        string line = $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} [{Environment.CurrentManagedThreadId,2}] [{category}] {message}";
        lock (_gate)
        {
            _writer.WriteLine(line);
        }
    }

    public void WriteException(string category, string context, Exception ex)
    {
        Write(category, $"EXCEPTION context={context}; type={ex.GetType().FullName}; hresult=0x{ex.HResult:X8}; message={ex.Message}");
        lock (_gate)
        {
            _writer.WriteLine(ex.ToString());
        }
    }

    public void Dispose()
    {
        lock (_gate)
        {
            _writer.WriteLine($"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} [SESSION] ================ session end ================");
            _writer.Dispose();
        }
    }
}
