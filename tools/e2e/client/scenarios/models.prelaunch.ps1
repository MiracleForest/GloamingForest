# Client scenario 'models' (unit F2), before the game starts:
# - add the test-only preview packs that show every converted model on a test actor
#   (tools/models/e2e/preview.ps1) to the deployed mod;
# - game options of the instance (every minecraftpe\options.txt below its game data):
#   - content_log_file:1: the game then writes <game data>/logs/ContentLog<date>_<time>_1.txt, so
#     models.ps1 can read what the client says about the resource packs (geometry, animations,
#     attachables, render controllers, client entities);
#   - gfx_hidehud:0 and gfx_hidehand:0: the game starts with the HUD and the hand shown, whatever an
#     earlier run left behind (F1 persists both); models.ps1 then hides them with F1 for the preview
#     shots and shows them again, checking each press (models.client_hud);
#   the instance's own values are saved first in the e2e home (models.restore.ps1 puts them back
#   however the run ends; a hidden HUD or hand found here is saved as shown, never put back). The backup lives outside out/ (cleaned at the start of every run), so a
#   killed run's backup survives: the next run keeps it (it holds the values from before the killed
#   run) and restores it at its end.
# Dot-sourced by tools/e2e/client/run.ps1.
$gfModelsDest = Join-Path $script:ModsDir $script:ModName
$gfModelsOut = Join-Path $script:OutRoot $script:Unit
& pwsh -NoProfile -File (Join-Path $script:RepoRoot 'tools/models/e2e/preview.ps1') -ModDir $gfModelsDest -OutDir $gfModelsOut
if ($LASTEXITCODE -ne 0) { throw 'tools/models/e2e/preview.ps1 failed' }

$gfData = Get-GameDataDir
$gfOptions = @(Get-ChildItem -Recurse -Path $gfData -Filter 'options.txt' -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match '\\minecraftpe\\options\.txt$' })
if (-not $gfOptions) { throw "no options.txt below $gfData (start this game instance once by hand)" }
# the options this run sets, and their values for the run
$gfRunOptions = [ordered]@{ content_log_file = '1'; gfx_hidehud = '0'; gfx_hidehand = '0' }
$gfBackupFile = Join-Path (Get-E2EHome) "models-options.$($script:VersionName).json"
if (Test-Path $gfBackupFile) {
    Write-Step "kept the options backup of an earlier run that did not restore it ($gfBackupFile)"
} else {
    $gfBackup = [ordered]@{}
    foreach ($o in $gfOptions) {
        $lines = @(Get-Content $o.FullName)
        $values = [ordered]@{}
        foreach ($k in $gfRunOptions.Keys) {
            $old = $lines | Select-String -Pattern "^${k}:(.*)$" | Select-Object -First 1
            $values[$k] = if ($old) { $old.Matches[0].Groups[1].Value } else { $null }   # $null: no such line
        }
        # A hidden HUD or hand is never put back: it is what a lost or killed F1 of an earlier run
        # leaves behind, and every other client scenario needs the HUD (toasts, hotbar, screens).
        foreach ($k in 'gfx_hidehud', 'gfx_hidehand') {
            if ($values[$k] -and $values[$k] -ne '0') {
                Write-Step "warning: $($o.FullName) had ${k}:$($values[$k]) before this run (an earlier run's F1?); it is restored as ${k}:0"
                $values[$k] = '0'
            }
        }
        $gfBackup[$o.FullName] = $values
    }
    [ordered]@{ instance = $script:VersionName; saved = (Get-Date).ToString('o'); options = $gfBackup } | ConvertTo-Json -Depth 4 |
        Set-Content -Path $gfBackupFile -Encoding UTF8
}
foreach ($o in $gfOptions) {
    $lines = @(Get-Content $o.FullName)
    foreach ($k in $gfRunOptions.Keys) {
        $lines = if ($lines -match "^${k}:") { $lines -replace "^${k}:.*$", "${k}:$($gfRunOptions[$k])" } else { $lines + "${k}:$($gfRunOptions[$k])" }
    }
    Set-Content -Path $o.FullName -Value $lines
}
[ordered]@{ since = (Get-Date).ToString('o'); logs = (Join-Path $gfData 'logs') } | ConvertTo-Json -Depth 4 |
    Set-Content -Path (Join-Path $gfModelsOut 'content-log.json') -Encoding UTF8
Write-Step "options for the run in $($gfOptions.Count) options.txt: $(($gfRunOptions.Keys | ForEach-Object { "${_}:$($gfRunOptions[$_])" }) -join ', ') (saved in $gfBackupFile; logs: $(Join-Path $gfData 'logs'))"
