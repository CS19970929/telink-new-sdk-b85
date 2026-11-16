param(
    [string]$PythonVersion="3.9",
    [ValidateSet("All","CustomerZh","CustomerEn","InternalZh","InternalEn")][string]$Variant="All",
    [string]$ReleaseRoot=(Join-Path $env:USERPROFILE "Documents\CodexOutputs\comm-tool"),
    [string]$ReleaseTag=(Get-Date -Format "yyyyMMdd-HHmmss")
)
$ErrorActionPreference="Stop"
$variants=@(
 @{Key="CustomerZh";Name="BMS_CommTool_Upgrade_UI"},
 @{Key="CustomerEn";Name="BMS_CommTool_Upgrade_UI_EN"},
 @{Key="InternalZh";Name="BMS_CommTool_Upgrade_UI_Internal"},
 @{Key="InternalEn";Name="BMS_CommTool_Upgrade_UI_Internal_EN"}
)
$output=Join-Path $ReleaseRoot $ReleaseTag
if(Test-Path -LiteralPath $output){throw "发布目录已存在，请使用新的ReleaseTag"}
$temp=Join-Path $env:LOCALAPPDATA ("CodexTemp\comm-tool\publish\"+$ReleaseTag)
New-Item -ItemType Directory -Path $temp -Force | Out-Null
foreach($v in $variants){
 if($Variant -ne "All" -and $v.Key -ne $Variant){continue}
 & py "-$PythonVersion" -m PyInstaller --noconfirm --clean --onefile --windowed --name $v.Name --specpath $temp --workpath (Join-Path $temp $v.Key) --distpath (Join-Path $output $v.Key) --paths $PSScriptRoot (Join-Path $PSScriptRoot "comm_tool_upgrade_ui.py")
 if($LASTEXITCODE -ne 0){throw "PyInstaller失败：$($v.Key)"}
 Get-FileHash -LiteralPath (Join-Path $output ($v.Key+"\"+$v.Name+".exe")) -Algorithm SHA256
}
