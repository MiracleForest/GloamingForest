# Client scenario 'models' (unit F2), after the game closed (run.ps1 runs it however the run ended):
# put back the instance's own content_log_file, gfx_hidehud and gfx_hidehand of every options.txt
# that models.prelaunch.ps1 changed (its backup in the e2e home, models-options.<instance>.json; a
# backup that a killed run left behind is restored by the next models run), and read the values
# back. Dot-sourced by tools/e2e/client/run.ps1.
$gfBackupFile = Join-Path (Get-E2EHome) "models-options.$($script:VersionName).json"
if (Test-Path $gfBackupFile) {
    $gfSaved = Get-Content $gfBackupFile -Raw | ConvertFrom-Json
    $gfLeft = $false
    foreach ($p in $gfSaved.options.PSObject.Properties) {
        if (-not (Test-Path $p.Name)) { Write-Step "warning: $($p.Name) is gone; nothing to restore there"; continue }
        $lines = @(Get-Content $p.Name)
        $done = @()
        foreach ($k in $p.Value.PSObject.Properties) {
            $key = $k.Name
            if ($null -ne $k.Value) {
                $lines = if ($lines -match "^${key}:") { $lines -replace "^${key}:.*$", "${key}:$($k.Value)" } else { $lines + "${key}:$($k.Value)" }
                $done += "${key}:$($k.Value)"
            } else {
                $lines = @($lines | Where-Object { $_ -notmatch "^${key}:" })
                $done += "no ${key}"
            }
        }
        Set-Content -Path $p.Name -Value $lines
        # read back: the file must hold the saved values now (a game still running would rewrite it)
        $now = @(Get-Content $p.Name)
        foreach ($k in $p.Value.PSObject.Properties) {
            $line = $now | Where-Object { $_ -match "^$($k.Name):" } | Select-Object -First 1
            $want = if ($null -ne $k.Value) { "$($k.Name):$($k.Value)" } else { $null }
            if ($line -ne $want) { $gfLeft = $true; Write-Host "[e2e] FAILED to restore $($k.Name) in $($p.Name): '$line' instead of '$want'" -ForegroundColor Red }
        }
        Write-Step "options restored in $($p.Name): $($done -join ', ')"
    }
    if ($gfLeft) { throw "the options of $($script:VersionName) were not restored; the backup stays in $gfBackupFile for the next models run" }
    Remove-Item $gfBackupFile -Force
}
