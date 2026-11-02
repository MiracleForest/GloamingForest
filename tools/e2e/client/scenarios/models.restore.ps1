# Client scenario 'models' (unit F2), after the game closed: put back the content_log_file value of
# every options.txt that models.prelaunch.ps1 turned on. Dot-sourced by tools/e2e/client/run.ps1.
$gfState = Join-Path (Join-Path $script:OutRoot $script:Unit) 'content-log.json'
if (Test-Path $gfState) {
    $gfSaved = Get-Content $gfState -Raw | ConvertFrom-Json
    foreach ($p in $gfSaved.options.PSObject.Properties) {
        if (-not (Test-Path $p.Name)) { continue }
        $lines = @(Get-Content $p.Name)
        $lines = if ($null -ne $p.Value) { $lines -replace '^content_log_file:\d+', "content_log_file:$($p.Value)" } else { @($lines | Where-Object { $_ -notmatch '^content_log_file:' }) }
        Set-Content -Path $p.Name -Value $lines
    }
    Write-Step "content log file setting restored"
}
