using System.IO;
using BmsTool.Windows;

static class SocProfilePolicyTest
{
    static void Check(bool ok){if(!ok)throw new Exception("SOC模型断言失败");}
    static void Reject(Action action){try{action();}catch(ArgumentException){return;}throw new Exception("应当拒绝无效模型");}
    static async Task Main(){
        for(ushort chemistry=1;chemistry<=2;chemistry++){
            var bundle=BmsClient.CreateSocTrialBundle(chemistry);
            var restored=BmsClient.ImportSocBundle(BmsClient.ExportSocBundle(bundle));
            Check(restored.Profile.ProfileVersion==3&&restored.Profile.RestPrepareSeconds==3600);
            Check(restored.Profile.OcvPoints.SequenceEqual(bundle.Profile.OcvPoints));
            Check(restored.Profile.OcvPoints.Count==(chemistry==1?14:13));
            Check(restored.EngineeringCandidate&&restored.Source.Length>0);
        }
        Reject(()=>BmsClient.ImportSocBundle("{}"));
        Reject(()=>BmsClient.ImportSocBundle(new string(' ',1024*1024+1)));
        var invalid=BmsClient.CreateSocTrialBundle(1);invalid.Profile.RestPrepareSeconds=600;
        Reject(()=>BmsClient.ExportSocBundle(invalid));
        invalid=BmsClient.CreateSocTrialBundle(1);invalid.Profile.OcvPoints[1]=new(2844,5);
        Reject(()=>BmsClient.ExportSocBundle(invalid));
        var client=new BmsClient();
        var current=await client.ReadSocProfileAsync();Check(current.SupportsPolicyV3&&current.ProfileVersion==3);
        var candidate=BmsClient.CreateSocTrialBundle(1).Profile;candidate.FullSyncMv=3510;
        var readback=await client.SaveSocProfileAsync(candidate,BmsServiceAccess.DefaultPassword);
        Check(readback.FullSyncMv==3510&&readback.Persisted&&client.Writes==15);
        client.Capabilities=0x0200;int writes=client.Writes;
        try{await client.SaveSocProfileAsync(candidate,BmsServiceAccess.DefaultPassword);throw new Exception("旧固件收到写入");}catch(NotSupportedException){}
        Check(client.Writes==writes);
        client.Capabilities=0x0a00;
        try{await client.SaveSocProfileAsync(BmsClient.CreateSocTrialBundle(2).Profile,BmsServiceAccess.DefaultPassword);throw new Exception("类型不符收到写入");}catch(ArgumentException){}
        Check(client.Writes==writes);
        candidate.ProfileVersion=2;
        try{await client.SaveSocProfileAsync(candidate,BmsServiceAccess.DefaultPassword);throw new Exception("旧模型收到v3写入");}catch(NotSupportedException){}
        Check(client.Writes==writes);
        using var cancelled=new CancellationTokenSource();cancelled.Cancel();
        try{await client.ReadSocProfileAsync(cancelled.Token);throw new Exception("取消未生效");}catch(OperationCanceledException){}
        Console.WriteLine("PASS SOC模型v3：候选曲线、JSON往返、范围/版本/类型门禁和原事务读回；无真实设备或WPF证据");
    }
}

namespace BmsTool.Windows
{
    static class D008Parameters { public const ushort Magic=0xd008; }
    static class BmsServiceAccess {
        public const string DefaultPassword="hs456";
        public static bool PasswordValid(string password)=>password==DefaultPassword;
    }
    public sealed partial class BmsClient
    {
        public ushort Capabilities=0x0a00;
        public int Writes;
        private ushort[] words=EncodeSocProfile(CreateSocTrialBundle(1).Profile);
        private ushort[] stage=new ushort[50];
        private bool persisted=true;
        private Task<ushort[]> ReadRegistersAsync(ushort address,ushort count,CancellationToken ct){
            ct.ThrowIfCancellationRequested();ushort[] result;
            if(address==0x2e00)result=new ushort[]{0xd008,2,Capabilities};
            else if(address==0x2eb0)result=new ushort[]{0x534f,1};
            else if(address==0x2ee4)result=new ushort[]{(ushort)(persisted?1:0),1,2};
            else if(address==0x2ee5)result=new ushort[]{1};
            else if(address>=0x2eb2&&address+count<=0x2ee4)result=words.Skip(address-0x2eb2).Take(count).ToArray();
            else throw new IOException("意外读取地址");
            CheckCount(result,count);return Task.FromResult(result);
        }
        private static void CheckCount(ushort[] result,ushort count){if(result.Length!=count)throw new IOException("长度错误");}
        private Task WriteRegistersAsync(ushort address,ushort[] values,CancellationToken ct){
            ct.ThrowIfCancellationRequested();Writes++;
            if(address==0x2ef8){
                if(values[0]==1)stage=(ushort[])words.Clone();
                else if(values[0]==2){words=(ushort[])stage.Clone();persisted=true;}
                else throw new IOException("意外命令");
            }else if(address>=0x2f20&&address+values.Length<=0x2f52)values.CopyTo(stage,address-0x2f20);
            else throw new IOException("意外写入地址");
            return Task.CompletedTask;
        }
    }
}
