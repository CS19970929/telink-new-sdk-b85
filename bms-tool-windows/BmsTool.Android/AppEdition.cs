namespace BmsTool.Android;

internal static class AppEdition
{
#if BMS_INTERNAL_APP
    public static bool IsInternal { get; } = true;
    public const string DisplayName = "BMS Tool 测试版";
#else
    public static bool IsInternal { get; } = false;
    public const string DisplayName = "BMS Battery";
#endif
}
