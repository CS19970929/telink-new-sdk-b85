param([string]$ReleaseTag = (Get-Date -Format "yyyyMMdd-HHmmss"))

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$project = Join-Path $projectRoot "BmsTool.Android.Deployer\BmsTool.Android.Deployer.csproj"
$output = Join-Path $projectRoot "BmsTool.Android.Deployer\publish\android-deployer-win-x64-$ReleaseTag"
$buildTemp = Join-Path $env:LOCALAPPDATA "CodexTemp\bms-tool-android-deployer\$ReleaseTag"

if (Test-Path -LiteralPath $output) { throw "发布目录已存在：$output" }
New-Item -ItemType Directory -Path $output -Force | Out-Null

& dotnet restore $project -r win-x64 `
    -p:BaseIntermediateOutputPath="$buildTemp\obj\" `
    -p:MSBuildProjectExtensionsPath="$buildTemp\obj\" `
    -p:BaseOutputPath="$buildTemp\bin\" --force-evaluate
if ($LASTEXITCODE -ne 0) { throw "Android 固件发送器 restore 失败。" }

& dotnet publish $project -c Release -r win-x64 --self-contained true `
    -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:EnableCompressionInSingleFile=true -p:DebugType=None `
    -p:BaseIntermediateOutputPath="$buildTemp\obj\" `
    -p:MSBuildProjectExtensionsPath="$buildTemp\obj\" `
    -p:BaseOutputPath="$buildTemp\bin\" --no-restore -o $output
if ($LASTEXITCODE -ne 0) { throw "Android 固件发送器 publish 失败。" }

$exe = Join-Path $output "BmsTool.Android.Deployer.exe"
$hash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
Write-Host "Android 固件发送器发布完成："
Write-Host $exe
Write-Host "SHA256=$hash"
