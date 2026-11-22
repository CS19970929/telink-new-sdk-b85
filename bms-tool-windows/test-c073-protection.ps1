$ErrorActionPreference='Stop'
$testRoot=Join-Path $env:TEMP ('CodexTemp/c073-protection-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$src=[Security.SecurityElement]::Escape($PSScriptRoot)
@"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup>
  <ItemGroup>
    <ProjectReference Include="$src/BmsTool.Core/BmsTool.Core.csproj"/>
    <PackageReference Include="System.IO.Ports" Version="8.0.0"/>
    <Compile Include="$src/Tests/C073ProtectionTest.cs"/>
    <Compile Include="$src/Shared/CommToolCanTransport.cs"/>
    <Compile Include="$src/Shared/CommToolOwcProtocol.cs"/>
    <Compile Include="$src/Shared/CommToolOwcClient.cs"/>
  </ItemGroup>
</Project>
"@ | Set-Content -LiteralPath (Join-Path $testRoot 'Test.csproj') -Encoding utf8
dotnet run --project (Join-Path $testRoot 'Test.csproj') -c Release --artifacts-path (Join-Path $testRoot 'artifacts') -p:NuGetAudit=false
exit $LASTEXITCODE
