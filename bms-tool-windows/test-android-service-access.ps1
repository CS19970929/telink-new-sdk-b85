$ErrorActionPreference='Stop'
$testRoot=Join-Path $env:TEMP ('CodexTemp/bms-tool-windows/android-service-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Tests/AndroidServiceAccessTest.cs') -Destination (Join-Path $testRoot 'Program.cs')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'BmsTool.Android/BluetoothMacCode.cs') -Destination $testRoot
# Compile the actual async permission and navigation methods with inert UI state.
$methods=@('using BmsTool.Android; using BmsTool.Windows; partial class ServiceState {')
foreach($entry in @(@{File='MainActivity.Features.cs';Names=@('EnterServiceModeAsync','EnsureServiceAccess')},@{File='MainActivity.Ui.cs';Names=@('ShowPage')})) {
    $source=Get-Content -LiteralPath (Join-Path $PSScriptRoot ('BmsTool.Android/'+$entry.File)) -Raw
    foreach($name in $entry.Names) {
        $match=[regex]::Match($source,'    private (?:async )?(?:Task|bool|void) '+$name+'\(')
        if(!$match.Success){throw "Production method missing: $name"}
        $start=$match.Index;$open=$source.IndexOf('{',$start);$depth=1;$end=$open+1
        while($depth -gt 0){if($source[$end] -eq '{'){$depth++};if($source[$end] -eq '}'){$depth--};$end++}
        $methods+=$source.Substring($start,$end-$start)
    }
}
$methods+='}'
($methods -join "`n") | Set-Content -LiteralPath (Join-Path $testRoot 'Production.cs') -Encoding utf8
$core=[Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'BmsTool.Core/BmsTool.Core.csproj'))
@"
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><ProjectReference Include="$core"/><PackageReference Include="ZXing.Net" Version="0.16.9"/></ItemGroup></Project>
"@ | Set-Content -LiteralPath (Join-Path $testRoot 'Test.csproj') -Encoding utf8
dotnet run --project (Join-Path $testRoot 'Test.csproj') -c Release --artifacts-path (Join-Path $testRoot 'artifacts') -p:NuGetAudit=false
if($LASTEXITCODE -ne 0){throw 'Android service access regression failed'}
