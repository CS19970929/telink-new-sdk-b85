using System.Text.Json;

namespace BmsTool.Cli;

internal static class ExitCodes
{
    public const int Success = 0;
    public const int Unexpected = 1;
    public const int Usage = 2;
    public const int DeviceNotFound = 10;
    public const int MultipleDevices = 11;
    public const int ConnectFailed = 12;
    public const int FirmwareInvalid = 20;
    public const int ProductMismatch = 21;
    public const int FirmwareTooLarge = 22;
    public const int OtaFailed = 30;
    public const int OtaUnconfirmed = 33;
    public const int ReconnectFailed = 40;
    public const int VersionMismatch = 41;
    public const int Cancelled = 130;
}

internal sealed class CliException : Exception
{
    public int ExitCode { get; }
    public string Kind { get; }
    public object? Details { get; }

    public CliException(int exitCode, string kind, string message, object? details = null, Exception? inner = null)
        : base(message, inner)
    {
        ExitCode = exitCode;
        Kind = kind;
        Details = details;
    }
}

internal sealed class CliOptions
{
    private readonly Dictionary<string, string?> _values = new(StringComparer.OrdinalIgnoreCase);

    public string Command { get; private init; } = "help";
    public IReadOnlyList<string> Positionals { get; private init; } = Array.Empty<string>();
    public bool Json => Has("json");
    public bool Verbose => Has("verbose");

    public static CliOptions Parse(string[] args)
    {
        if (args.Length == 0)
            return new CliOptions { Command = "help" };

        string command = args[0].Trim().ToLowerInvariant();
        if (command.StartsWith('-'))
            command = "help";

        var result = new CliOptions { Command = command };
        var positional = new List<string>();

        for (int i = command == "help" ? 0 : 1; i < args.Length; i++)
        {
            string token = args[i];
            if (token == "-h")
            {
                result._values["help"] = null;
                continue;
            }
            if (token == "-y")
            {
                result._values["yes"] = null;
                continue;
            }
            if (!token.StartsWith("--", StringComparison.Ordinal))
            {
                positional.Add(token);
                continue;
            }

            string body = token[2..];
            int eq = body.IndexOf('=');
            if (eq >= 0)
            {
                result._values[body[..eq]] = body[(eq + 1)..];
                continue;
            }

            string key = body;
            string? value = null;
            if (i + 1 < args.Length && !args[i + 1].StartsWith("-", StringComparison.Ordinal))
                value = args[++i];
            result._values[key] = value;
        }

        result.Positionals = positional;
        return result;
    }

    public bool Has(string key) => _values.ContainsKey(key);

    public string? Get(string key) =>
        _values.TryGetValue(key, out string? value) ? value : null;

    public string RequireValue(string key)
    {
        string? value = Get(key);
        if (string.IsNullOrWhiteSpace(value))
            throw new CliException(ExitCodes.Usage, "usage", $"Missing value for --{key}.");
        return value;
    }

    public int GetInt(string key, int fallback, int min, int max)
    {
        string? text = Get(key);
        if (string.IsNullOrWhiteSpace(text))
            return fallback;
        if (!int.TryParse(text, out int value) || value < min || value > max)
            throw new CliException(ExitCodes.Usage, "usage", $"--{key} must be in {min}..{max}.");
        return value;
    }
}

internal sealed class CliReporter
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web)
    {
        WriteIndented = true
    };

    public bool Json { get; }
    public bool Verbose { get; }

    public CliReporter(bool json, bool verbose)
    {
        Json = json;
        Verbose = verbose;
    }

    public void Status(string message)
    {
        if (!Json || Verbose)
            Console.Error.WriteLine(message);
    }

    public void VerboseLog(string message)
    {
        if (Verbose)
            Console.Error.WriteLine(message);
    }

    public void Progress(string message)
    {
        if (!Json || Verbose)
            Console.Error.WriteLine(message);
    }

    public void Success(string command, object data)
    {
        if (Json)
        {
            Console.WriteLine(JsonSerializer.Serialize(new
            {
                schema = 1,
                ok = true,
                command,
                data
            }, JsonOptions));
        }
    }

    public static void WriteError(bool json, string kind, int code, string message, object? details = null)
    {
        if (json)
        {
            Console.WriteLine(JsonSerializer.Serialize(new
            {
                schema = 1,
                ok = false,
                error = new { code, kind, message, details }
            }, JsonOptions));
        }
        else
        {
            Console.Error.WriteLine($"ERROR [{code}:{kind}] {message}");
        }
    }
}
