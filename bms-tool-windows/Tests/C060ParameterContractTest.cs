using BmsTool.Windows;

if (args.Length != 1) throw new ArgumentException("Pass the firmware tests/c060_short_current_vectors.csv file.");
int cases = 0;
foreach (string line in File.ReadLines(args[0]).Skip(1))
{
    ushort[] c = line.Split(',').Select(ushort.Parse).ToArray();
    ushort[] input = { 0, 0, c[2], c[3], 17, 23, 31, 41 };
    ushort[] saved = (ushort[])input.Clone();
    ushort[]? actual = null;
    try { actual = C060Parameters.NormalizeCurrentGroup(input, c[0], c[1]); }
    catch (ArgumentException) when (c[7] == 0) { }
    if (c[7] == 0 ? actual is not null : actual is null ||
        actual[0] != c[6] || actual[1] != c[6] || actual[2] != c[4] || actual[3] != c[5])
        throw new InvalidOperationException($"Case {cases}: {line}; actual={string.Join(',', actual ?? Array.Empty<ushort>())}");
    if (!input.SequenceEqual(saved) || actual is not null && !actual.Skip(4).SequenceEqual(saved.Skip(4)))
        throw new InvalidOperationException("Normalization modified the source or unrelated fields.");
    if (actual is not null && !C060Parameters.NormalizeCurrentGroup(actual,c[0],c[1]).SequenceEqual(actual))
        throw new InvalidOperationException("Effective values cannot round-trip.");
    ++cases;
}
foreach (string text in new[] { "NaN", "Infinity", "-Infinity", "1e999" })
{
    var row = ProtectionParameterCatalog.Create()[0];
    row.EditValue = text;
    if (row.TryParseEditedValue(out _)) throw new InvalidOperationException("Accepted non-finite input: " + text);
}
Console.WriteLine($"PASS: {cases} shared C/C# normalization cases, unchanged input/group fields and non-finite protection input rejection");
