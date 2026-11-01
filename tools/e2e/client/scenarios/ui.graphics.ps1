# Helpers of the F12 (ui) client scenario: the game's graphics mode (options.txt `graphics_mode`:
# 1 = Fancy, the "Classic" look; 2 = Advanced, "Vibrant Visuals"). Dot-sourced by ui.prelaunch.ps1,
# ui.ps1 and ui.restore.ps1. The original values of this run's instance are kept in the e2e home
# (Get-E2EHome) and put back by ui.restore.ps1, so other harnesses sharing the instance keep their
# setting.

function Get-UiOptionsFiles {
    $data = Get-GameDataDir
    @(Get-ChildItem -Recurse -Path $data -Filter 'options.txt' -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match 'minecraftpe' })
}

# Outside out/ (cleaned at the start of every run): a killed run's backup survives until restored.
function Get-UiGraphicsBackupPath { Join-Path (Get-E2EHome) "ui-graphics-mode.$($script:VersionName).json" }

function Save-UiGraphicsModes {
    $backup = Get-UiGraphicsBackupPath
    if (Test-Path $backup) { return } # the first value of the run is the one to restore
    $orig = @{}
    foreach ($f in Get-UiOptionsFiles) {
        $line = Get-Content $f.FullName | Where-Object { $_ -match '^graphics_mode:' } | Select-Object -First 1
        $orig[$f.FullName] = if ($line) { $line } else { '' }
    }
    New-Item -ItemType Directory -Force (Split-Path $backup) | Out-Null
    $orig | ConvertTo-Json | Set-Content -Path $backup -Encoding UTF8
}

function Set-UiGraphicsMode([int]$Mode) {
    Save-UiGraphicsModes
    foreach ($f in Get-UiOptionsFiles) {
        $lines = @(Get-Content $f.FullName)
        if ($lines -match '^graphics_mode:') { $lines = $lines -replace '^graphics_mode:\d+', "graphics_mode:$Mode" }
        else { $lines += "graphics_mode:$Mode" }
        $lines | Set-Content $f.FullName
        Write-Step "graphics_mode:$Mode in $($f.FullName)"
    }
}

function Restore-UiGraphicsModes {
    $backup = Get-UiGraphicsBackupPath
    if (-not (Test-Path $backup)) { return }
    $orig = Get-Content $backup -Raw | ConvertFrom-Json
    foreach ($p in $orig.PSObject.Properties) {
        if (-not (Test-Path $p.Name)) { continue }
        $lines = @(Get-Content $p.Name)
        if ($p.Value) { $lines = $lines -replace '^graphics_mode:\d+', $p.Value }
        else { $lines = @($lines | Where-Object { $_ -notmatch '^graphics_mode:' }) }
        $lines | Set-Content $p.Name
        Write-Step "restored $($p.Value) in $($p.Name)"
    }
    Remove-Item $backup -Force
}
