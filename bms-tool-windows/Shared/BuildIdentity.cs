using System.Reflection;

namespace BmsTool.Windows;

public static class BuildIdentity
{
    public static string Display { get; } = Read();

    private static string Read()
    {
        Assembly assembly = Assembly.GetEntryAssembly() ?? typeof(BuildIdentity).Assembly;
        return assembly.GetCustomAttributes<AssemblyMetadataAttribute>()
            .FirstOrDefault(value => value.Key == "BmsGitCommit")?.Value ?? "unknown";
    }
}
