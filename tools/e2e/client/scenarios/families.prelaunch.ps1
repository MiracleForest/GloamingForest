# Families (F11), before the game starts: the block family generator (tools/families) on the
# deployed client mod, so the gf_test fixture families exist in its gf_test packs. Remote modes
# take the host's mod from bin/server/GloamingForest: run `pwsh tools/families/run.ps1` after the
# build so the host has them too (until tools/xmake/assemble.lua runs the generator itself).
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
