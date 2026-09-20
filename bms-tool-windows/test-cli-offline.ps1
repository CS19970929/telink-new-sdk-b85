param([string]$CliDll)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = $PSScriptRoot
$project = Join-Path $root "BmsTool.Cli\BmsTool.Cli.csproj"
$tempBase = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA "CodexTemp\bms-tool-windows"))
$testRoot = [IO.Path]::GetFullPath((Join-Path $tempBase ("cli-offline-" + [Guid]::NewGuid().ToString("N"))))
if (-not $testRoot.StartsWith($tempBase + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing unsafe test directory: $testRoot"
}
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null

function New-DiagnosticBundle([string]$Path, [string]$BuildId, [int]$Soc) {
    $source = $Path + ".source"
    New-Item -ItemType Directory -Path $source | Out-Null
    [IO.File]::WriteAllText((Join-Path $source "manifest.json"), ("{`"firmware_git_commit`":`"" + $BuildId + "`"}"), [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $source "soc.json"), ("[{`"field`":`"SOC estimate`",`"value`":" + $Soc + "}]"), [Text.UTF8Encoding]::new($false))
    foreach ($name in @("boot.json", "storage.json", "current.json", "power.json", "protection_runtime.json", "parameters.json", "afe.json", "health.json")) {
        [IO.File]::WriteAllText((Join-Path $source $name), "{}", [Text.UTF8Encoding]::new($false))
    }
    [System.IO.Compression.ZipFile]::CreateFromDirectory($source, $Path)
}

function Invoke-CliJson([string[]]$Arguments) {
    if ($CliDll) { $text = & dotnet $CliDll @Arguments }
    else { $text = & dotnet run --project $project -c Release --no-build -- @Arguments }
    if ($LASTEXITCODE -ne 0) { throw "CLI failed with exit code ${LASTEXITCODE}: $Arguments" }
    return ($text | Out-String | ConvertFrom-Json)
}

try {
    $capabilities = Invoke-CliJson @("capabilities", "--json")
    if (-not $capabilities.ok -or $capabilities.data.products.Count -ne 4) {
        throw "Capabilities JSON contract failed"
    }
    if ($capabilities.data.source -ne 'client_support_catalog' -or $capabilities.data.deviceProbed) {
        throw "Static catalog must not masquerade as a live device probe"
    }
    if ($CliDll) {
        $socInvalid = & dotnet $CliDll record soc --inputs --auto --output (Join-Path $testRoot 'soc.csv') --json
        if ($LASTEXITCODE -ne 2 -or ($socInvalid | Out-String | ConvertFrom-Json).ok) {
            throw 'SOC input capture must reject unpinned targets with JSON ok=false'
        }
        $invalid = & dotnet $CliDll capture --auto --output $testRoot --json
        if ($LASTEXITCODE -ne 2 -or ($invalid | Out-String | ConvertFrom-Json).error.kind -ne 'usage') {
            throw 'Capture must reject an unpinned target before device access'
        }
    }

    $before = Join-Path $testRoot "before.zip"
    $after = Join-Path $testRoot "after.zip"
    $report = Join-Path $testRoot "identity-report.md"
    New-DiagnosticBundle $before "11111111" 50
    New-DiagnosticBundle $after "22222222" 51

    $identity = Invoke-CliJson @("compare", $before, $after, "--scope", "identity", "--output", $report, "--json")
    if ($identity.data.differenceCount -ne 1 -or
        $identity.data.differences[0].category -ne "identity" -or
        $identity.data.scope -ne "identity" -or
        -not (Test-Path -LiteralPath $report)) {
        throw "Identity comparison/Markdown contract failed"
    }

    $runtime = Invoke-CliJson @("compare", $before, $after, "--scope", "runtime", "--json")
    if ($runtime.data.differenceCount -ne 1 -or
        $runtime.data.differences[0].category -ne "runtime") {
        throw "Runtime comparison scope contract failed"
    }

    Write-Host "PASS CLI offline: capabilities JSON, categorized compare and Markdown report"
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}
