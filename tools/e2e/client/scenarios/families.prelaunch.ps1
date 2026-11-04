# Families (F11), before the game starts: a safety net. tools/xmake/assemble.lua runs the block
# family generator (tools/families) on every build (both target types), so the deployed client mod
# and the host's copy from bin/server already carry the generated blocks (families-generated.txt)
# and this run is a no-op; it only completes a copy the build did not assemble.
$deployed = Join-Path $script:ModsDir $script:ModName
if ($script:Mode -eq 'local' -or $script:Mode -eq 'remote-mod') {
    Write-Step "families: generating the block families into $deployed"
    & pwsh -NoProfile -File (Join-Path $script:RepoRoot 'tools/families/run.ps1') -Out $deployed
    if ($LASTEXITCODE -ne 0) { throw 'tools/families/run.ps1 failed on the deployed mod' }
}
# the instance's own graphics mode, should a killed families.classic (or ui) run have left its
# backup behind (F12's option helpers; nothing to do without a backup)
. (Join-Path $PSScriptRoot 'ui.graphics.ps1')
Restore-UiGraphicsModes
