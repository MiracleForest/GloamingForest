# Server scenario 'models.preview' (unit F2), before the server boots: add the test-only preview
# packs of the client scenario 'models' (tools/models/e2e/preview.ps1) to the deployed mod, so BDS
# loads the behavior pack with its content log on the console (BDS skips the resource packs).
# Dot-sourced by tools/e2e/server/run.ps1.
$gfModelsDest = Join-Path $script:SlotDir "$(Get-ModsDirName $script:BaseDir)\$script:ModName"
& pwsh -NoProfile -File (Join-Path $script:RepoRoot 'tools/models/e2e/preview.ps1') -ModDir $gfModelsDest -OutDir $script:OutDir
if ($LASTEXITCODE -ne 0) { throw 'tools/models/e2e/preview.ps1 failed' }
