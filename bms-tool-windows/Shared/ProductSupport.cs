namespace BmsTool.Windows;

public sealed record ProductSupportProfile(
    string Product,
    string Afe,
    string AfeModel,
    int DiagnosticsSchema,
    int RuntimeVersion,
    bool AfeHardwareV2,
    int? D008ParametersVersion,
    string SoftwareEvidence,
    string HardwareEvidence);

public static class ProductSupportMatrix
{
    public const int Schema = 1;

    public static IReadOnlyList<ProductSupportProfile> Products { get; } =
        new ProductSupportProfile[]
        {
            new(
                "D008", "DVC1124", "0x1124", 1, 3, true, 2,
                "firmware build, host contracts, unified diagnostics and client protocol",
                "D008 board link exists; protection timing and physical MOS/Gate still require the hardware checklist"),
            new(
                "D011", "SH3673510", "0x3510", 1, 3, true, null,
                "firmware build, host contracts and unified diagnostics",
                "not validated on a D011 board in the current cycle"),
            new(
                "D013", "SH3673510", "0x3510", 1, 3, true, null,
                "firmware build and unified diagnostics protocol",
                "D013 schematic/BOM and board validation are unavailable; inherited D011 pin names are not hardware proof"),
            new(
                "D014", "SH3673510", "0x3510", 1, 3, true, null,
                "firmware build, host contracts and unified diagnostics",
                "not validated on a D014 board in the current cycle"),
        };
}
