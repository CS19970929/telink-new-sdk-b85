namespace Bms.Ota.Core.Telink;

public sealed record OtaProgress(
    int CompletedPackets,
    int TotalPackets,
    int SentImageBytes,
    int TotalImageBytes,
    TimeSpan Elapsed,
    double BytesPerSecond,
    OtaTransferMode Mode)
{
    public double Percent => TotalImageBytes == 0 ? 0 : SentImageBytes * 100.0 / TotalImageBytes;
    public TimeSpan? EstimatedRemaining => BytesPerSecond <= 1 || SentImageBytes >= TotalImageBytes
        ? TimeSpan.Zero
        : TimeSpan.FromSeconds((TotalImageBytes - SentImageBytes) / BytesPerSecond);
}
