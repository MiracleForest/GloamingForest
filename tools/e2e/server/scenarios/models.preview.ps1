# Server scenario 'models.preview' (unit F2): the preview behavior pack of the client scenario 'models'
# (actors, test items, functions) loads on BDS without a content-log error (run.ps1 fails the run on
# any; a function with one bad command is dropped by the game), every preview function runs, and the
# follow-up commands of each shot (armor stands dressed in the preview items) succeed. BDS does not
# load resource packs (geometry, animations, attachables, render controllers): their content log is
# checked by the client scenario 'models'. Dot-sourced by tools/e2e/server/run.ps1.
$plan = Get-Content (Join-Path $script:OutDir 'preview-shots.json') -Raw | ConvertFrom-Json
# no player stands at the preview spot: keep its chunks loaded, or summons fail now and then
Send-Console 'tickingarea add -16 -64 -16 16 -40 16 gfpreview true'
Wait-Seconds 3
$mark = Get-LogMark
$sent = 0
foreach ($s in $plan.shots) {
    Send-Console "function gfpreview/$($s.name)"
    foreach ($c in @($s.commands)) { if ($c) { Send-Console $c; $sent++ } }
    Wait-Seconds 1
}
Send-Console 'function gfpreview/clear'
Wait-Seconds 2
Send-Console 'tickingarea remove gfpreview'
# empty selectors (nothing to clear yet) are fine; anything else is a broken preview command
$bad = @(Get-ServerLog -Since $mark | Select-String -Pattern 'functionNameNotFound|Syntax error|Command error: (?!commands\.generic\.noTargetMatch)' | ForEach-Object { $_.Line } | Sort-Object -Unique)
if ($bad) { throw "preview commands failed: $($bad -join ' | ')" }
Write-Step "models.preview: $($plan.shots.Count) preview functions and $sent follow-up commands ran"
