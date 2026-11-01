# Client scenario 'models' (unit F2), before the game starts:
# - add the test-only preview packs that show every converted model on a test actor
#   (tools/models/e2e/preview.ps1) to the deployed mod;
# - turn on the game's content log file (options.txt content_log_file:1; the game then writes
#   <game data>/logs/ContentLog<date>_<time>_1.txt), so models.ps1 can read what the client says
#   about the resource packs (geometry, animations, attachables, render controllers, client
#   entities); models.restore.ps1 puts the old value back.
# Dot-sourced by tools/e2e/client/run.ps1.
$gfModelsDest = Join-Path $script:ModsDir $script:ModName
$gfModelsOut = Join-Path $script:OutRoot $script:Unit
& pwsh -NoProfile -File (Join-Path $script:RepoRoot 'tools/models/e2e/preview.ps1') -ModDir $gfModelsDest -OutDir $gfModelsOut
if ($LASTEXITCODE -ne 0) { throw 'tools/models/e2e/preview.ps1 failed' }

$gfData = Get-GameDataDir
$gfOptions = @(Get-ChildItem -Recurse -Path $gfData -Filter 'options.txt' -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match '\\minecraftpe\\options\.txt$' })
if (-not $gfOptions) { throw "no options.txt below $gfData (start this game instance once by hand)" }
$gfBackup = [ordered]@{}
foreach ($o in $gfOptions) {
    $lines = @(Get-Content $o.FullName)
    $old = $lines | Select-String -Pattern '^content_log_file:(\d+)' | Select-Object -First 1
    $gfBackup[$o.FullName] = if ($old) { $old.Matches[0].Groups[1].Value } else { $null }
    $lines = if ($old) { $lines -replace '^content_log_file:\d+', 'content_log_file:1' } else { $lines + 'content_log_file:1' }
    Set-Content -Path $o.FullName -Value $lines
}
[ordered]@{ since = (Get-Date).ToString('o'); logs = (Join-Path $gfData 'logs'); options = $gfBackup } | ConvertTo-Json -Depth 4 |
    Set-Content -Path (Join-Path $gfModelsOut 'content-log.json') -Encoding UTF8
Write-Step "content log file on in $($gfOptions.Count) options.txt (logs: $(Join-Path $gfData 'logs'))"
