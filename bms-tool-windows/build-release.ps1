param(
    [string]$TargetFramework = "net7.0-windows10.0.19041.0",
    [string]$ReleaseTag = (Get-Date -Format "yyyyMMdd-HHmmss")
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$customerProject = Join-Path $projectRoot "BmsTool.Windows\BmsTool.Windows.csproj"
$internalProject = Join-Path $projectRoot "BmsFactoryTest.Windows\BmsFactoryTest.Windows.csproj"
$customerOutput = Join-Path $projectRoot "BmsTool.Windows\publish\customer-win-x64-$ReleaseTag"
$internalOutput = Join-Path $projectRoot "BmsFactoryTest.Windows\publish\internal-full-win-x64-$ReleaseTag"

function Invoke-Dotnet([string[]]$Arguments) {
    & dotnet @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "dotnet 命令失败，退出码：$LASTEXITCODE"
    }
}

function New-PublishDirectory([string]$Path) {
    if (Test-Path -LiteralPath $Path) {
        throw "发布目录已存在，为避免覆盖可能正在运行的 EXE，请使用新的 -ReleaseTag：$Path"
    }
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

Write-Host "发布客户版和内部完整测试版：$TargetFramework / win-x64 / $ReleaseTag"
New-PublishDirectory $customerOutput
New-PublishDirectory $internalOutput

Invoke-Dotnet @(
    "restore", $customerProject, "-r", "win-x64",
    "-p:TargetFrameworks=$TargetFramework", "-p:TargetFramework=$TargetFramework",
    "--force-evaluate"
)
Invoke-Dotnet @(
    "restore", $internalProject, "-r", "win-x64",
    "-p:TargetFrameworks=$TargetFramework", "-p:TargetFramework=$TargetFramework",
    "-p:LangVersion=preview", "--force-evaluate"
)

Invoke-Dotnet @(
    "publish", $customerProject, "-c", "Release", "-r", "win-x64", "--self-contained", "true",
    "-p:TargetFrameworks=$TargetFramework", "-p:TargetFramework=$TargetFramework",
    "-p:PublishSingleFile=true", "-p:IncludeNativeLibrariesForSelfExtract=true",
    "-p:EnableCompressionInSingleFile=true", "-p:DebugType=None", "--no-restore", "-o", $customerOutput
)
Invoke-Dotnet @(
    "publish", $internalProject, "-c", "Release", "-r", "win-x64", "--self-contained", "true",
    "-p:TargetFrameworks=$TargetFramework", "-p:TargetFramework=$TargetFramework",
    "-p:LangVersion=preview", "-p:PublishSingleFile=true", "-p:IncludeNativeLibrariesForSelfExtract=true",
    "-p:EnableCompressionInSingleFile=true", "-p:DebugType=None", "--no-restore", "-o", $internalOutput
)

$commit = (git -C $projectRoot rev-parse --short HEAD).Trim()
$customerExe = Join-Path $customerOutput "BmsTool.Windows.exe"
$internalExe = Join-Path $internalOutput "BmsFactoryTest.Windows.exe"
Write-Host ""
Write-Host "双版本发布完成："
Get-Item -LiteralPath $customerExe, $internalExe | ForEach-Object {
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
    Write-Host ("{0}`n  SHA256={1}`n  Commit={2}" -f $_.FullName, $hash, $commit)
}
