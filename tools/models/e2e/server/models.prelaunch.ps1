# Server scenario 'models' (unit F2), before the server boots: put every converted model (all
# partitions, as once every W1 unit regenerated its own) into the deployed mod, so BDS loads them
# and the models.* selftests see the complete set. Dot-sourced by tools/e2e/server/run.ps1.
$gfModelsDest = Join-Path $script:SlotDir "$(Get-ModsDirName $script:BaseDir)\$script:ModName"
& pwsh -NoProfile -File (Join-Path $script:RepoRoot 'tools/models/e2e/preview.ps1') -ModDir $gfModelsDest
if ($LASTEXITCODE -ne 0) { throw 'tools/models/e2e/preview.ps1 failed' }
