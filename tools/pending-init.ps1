param(
    [string]$Root = (Get-Location).Path
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path $Root).Path
$hookDir = Join-Path $repoRoot '.githooks'
$hookPath = Join-Path $hookDir 'pre-push'
$todoPath = Join-Path $repoRoot 'TODO.md'
$testPendingPath = Join-Path $repoRoot 'TEST_PENDING.md'

New-Item -ItemType Directory -Force -Path $hookDir | Out-Null

$hookContent = @'
#!/bin/sh

REPO_ROOT=$(git rev-parse --show-toplevel 2>/dev/null)
[ -n "$REPO_ROOT" ] || exit 0

PENDING_FILES="$REPO_ROOT/TODO.md $REPO_ROOT/TEST_PENDING.md"
FOUND=0

for PENDING_FILE in $PENDING_FILES; do
    [ -f "$PENDING_FILE" ] || continue

    if grep -q '^- \[ \]' "$PENDING_FILE"; then
        if [ "$FOUND" -eq 0 ]; then
            printf '%s\n' ''
            printf '%s\n' '========================================'
            printf '%s\n' 'WARNING: unfinished items found'
            printf '%s\n' 'Please review the following list before push:'
        fi

        printf '%s\n' "File: $PENDING_FILE"
        grep '^- \[ \]' "$PENDING_FILE"
        FOUND=1
    fi
done

if [ "$FOUND" -eq 1 ]; then
    printf '%s\n' '========================================'
    printf '%s\n' ''
fi

exit 0
'@

$todoContent = @'
# TODO

## Pending items

- [ ] item 1
- [ ] item 2
- [ ] item 3

## Notes

- `- [ ]` means unfinished and will trigger a reminder before `git push`
- `- [x]` means completed and will not trigger a reminder
'@

$testPendingContent = @'
# TEST_PENDING

## Pending items

- [ ] item 1
- [ ] item 2
- [ ] item 3

## Notes

- `- [ ]` means unfinished and will trigger a reminder before `git push`
- `- [x]` means completed and will not trigger a reminder
'@

$hookContent | Set-Content -Path $hookPath -Encoding ASCII

if (-not (Test-Path $todoPath)) {
    $todoContent | Set-Content -Path $todoPath -Encoding UTF8
}

if (-not (Test-Path $testPendingPath)) {
    $testPendingContent | Set-Content -Path $testPendingPath -Encoding UTF8
}

git -C $repoRoot config core.hooksPath .githooks

Write-Host "Initialization complete:"
Write-Host "  hooks: $hookDir"
Write-Host "  todo : $todoPath"
Write-Host "  test : $testPendingPath"
Write-Host "  config: core.hooksPath = .githooks"
