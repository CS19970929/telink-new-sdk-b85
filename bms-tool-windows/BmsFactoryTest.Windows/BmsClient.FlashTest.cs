using System.Buffers.Binary;
namespace BmsTool.Windows;
public sealed partial class BmsClient
{
    public async Task RestoreFlashTestSnapshotAsync(ushort[] words)
    {
        if (words.Length != 20) throw new ArgumentException("SOC快照必须为20字。");
        byte[] raw=new byte[words.Length*2];
        for(int i=0;i<words.Length;i++) BinaryPrimitives.WriteUInt16BigEndian(raw.AsSpan(i*2,2),words[i]);
        ModbusRtu.ValidateWriteMultipleAck(await TransactAsync(ModbusRtu.WriteMultiple(0x2720,raw),default),0x2720,20);
    }
}
