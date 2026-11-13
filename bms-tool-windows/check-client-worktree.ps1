param()
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$branch = & git -C $root symbolic-ref --quiet --short HEAD
if ($LASTEXITCODE -ne 0 -or $branch -ne 'client/main') {
    throw ('客户端开发必须使用现有 client/main 工作树；当前为 ' + $branch + '。请定位负责工作树，不要自动新建分支。')
}
$head = & git -C $root rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot read client HEAD.' }
Write-Output ('源码目录：' + (Join-Path $root 'bms-tool-windows'))
Write-Output ('负责分支：' + $branch)
Write-Output ('HEAD：' + $head)
$dirty = & git -C $root status --short
if ($LASTEXITCODE -ne 0) { throw 'Cannot read worktree status.' }
if ($dirty) { Write-Output '工作区已有改动，继续开发时保留无关内容：'; $dirty }
else { Write-Output '工作区：clean' }
