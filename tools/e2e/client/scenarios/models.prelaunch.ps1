# Client scenario 'models' (unit F2), before the game starts: add the test-only preview packs that
# show every converted model on a test actor (tools/models/e2e/preview.ps1) to the deployed mod, and
# note where and since when the game writes its content log (out/models/content-log.json), which
# models.ps1 reads for the preview pack's own checks. The game options of the run (content log file
# on, HUD and hand shown) and putting the instance's own values back afterwards are the harness's
# (run.ps1: Reset-GameOptions, Enable-ContentLog, Restore-RunOptions; lib.ps1 $script:ForcedOptions),
# which run before this script and after the scenario however the run ends.
# Dot-sourced by tools/e2e/client/run.ps1.
$gfModelsDest = Join-Path $script:ModsDir $script:ModName
$gfModelsOut = Join-Path $script:OutRoot $script:Unit
& pwsh -NoProfile -File (Join-Path $script:RepoRoot 'tools/models/e2e/preview.ps1') -ModDir $gfModelsDest -OutDir $gfModelsOut
if ($LASTEXITCODE -ne 0) { throw 'tools/models/e2e/preview.ps1 failed' }

$gfData = Get-GameDataDir
[ordered]@{ since = (Get-Date).ToString('o'); logs = (Join-Path $gfData 'logs') } | ConvertTo-Json -Depth 4 |
    Set-Content -Path (Join-Path $gfModelsOut 'content-log.json') -Encoding UTF8
Write-Step "preview packs added; content log of the run in $(Join-Path $gfData 'logs')"
