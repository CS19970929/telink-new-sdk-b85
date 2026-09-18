using System.Text;

namespace BmsTool.Cli;

internal static class Program
{
    public static async Task<int> Main(string[] args)
    {
        Console.OutputEncoding = Encoding.UTF8;
        using var cts = new CancellationTokenSource();
        Console.CancelKeyPress += (_, e) =>
        {
            e.Cancel = true;
            cts.Cancel();
        };

        CliOptions? options = null;
        try
        {
            options = CliOptions.Parse(args);
            if (options.Command is "help" or "--help" or "-h" || options.Has("help"))
            {
                Console.WriteLine(CliCommands.HelpText);
                return ExitCodes.Success;
            }

            var reporter = new CliReporter(options.Json, options.Verbose);
            return await CliCommands.ExecuteAsync(options, reporter, cts.Token);
        }
        catch (OperationCanceledException)
        {
            CliReporter.WriteError(options?.Json ?? args.Contains("--json"), "cancelled", ExitCodes.Cancelled, "Operation cancelled.");
            return ExitCodes.Cancelled;
        }
        catch (CliException ex)
        {
            CliReporter.WriteError(options?.Json ?? args.Contains("--json"), ex.Kind, ex.ExitCode, ex.Message, ex.Details);
            return ex.ExitCode;
        }
        catch (Exception ex)
        {
            if (options?.Verbose == true)
                Console.Error.WriteLine(ex);
            CliReporter.WriteError(options?.Json ?? args.Contains("--json"), "unexpected_error", ExitCodes.Unexpected, ex.Message);
            return ExitCodes.Unexpected;
        }
    }
}
