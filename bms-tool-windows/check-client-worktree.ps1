param([string]$ExpectedBranch = 'client/main')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$top = & git -C $root rev-parse --show-toplevel
if ($LASTEXITCODE -ne 0 -or [IO.Path]::GetFullPath($top) -ne $root) {
    throw '客户端脚本必须位于仓库根目录的 bms-tool-windows 下。'
}
$worktrees = & git -C $root -c core.quotePath=false worktree list --porcelain
if ($LASTEXITCODE -ne 0 -or !$worktrees -or !$worktrees[0].StartsWith('worktree ')) {
    throw '无法读取仓库主工作目录。'
}
$mainRoot = [IO.Path]::GetFullPath($worktrees[0].Substring('worktree '.Length))
if ($mainRoot -ne $root) {
    throw ('客户端开发使用仓库主工作目录：' + $mainRoot + '；不再使用额外的客户端 worktree。')
}
$branch = & git -C $root symbolic-ref --quiet --short HEAD
if ($LASTEXITCODE -ne 0) { throw '客户端开发不使用 detached HEAD，请先核对用户指定的现有分支。' }
if ([string]::IsNullOrWhiteSpace($ExpectedBranch) -or $branch -cne $ExpectedBranch) {
    throw ('客户端当前分支为 ' + $branch + '，预期为 ' + $ExpectedBranch + '。默认使用 client/main；用户明确指定功能分支时通过 -ExpectedBranch 传入，不自动创建或切换分支。')
}
& git -C $root cat-file -e 'HEAD:bms-tool-windows/BmsTool.Windows/BmsTool.Windows.csproj'
if ($LASTEXITCODE -ne 0) {
    throw '当前 HEAD 没有客户端工程；未跟踪的旧副本和发布包不能作为开发入口。'
}
$head = & git -C $root rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw '无法读取客户端 HEAD。' }
Write-Output ('仓库主目录：' + $root)
Write-Output ('源码目录：' + (Join-Path $root 'bms-tool-windows'))
Write-Output ('负责分支：' + $branch)
Write-Output '长期客户端主线：client/main'
Write-Output ('HEAD：' + $head)
$dirty = & git -C $root status --short
if ($LASTEXITCODE -ne 0) { throw '无法读取工作区状态。' }
if ($dirty) { Write-Output '工作区已有改动，继续开发时保留无关内容：'; $dirty }
else { Write-Output '工作区：clean' }
