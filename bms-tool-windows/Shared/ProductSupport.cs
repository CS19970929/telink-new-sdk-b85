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
            new("C060", "BQ76930 ×2", "20S split-port", 0, 0, false, null,
                "C060 legacy identity/realtime auto-detection; software protocol validation",
                "physical board acceptance remains required"),
            new("D015", "SH3673520", "0x3520", 0, 2, true, null,
                "D015 C firmware interop: realtime, protection, MTU23 commit and current calibration",
                "no D015 physical board validation in this cycle"),
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
