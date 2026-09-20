using BmsTool.Windows;

static class SocInputRecordingTest
{
    public static async Task RunAsync()
    {
        ushort[] w = new ushort[32];
        w[1]=w[31]=7; w[3]=6401; w[4]=65535; w[5]=65335; // -201 mA
        w[8]=3300;w[9]=3310;w[10]=10;w[11]=w[12]=650;
        w[14]=7;w[15]=60;w[16]=1;w[17]=1000;
        w[18]=200;w[19]=600;w[20]=5;w[24]=3650;w[25]=2500;
        string[] names=SocInputRecording.CsvHeader.Split(',');
        string[] values=SocInputRecording.ToCsv(w,7,0).Split(',');
        if(names.Length!=values.Length)throw new Exception("input CSV field alignment");
        var row=names.Zip(values).ToDictionary(x=>x.First,x=>x.Second);
        if(row["current_ma"]!="-201"||row["true_soc"]!=""||row["charger_known"]!="0"||row["event"]!="2")
            throw new Exception("signed current / unknown truth / source state");
        var transport=new FakeTransport { SocInputWords=w };
        await using var client=new BmsClient(transport);
        if(await SocInputRecording.ReadSequenceAsync(client,default)!=7)throw new Exception("recorder header");
        var captured=await SocInputRecording.ReadSampleAsync(client,7,default);
        if(!captured.SequenceEqual(w))throw new Exception("fragmented recorder read");
        string? export=Environment.GetEnvironmentVariable("SOC_INPUT_TEST_CSV");
        if(!string.IsNullOrEmpty(export)) {
            using var writer=new System.IO.StreamWriter(export);
            writer.WriteLine(SocInputRecording.CsvHeader);
            writer.WriteLine(SocInputRecording.ToCsv(w,7,0));
            w[1]=w[31]=8;w[3]=12801;
            writer.WriteLine(SocInputRecording.ToCsv(w,8,1));
            w[1]=w[31]=7;
        }
        w[31]=8;
        try { SocInputRecording.ToCsv(w,7,1);throw new Exception("torn record accepted"); }
        catch(System.IO.InvalidDataException) { }
        w[31]=7;w[16]=3;
        try { SocInputRecording.Validate(w,7);throw new Exception("invalid chemistry accepted"); }
        catch(System.IO.InvalidDataException) { }
        Console.WriteLine("PASS SOC input CSV: units, missing truth, flags, overwrite and chemistry guards");
    }
}
