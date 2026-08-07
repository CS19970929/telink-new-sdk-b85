namespace TelinkOta.Core.Ota;

/// <summary>日志级别。</summary>
public enum LogLevel
{
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
}

/// <summary>会话日志回调。</summary>
public delegate void LogCallback(LogLevel level, string message);

/// <summary>十六进制工具。</summary>
public static class Hex
{
    public static string Dump(byte[] data) =>
        data is null || data.Length == 0 ? "(empty)" : Convert.ToHexString(data);

    public static string Dump(byte[] data, string separator = " ") =>
        data is null || data.Length == 0
            ? "(empty)"
            : string.Join(separator, data.Select(b => b.ToString("X2")));
}
