# Client scenario 'models' (unit F2), before the game starts: add the test-only preview packs that
# show every converted model on a test actor (tools/models/e2e/preview.ps1) to the deployed mod.
# Dot-sourced by tools/e2e/client/run.ps1.
$gfModelsDest = Join-Path $script:ModsDir $script:ModName
$gfModelsOut = Join-Path $script:OutRoot $script:Unit
& pwsh -NoProfile -File (Join-Path $script:RepoRoot 'tools/models/e2e/preview.ps1') -ModDir $gfModelsDest -OutDir $gfModelsOut
if ($LASTEXITCODE -ne 0) { throw 'tools/models/e2e/preview.ps1 failed' }
